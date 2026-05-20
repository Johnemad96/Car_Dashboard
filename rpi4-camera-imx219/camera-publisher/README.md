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

### Examples## libcamera capture lifecycle (the short version)


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

## Pipeline architecture

The package factors the capture path into one library and two thin
front-ends. The library (`camera_capture`) owns every interaction with
libcamera; the two binaries (`camera_check`, `camera_publisher_node`)
consume frames via a single `FrameCallback` and decide what to do with
them.

```
                    +------------------------------+
sensor (IMX219)     |  camera_capture (library)    |
        |           |  - libcamera CameraManager   |
        v           |  - StreamConfiguration       |
   MIPI CSI-2       |  - FrameBufferAllocator      |
        |           |  - Request lifecycle         |
        v           |  - mmap + FrameCallback fan- |
  Unicam + ISP      |    out to consumers          |
  (bcm2835)         +---------------+--------------+
        |                           |
        v                           v
 libcamera vc4         +------------------------+      +------------------------+
 pipeline handler ---> | camera_check           |      | camera_publisher_node  |
                       | - per-frame stats line |      | - raw  -> sensor_msgs  |
                       | - optional raw dump    |      |          /Image        |
                       +------------------------+      | - jpeg -> CompressedImg|
                                                       |   via libjpeg-turbo    |
                                                       +-----------+------------+
                                                                   |
                                                                   v
                                                       ROS 2 (rclcpp), sensor_data QoS
```

### Stage-by-stage

| Stage                         | What happens                                                                                                                                                                                                                                              |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **1. Manager + acquire**      | `CameraManager::start()` enumerates cameras via the rpi/vc4 pipeline handler. `Camera::acquire()` takes exclusive control (no other process, not even the `cam` CLI, can stream).                                                                          |
| **2. Configure stream**       | `generateConfiguration({Viewfinder})` returns a sensible default. We override only `size` and `bufferCount`; pixel format is left to libcamera, then `validate()` may adjust the request to the nearest legal combination, which we re-read.              |
| **3. Allocate buffers**       | `FrameBufferAllocator::allocate(stream)` produces DMA-capable buffers (dmabuf fds). Each plane is `mmap()`-ed once at startup so the CPU can read frame contents with no per-frame syscall.                                                                |
| **4. Build Requests**         | One `Request` per buffer. Requests are reused for the lifetime of the pipeline via `reuse(ReuseBuffers)` -- libcamera does not allocate per frame.                                                                                                        |
| **5. Start + prime**          | `camera->start()` with a `FrameDurationLimits` control pins the frame rate. We then queue every Request once to prime the pipeline.                                                                                                                       |
| **6. Frame completion**       | libcamera fires `requestCompleted` on its own internal thread when a Request has been filled. The slot wraps the buffer in a `Frame` struct (pointer + length + format + stride + timestamp) and hands it to the `FrameCallback` the consumer registered. |
| **7a. Consumer: stats only**  | `camera_check` prints one line per frame with format, stride, bytes, and inter-frame delta, and optionally dumps every Nth frame to disk. No ROS, no network.                                                                                              |
| **7b. Consumer: ROS raw**     | `camera_publisher_node --mode raw` wraps the buffer in `sensor_msgs/Image` (encoding derived from the negotiated pixel format, `step = stride`) and publishes on `/camera/image_raw`.                                                                      |
| **7c. Consumer: ROS jpeg**    | `camera_publisher_node --mode jpeg` calls `JpegEncoder::encode()` (libjpeg-turbo, CPU) and publishes the resulting bytes as `sensor_msgs/CompressedImage` on `/camera/image_compressed`. Format string is `"jpeg"`.                                        |
| **8. Re-queue**               | The callback returns; the capture core calls `request->reuse(ReuseBuffers)` and re-queues so the buffer pool stays full. There is no per-frame allocation in steady state.                                                                                |
| **9. Shutdown**               | On SIGINT: `camera->stop()`, drop Requests, `munmap` every plane, `allocator->free()`, `camera->release()`, drop the manager. Skipping any of these leaves the camera acquired and the Pi needs a reboot to recover.                                       |

### Threading model

* The completion callback runs on **libcamera's own thread**, not the
  thread that called `start()`. Everything touched from the callback
  must be thread-safe or externally synchronised.
* In `camera_check` the callback only writes to `stdout` and
  (optionally) the filesystem -- both safe.
* In `camera_publisher_node` the callback calls
  `rclcpp::Publisher::publish` (thread-safe), `Logger`, and `Clock`
  (also thread-safe). It does not touch shared state belonging to the
  main thread.

### Things worth knowing before you read the code

* **Stride is not `width * bytes_per_pixel`.** Hardware buffers are
  padded for row alignment. Use `StreamConfiguration::stride` for
  `sensor_msgs/Image::step` and any pixel-by-pixel work; the capture
  core's `Frame::stride` carries this through.
* **Pixel format is negotiated, not fixed.** Startup logs the
  negotiated format; the JPEG encoder rejects unknown formats with
  `-ENOTSUP` rather than producing garbage. A `WARN: unsupported
  pixel format ...` line means extend `jpeg_encoder.cpp::mapPacked()`
  or the YUV branch.
* **libcamera XRGB8888 is BGRX in memory** on little-endian, so
  `jpeg_encoder.cpp` maps it to `TJPF_BGRX`. Mapping it to `TJPF_XRGB`
  swaps red and blue in the output JPEG.

For the deeper, code-cross-referenced walkthrough of the libcamera
lifecycle (the full numbered tour matching `camera_capture.cpp`
section headers), see `LIBCAMERA_LIFECYCLE.md` next to this README.
That file is local-only and not tracked in git.
