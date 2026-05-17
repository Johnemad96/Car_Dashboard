# camera-publisher

A ROS 2 package that captures from a Raspberry Pi Camera v2 (Sony IMX219)
via the **libcamera C++ API** and either publishes the frames over ROS or
prints them to a terminal for diagnostic use.

The package ships two executables that share a single libcamera capture
core (`src/camera_capture.cpp`):

| Executable | Depends on ROS? | Purpose |
| --- | --- | --- |
| `camera_check` | No | Isolation test. Proves libcamera works end-to-end on the device with zero ROS or networking variables. |
| `camera_publisher_node` | Yes | ROS 2 node. Publishes each frame in either raw or JPEG form. |

Target ROS 2 distro is **Humble** on the Pi/Yocto image. The code is also
verified building and running on **Jazzy** on Ubuntu 24.04 for native
development; the source intentionally avoids distro-specific APIs.

---

## Building

```bash
# On Ubuntu 24.04 (laptop dev):
sudo apt install libcamera-dev libturbojpeg0-dev
source /opt/ros/jazzy/setup.bash
cd camera-publisher
colcon build --base-paths . --build-base ./build --install-base ./install
source install/setup.bash
```

The Yocto recipe that builds this package for the Pi 4 image is not part
of this package -- it is added in a separate step at the
`yocto/car-dashboard/` layer.

---

## `camera_check`

A no-ROS isolation test. Captures frames from the camera using libcamera
and prints one line per frame, plus optionally dumps every Nth raw frame
to disk.

### CLI arguments

| Argument | Default | Meaning |
| --- | --- | --- |
| `--count N` | `30` | Dump every Nth captured frame as a raw file. Must be >= 1. |
| `--outdir P` | `/tmp` | Directory to write raw dumps into. Created if missing. |
| `--frames M` | `0` | Stop after M frames. `0` = run until Ctrl-C. |
| `--width W`  | `1640` | Requested width. Libcamera may adjust. |
| `--height H` | `1232` | Requested height. Libcamera may adjust. |

### Example

```bash
# Run forever, dumping every 60th frame to ~/dumps:
./install/camera_publisher/lib/camera_publisher/camera_check \
    --count 60 --outdir ~/dumps

# Run for exactly 30 frames at 1280x720, dump every 5th:
./install/camera_publisher/lib/camera_publisher/camera_check \
    --width 1280 --height 720 --count 5 --frames 30
```

### Output

```
frame=0 1640x1232 XRGB8888 stride=6560 bytes=8081920 dt=0.0ms (~0.0 fps)
frame=1 1640x1232 XRGB8888 stride=6560 bytes=8081920 dt=33.1ms (~30.2 fps)
frame=2 1640x1232 XRGB8888 stride=6560 bytes=8081920 dt=33.1ms (~30.2 fps)
  -> wrote /tmp/camera_check_000030_1640x1232_XRGB8888.raw (8081920 bytes)
```

Each line carries: the libcamera-reported sequence number, the negotiated
resolution and pixel format, the row stride (which is **not** the same as
`width * bytes_per_pixel` in general -- the hardware may pad), the bytes
in this frame, and the inter-frame delta in milliseconds.

---

## `camera_publisher_node`

The ROS 2 node. The output mode is selected by a **required**
command-line argument so that during debugging it is unambiguous from a
`ros2 topic list` what the node is emitting.

### CLI arguments

| Argument | Default | Meaning |
| --- | --- | --- |
| `--mode raw\|jpeg` | (required) | Output mode. See below. |
| `--width W`        | `1640` | Requested width. |
| `--height H`       | `1232` | Requested height. |
| `--fps F`          | `30.0` | Target frame rate. |
| `--quality Q`      | `80`   | JPEG quality 1..100. Ignored in `raw` mode. |

ROS-injected `--ros-args ...` flags are passed through to rclcpp.

### Modes

| `--mode` | Topic | Message type |
| --- | --- | --- |
| `raw`  | `/camera/image_raw`         | `sensor_msgs/msg/Image` |
| `jpeg` | `/camera/image_compressed`  | `sensor_msgs/msg/CompressedImage` |

The topic AND the message type differ between modes deliberately so it is
obvious on the wire which encoding is active.

QoS: the ROS 2 **sensor_data** profile (best-effort, depth 5). Live video
should drop stale frames rather than buffer them.

### Examples

```bash
# Source the workspace first:
source install/setup.bash

# Raw mode at default resolution:
ros2 run camera_publisher camera_publisher_node --mode raw

# JPEG at 1280x720, quality 70, ~15 fps:
ros2 run camera_publisher camera_publisher_node \
    --mode jpeg --width 1280 --height 720 --fps 15 --quality 70
```

