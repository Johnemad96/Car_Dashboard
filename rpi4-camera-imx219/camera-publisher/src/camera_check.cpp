// SPDX-License-Identifier: MIT
//
// camera_check
//
// A no-ROS isolation test for the libcamera capture path. Its job is
// to prove the camera works end-to-end (sensor -> pipeline -> dmabuf
// -> mmap -> CPU read) before any networking, encoding, or ROS
// transport is involved. If this binary cannot print frames, the
// ROS node won't either, and you've cut the search space in half.
//
// Usage:
//   camera_check [--outdir PATH] [--count N] [--frames M]
//
//   --outdir P  directory for raw dumps. Default is empty, which
//               means "do not write anything to disk" -- the tool
//               just prints per-frame stats.
//   --count N   when --outdir is set, write every Nth frame
//               (default 30). Ignored when --outdir is empty.
//   --frames M  stop after M frames (default 0 = run until Ctrl-C)
//
// Each captured frame produces one terminal line of the form:
//
//   frame=<seq> 1640x1232 BGR888 stride=4928 bytes=6068736 dt=33.1ms (~30.2 fps)
//
// so the achieved frame rate is visible live without any external tool.
//
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "camera_publisher/camera_capture.hpp"

namespace {

std::atomic<bool> g_stop{false};

void onSigint(int) { g_stop = true; }

void usage(const char *argv0) {
  std::fprintf(stderr,
               "usage: %s [--outdir PATH] [--count N] [--frames M]\n"
               "  --outdir P   directory for raw dumps. Default empty:\n"
               "               nothing is written, only stats are printed.\n"
               "  --count N    when --outdir is set, write every Nth frame\n"
               "               (default 30). Ignored if --outdir is empty.\n"
               "  --frames M   stop after M frames (default 0 = run forever)\n",
               argv0);
}

}  // namespace

int main(int argc, char **argv) {
  // Defaults: do NOT dump anything. The tool is primarily a live
  // monitor; writing raw frames to disk is opt-in via --outdir.
  // The 30-frame cadence is only applied once dumping is enabled.
  unsigned int   count_n = 30;
  std::string    outdir;            // empty => no disk writes
  std::uint64_t  frames_max = 0;
  std::uint32_t  width   = 1640;
  std::uint32_t  height  = 1232;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--count" && i + 1 < argc) {
      count_n = static_cast<unsigned int>(std::atoi(argv[++i]));
      if (count_n == 0) count_n = 1;
    } else if (a == "--outdir" && i + 1 < argc) {
      outdir = argv[++i];
    } else if (a == "--frames" && i + 1 < argc) {
      frames_max = static_cast<std::uint64_t>(std::atoll(argv[++i]));
    } else if (a == "--width" && i + 1 < argc) {
      width = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--height" && i + 1 < argc) {
      height = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    } else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
      usage(argv[0]);
      return 2;
    }
  }

  // Clean SIGINT shutdown: flip a flag the main thread polls. We rely
  // on the capture core's destructor to stop the pipeline + release
  // the camera; doing it from the signal handler directly would be
  // unsafe (libcamera calls aren't async-signal-safe).
  std::signal(SIGINT, onSigint);
  std::signal(SIGTERM, onSigint);

  // Only touch the filesystem if the user asked us to dump frames.
  const bool dump_enabled = !outdir.empty();
  if (dump_enabled) {
    std::filesystem::create_directories(outdir);
  }

  camera_publisher::CameraCapture cap;
  camera_publisher::CaptureConfig cfg;
  cfg.width = width;
  cfg.height = height;
  cfg.fps_target = 30.0;

  if (int rc = cap.open(cfg); rc != 0) {
    std::fprintf(stderr, "camera_check: open() failed (%d)\n", rc);
    return 1;
  }

  // Counters live on the main thread but are mutated from the libcamera
  // completion thread inside the callback. std::atomic keeps that safe.
  std::atomic<std::uint64_t> frame_count{0};
  std::atomic<bool> hit_max{false};

  // Last-frame timestamp for the dt print. Owned by callback only.
  std::uint64_t last_ts_ns = 0;
  unsigned int  every = count_n;

  auto on_frame = [&](const camera_publisher::Frame &f) {
    std::uint64_t n = frame_count.fetch_add(1) + 1;

    double dt_ms = 0.0;
    if (last_ts_ns != 0 && f.timestamp_ns > last_ts_ns) {
      dt_ms = (f.timestamp_ns - last_ts_ns) / 1.0e6;
    }
    last_ts_ns = f.timestamp_ns;
    double fps = (dt_ms > 0.0) ? (1000.0 / dt_ms) : 0.0;

    std::printf("frame=%u %ux%u %s stride=%u bytes=%zu dt=%.1fms (~%.1f fps)\n",
                f.sequence, f.width, f.height,
                f.pixel_format_str.c_str(), f.stride,
                f.length, dt_ms, fps);
    std::fflush(stdout);

    if (dump_enabled && n % every == 0) {
      char path[512];
      std::snprintf(path, sizeof(path),
                    "%s/camera_check_%06u_%ux%u_%s.raw",
                    outdir.c_str(), f.sequence, f.width, f.height,
                    f.pixel_format_str.c_str());
      // Write the whole mapped plane. Consumers can use the printed
      // stride to deinterleave if the format has padded rows.
      std::ofstream out(path, std::ios::binary);
      if (out) {
        out.write(reinterpret_cast<const char *>(f.data),
                  static_cast<std::streamsize>(f.length));
        std::fprintf(stderr, "  -> wrote %s (%zu bytes)\n", path, f.length);
      } else {
        std::fprintf(stderr, "  -> failed to open %s\n", path);
      }
    }

    if (frames_max != 0 && n >= frames_max) {
      hit_max = true;
    }
  };

  if (int rc = cap.start(on_frame); rc != 0) {
    std::fprintf(stderr, "camera_check: start() failed (%d)\n", rc);
    return 1;
  }

  // Idle wait. The libcamera completion thread does the work; we just
  // block here until SIGINT or frames_max is reached. A polled sleep
  // is fine since this is a one-shot diagnostic tool.
  using namespace std::chrono_literals;
  while (!g_stop && !hit_max) {
    std::this_thread::sleep_for(50ms);
  }

  std::fprintf(stderr, "camera_check: stopping after %llu frames\n",
               static_cast<unsigned long long>(frame_count.load()));
  cap.stop();
  return 0;
}
