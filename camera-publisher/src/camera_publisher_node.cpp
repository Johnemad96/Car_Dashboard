// SPDX-License-Identifier: MIT
//
// camera_publisher_node
//
// ROS 2 node that drives the same libcamera capture core as the
// no-ROS camera_check tool and publishes each frame on a topic.
//
// The output mode is a REQUIRED command-line argument:
//
//   --mode raw    publishes sensor_msgs/Image on /camera/image_raw
//   --mode jpeg   publishes sensor_msgs/CompressedImage on /camera/image_compressed
//                 (JPEG-encoded on the fly via TurboJPEG)
//
// The topic name AND the message type are deliberately different
// between the two modes so that a subscriber, a `ros2 topic list`,
// or a `ros2 bag info` makes the encoding state immediately obvious.
//
// QoS profile: sensor data (best-effort, depth=5). Video streams
// are real-time -- dropping an old frame to deliver a fresh one is
// the correct trade-off.
//
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>

#include "camera_publisher/camera_capture.hpp"
#include "camera_publisher/jpeg_encoder.hpp"

namespace {

enum class Mode { kRaw, kJpeg };

struct Args {
  Mode          mode      = Mode::kRaw;
  std::uint32_t width     = 1640;
  std::uint32_t height    = 1232;
  double        fps       = 30.0;
  int           quality   = 80;
};

void usage(const char *argv0) {
  std::fprintf(stderr,
               "usage: %s --mode {raw|jpeg} [--width W] [--height H]\n"
               "          [--fps F] [--quality Q]\n",
               argv0);
}

bool parseArgs(int argc, char **argv, Args &out) {
  bool mode_set = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    // Tolerate the ROS-injected --ros-args block: anything after it is
    // for rclcpp::init to handle, not us.
    if (a == "--ros-args") break;
    if (a == "--mode" && i + 1 < argc) {
      std::string m = argv[++i];
      if (m == "raw")  { out.mode = Mode::kRaw;  mode_set = true; }
      else if (m == "jpeg") { out.mode = Mode::kJpeg; mode_set = true; }
      else { std::fprintf(stderr, "--mode must be raw or jpeg\n"); return false; }
    } else if (a == "--width"  && i + 1 < argc) {
      out.width = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--height" && i + 1 < argc) {
      out.height = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--fps"    && i + 1 < argc) {
      out.fps = std::atof(argv[++i]);
    } else if (a == "--quality" && i + 1 < argc) {
      out.quality = std::atoi(argv[++i]);
    } else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      std::exit(0);
    } else {
      std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
      return false;
    }
  }
  if (!mode_set) {
    std::fprintf(stderr, "--mode is required\n");
    return false;
  }
  return true;
}

// Map a libcamera pixel format to the sensor_msgs/Image::encoding string
// expected by REP 144. Returns empty string for an unsupported format
// (caller drops the frame with a loud log).
std::string rosEncodingFor(libcamera::PixelFormat pf) {
  using namespace libcamera::formats;
  if (pf == RGB888)   return "rgb8";
  if (pf == BGR888)   return "bgr8";
  if (pf == XRGB8888 || pf == ARGB8888) return "8UC4";  // X/A channel is don't-care, downstream may ignore
  if (pf == XBGR8888 || pf == ABGR8888) return "8UC4";
  if (pf == RGBA8888) return "rgba8";
  if (pf == BGRA8888) return "bgra8";
  if (pf == YUV420)   return "8UC1";   // Y plane; full frame published as raw planar bytes; consumer needs to know layout
  return {};
}

}  // namespace

class CameraPublisherNode : public rclcpp::Node {
 public:
  CameraPublisherNode(const Args &args, std::shared_ptr<camera_publisher::CameraCapture> cap)
      : rclcpp::Node("camera_publisher"),
        args_(args),
        capture_(std::move(cap)) {
    // sensor_data QoS profile: best-effort delivery, small queue.
    // This is the right call for live video; we'd rather lose an
    // old frame than buffer it on a slow subscriber.
    auto qos = rclcpp::QoS(rclcpp::QoSInitialization(
                               rmw_qos_profile_sensor_data.history,
                               5),
                           rmw_qos_profile_sensor_data);

    if (args_.mode == Mode::kRaw) {
      pub_raw_ = create_publisher<sensor_msgs::msg::Image>(
          "/camera/image_raw", qos);
      RCLCPP_INFO(get_logger(),
                  "publishing sensor_msgs/Image on /camera/image_raw");
    } else {
      pub_jpeg_ = create_publisher<sensor_msgs::msg::CompressedImage>(
          "/camera/image_compressed", qos);
      RCLCPP_INFO(get_logger(),
                  "publishing sensor_msgs/CompressedImage on /camera/image_compressed (q=%d)",
                  args_.quality);
    }
  }