### Verify on the subscriber side

```bash
# raw mode
ros2 topic hz   /camera/image_raw
ros2 topic bw   /camera/image_raw
ros2 topic echo /camera/image_raw --no-arr        # suppresses the giant data array

# jpeg mode
ros2 topic hz   /camera/image_compressed
ros2 topic bw   /camera/image_compressed
ros2 topic echo /camera/image_compressed --no-arr
```

`hz` confirms cadence, `bw` confirms throughput (very useful for noticing
that JPEG is one-to-two orders of magnitude smaller than raw), and the
`--no-arr` form of `echo` lets you inspect the header and encoding
without spamming the terminal with binary.

---

## IMX219 resolution modes worth knowing

The IMX219 is a 3280x2464 sensor. The Raspberry Pi pipeline (the bcm2835
ISP / vc4-libcamera pipeline handler) exposes several "modes" that
combine binning and cropping:

| Mode | Resolution | Notes |
| --- | --- | --- |
| Full | 3280x2464 | Slow, useful for stills. |
| 2x2 binned | **1640x1232** | **Recommended default**: full FOV, 2x2 pixel binning, ~30 fps achievable. |
| 720p crop | 1640x922  | Wider aspect crop. |
| 480p crop | 640x480   | Low resolution, often used for fast preview. |

`generateConfiguration({Viewfinder})` in this package asks the pipeline
handler for a sensible mode, and `validate()` will adjust the request to
the closest legal one. Always read the log line
`negotiated: WxH fmt=... stride=... frameSize=...` to confirm what you
actually got.

---

## libcamera capture lifecycle (the short version)

This README would not be complete without a one-page mental model of
what `camera_capture.cpp` is doing under the hood. The numbered steps
match the section headers in that file.

1. **`CameraManager::start()`** spins up libcamera's pipeline-handler
   machinery and enumerates cameras. It must outlive every Camera handle
   we acquire from it.
2. **`cm->cameras()`** lists what was found; we pick `[0]`.
3. **`Camera::acquire()`** takes exclusive control. Other processes
   (including the `cam` CLI) cannot stream the camera while we hold it.
4. **`generateConfiguration({StreamRole::Viewfinder})`** asks the
   pipeline handler for a sensible default `StreamConfiguration` given
   the role we declare.
5. We override only `size` and `bufferCount` -- pixel format is left to
   libcamera so we always get something the platform can emit
   efficiently.
6. **`config->validate()`** returns `Valid`, `Adjusted`, or `Invalid`.
   If `Adjusted`, the fields have been rewritten in place to the closest
   legal combination; we re-read them.
7. **`Camera::configure(config)`** commits.
8. **`FrameBufferAllocator::allocate(stream)`** produces DMA-capable
   buffers (dmabuf fds on Linux).
9. For each buffer we **`mmap()`** every plane so the CPU can read it.
10. We create **one `Request` per buffer** and attach the buffer to it
    via `addBuffer()`. Requests are reused for the lifetime of the
    pipeline (`request->reuse(ReuseBuffers)`).
11. We connect a slot to **`camera->requestCompleted`** -- the
    asynchronous signal libcamera fires when a Request has been
    filled. **This slot runs on a libcamera-owned thread.**
12. We supply a `FrameDurationLimits` control to pin the frame rate
    and call **`camera->start()`** to begin streaming.
13. We **prime** the pipeline by queueing every Request once.
14. Frames arrive via `requestCompleted`. Inside the callback we
    consume the buffer, then `reuse(ReuseBuffers)` and re-queue the
    Request so the pool stays full.
15. On shutdown: `camera->stop()`, drop all Requests, `munmap` every
    plane, `allocator->free()`, `camera->release()`, drop the manager.

### Pitfalls that bite people

* **Stride is not `width * bytes_per_pixel`.** Hardware buffers are
  often padded to align rows. Always use `StreamConfiguration::stride`
  for `sensor_msgs/Image::step` and for any pixel-by-pixel access. The
  capture core's `Frame::stride` carries this through.
* **The completion callback runs on libcamera's thread.** Anything you
  touch from it must be either thread-safe or protected. In this
  package the callback only invokes a `std::function` we own, and the
  ROS path only calls `rclcpp::Publisher::publish` (thread-safe) and
  `Logger`/`Clock` methods (also thread-safe).
* **Pixel format is negotiated, not fixed.** Both executables log the
  negotiated format on startup; the JPEG encoder rejects formats it
  does not know about (`-ENOTSUP`) rather than producing garbage. If you
  see a `WARN: unsupported pixel format ...` line, extend
  `jpeg_encoder.cpp::mapPacked()` or the YUV branch.
