// SPDX-License-Identifier: MIT
//
// camera_capture.hpp
//
// A tiny C++ wrapper around the libcamera C++ API that drives a single
// IMX219-class camera, mmaps the output buffers, and delivers each
// captured frame to a user-supplied callback.
//
// Both first-party executables in this package (camera_check and
// camera_publisher_node) drive the camera through this class, so the
// libcamera lifecycle lives in exactly one place.
//
// Threading note (READ THIS):
//   libcamera dispatches the requestCompleted signal on an internal
//   thread it owns. The FrameCallback you pass to start() is invoked
//   on that thread. While the callback runs, the underlying libcamera
//   buffer is still mapped and the pointer in Frame::data is valid.
//   The instant the callback returns, the capture core re-queues the
//   request, which may make the buffer available for the next exposure.
//   Therefore: do not retain Frame::data past the end of the callback;
//   copy out anything you need to keep.
//
// Pixel format note:
//   We do not assume any particular pixel format. The constructor
//   requests a Viewfinder role, configure() returns whatever libcamera
//   negotiates for the platform, and the negotiated format is reported
//   via streamConfig(). camera_check prints it; camera_publisher_node
//   branches on it. Consumers must handle at least:
//     - A packed BGR/RGB/XBGR/XRGB 24/32-bit format
//     - A YUV variant (typically YUV420 or NV12)
//   See README.md for the full list of formats observed in practice
//   on Pi 4 + IMX219.
//
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <libcamera/libcamera.h>

namespace camera_publisher {

// A single captured frame, handed to the user callback.
//
// data points into an mmap'd region owned by the capture core. It is
// only valid for the duration of the callback invocation.
struct Frame {
  const uint8_t *data;           // pointer to plane 0 of the buffer
  std::size_t    length;         // bytes used in plane 0 (from metadata)
  std::size_t    plane_size;     // total bytes mapped for plane 0
  std::uint32_t  width;
  std::uint32_t  height;
  std::uint32_t  stride;         // bytes per row as libcamera reports it
  libcamera::PixelFormat pixel_format;
  std::string    pixel_format_str;
  std::uint32_t  sequence;       // monotonically increasing sensor sequence
  std::uint64_t  timestamp_ns;   // libcamera-reported capture timestamp
};

// Configuration knobs surfaced to executables.
struct CaptureConfig {
  std::uint32_t width        = 1640;   // IMX219 binned full-FOV mode
  std::uint32_t height       = 1232;   // (default that fits a 2x2 binned sensor)
  double        fps_target   = 30.0;   // best-effort, requested via FrameDurationLimits
};

class CameraCapture {
 public:
  using FrameCallback = std::function<void(const Frame &)>;

  CameraCapture();
  ~CameraCapture();

  CameraCapture(const CameraCapture &) = delete;
  CameraCapture &operator=(const CameraCapture &) = delete;

  // Bring the camera up to "configured + buffers allocated, but not
  // streaming yet." Returns 0 on success, a negative errno on failure.
  // Errors are also logged to stderr with a human-readable explanation.
  int open(const CaptureConfig &cfg);

  // Start streaming. cb fires on libcamera's internal completion thread,
  // once per frame, until stop() is called.
  int start(FrameCallback cb);

  // Idempotent. Stops streaming, releases the camera, unmaps buffers.
  void stop();

  // The configuration libcamera actually accepted (may differ from
  // what was requested if libcamera Adjusted the request).
  const libcamera::StreamConfiguration &streamConfig() const { return *negotiated_; }

  // Has start() been called and not yet stop()?
  bool streaming() const { return streaming_; }

 private:
  // Bound to camera_->requestCompleted at start(). Runs on a libcamera
  // thread, so it must do its own synchronisation.
  void onRequestCompleted(libcamera::Request *request);

  // Per-buffer mmap bookkeeping. We mmap each buffer's plane(s) once at
  // allocation time and keep the pointer + length around until teardown.
  struct MappedPlane {
    void       *addr   = nullptr;
    std::size_t length = 0;
  };
  struct MappedBuffer {
    std::vector<MappedPlane> planes;
  };

  std::unique_ptr<libcamera::CameraManager>           manager_;
  std::shared_ptr<libcamera::Camera>                  camera_;
  std::unique_ptr<libcamera::CameraConfiguration>     config_;
  libcamera::StreamConfiguration                     *negotiated_ = nullptr;
  libcamera::Stream                                  *stream_     = nullptr;
  std::unique_ptr<libcamera::FrameBufferAllocator>    allocator_;
  std::vector<std::unique_ptr<libcamera::Request>>    requests_;

  // FrameBuffer* -> mapped plane(s). FrameBuffer pointers are stable for
  // the buffer's whole lifetime, so they make good keys.
  std::unordered_map<libcamera::FrameBuffer *, MappedBuffer> mapped_;

  FrameCallback        on_frame_;
  std::mutex           cb_mutex_;   // guards on_frame_ + streaming_ flips
  std::atomic<bool>    streaming_{false};
  // Set by stop() BEFORE camera_->stop() so the libcamera completion
  // thread can bail out of the requeue path while the pipeline is
  // transitioning to Stopping. Without this, in-flight callbacks race
  // camera_->stop() and call queueRequest() on a stopping pipeline,
  // which libcamera rejects with -EACCES (-13).
  std::atomic<bool>    stopping_{false};
  std::atomic<bool>    acquired_{false};
  std::uint64_t        target_frame_duration_us_ = 0;
};

}  // namespace camera_publisher