  // Bound to the capture core. Runs on libcamera's completion thread,
  // NOT the ROS executor thread. We must not touch any rclcpp object
  // that isn't documented thread-safe; Publisher::publish IS safe,
  // and the Node logger is safe, so we restrict ourselves to those.
  void onFrame(const camera_publisher::Frame &f) {
    rclcpp::Time stamp = now();
    if (args_.mode == Mode::kRaw) {
      publishRaw(f, stamp);
    } else {
      publishJpeg(f, stamp);
    }
  }

 private:
  void publishRaw(const camera_publisher::Frame &f, const rclcpp::Time &stamp) {
    std::string enc = rosEncodingFor(f.pixel_format);
    if (enc.empty()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                           "unsupported pixel format for raw publish: %s",
                           f.pixel_format_str.c_str());
      return;
    }
    auto msg = std::make_unique<sensor_msgs::msg::Image>();
    msg->header.stamp = stamp;
    msg->header.frame_id = "camera";
    msg->height = f.height;
    msg->width  = f.width;
    msg->encoding = enc;
    msg->is_bigendian = 0;
    // step is bytes-per-row, and MUST be the libcamera-reported stride
    // because the pipeline may have padded rows.
    msg->step = f.stride;
    msg->data.assign(f.data, f.data + f.length);
    pub_raw_->publish(std::move(msg));
  }

  void publishJpeg(const camera_publisher::Frame &f, const rclcpp::Time &stamp) {
    thread_local std::vector<std::uint8_t> buf;
    int rc = encoder_.encode(f.data, f.length,
                             f.width, f.height, f.stride,
                             f.pixel_format, args_.quality, buf);
    if (rc != 0) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                           "JPEG encode failed (rc=%d) for format %s",
                           rc, f.pixel_format_str.c_str());
      return;
    }
    auto msg = std::make_unique<sensor_msgs::msg::CompressedImage>();
    msg->header.stamp = stamp;
    msg->header.frame_id = "camera";
    msg->format = "jpeg";
    msg->data.assign(buf.begin(), buf.end());
    pub_jpeg_->publish(std::move(msg));
  }

  Args                                                                  args_;
  std::shared_ptr<camera_publisher::CameraCapture>                      capture_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr                 pub_raw_;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr       pub_jpeg_;
  camera_publisher::JpegEncoder                                         encoder_;
};

namespace {
std::atomic<bool> g_stop{false};
void onSig(int) { g_stop = true; }
}  // namespace

int main(int argc, char **argv) {
  Args args;
  if (!parseArgs(argc, argv, args)) {
    usage(argv[0]);
    return 2;
  }

  rclcpp::init(argc, argv);

  // Install our own SIGINT handler AFTER rclcpp::init so we have a
  // chance to tear the camera down cleanly before rclcpp::shutdown
  // unwinds. (rclcpp installs its own handler; we layer on top by
  // also setting g_stop so the wait loop can exit.)
  std::signal(SIGINT,  onSig);
  std::signal(SIGTERM, onSig);

  auto cap = std::make_shared<camera_publisher::CameraCapture>();
  camera_publisher::CaptureConfig cfg;
  cfg.width = args.width;
  cfg.height = args.height;
  cfg.fps_target = args.fps;

  if (int rc = cap->open(cfg); rc != 0) {
    std::fprintf(stderr, "camera_publisher: open() failed (%d)\n", rc);
    rclcpp::shutdown();
    return 1;
  }

  auto node = std::make_shared<CameraPublisherNode>(args, cap);

  // Captured frames are pushed into the node from the libcamera
  // thread; the ROS executor runs in parallel to drive timers,
  // service responses, etc. on this main thread.
  cap->start([node](const camera_publisher::Frame &f) {
    node->onFrame(f);
  });

  // Spin executor; check stop flag each tick.
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  while (rclcpp::ok() && !g_stop) {
    exec.spin_some(std::chrono::milliseconds(50));
  }

  RCLCPP_INFO(node->get_logger(), "shutting down: stopping camera");
  cap->stop();
  rclcpp::shutdown();
  return 0;
}
