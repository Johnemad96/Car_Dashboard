# camera-publisher: design notes

## What this package does and does not do

* It captures from a libcamera-enumerated camera (intended target:
  Raspberry Pi Camera v2 / IMX219 on a Pi 4 running our Yocto image).
* It exposes the captured frames in three forms: stdout (`camera_check`),
  `sensor_msgs/Image` (raw), and `sensor_msgs/CompressedImage` (JPEG).
* It does **not** ship a Yocto recipe; that comes later in
  `yocto/car-dashboard/recipes-multimedia/` once the package is happy.
* It does **not** do any image processing (debayering, color correction,
  white balance). It hands through whatever libcamera negotiated.

## Source layout

```
camera-publisher/
├── CMakeLists.txt
├── package.xml
├── README.md
├── NOTES.md
├── include/camera_publisher/
│   ├── camera_capture.hpp   # libcamera capture core (public header)
│   └── jpeg_encoder.hpp     # turbojpeg wrapper
└── src/
    ├── camera_capture.cpp        # the one place libcamera lives
    ├── camera_check.cpp          # executable 1 (no ROS)
    ├── camera_publisher_node.cpp # executable 2 (ROS 2)
    └── jpeg_encoder.cpp          # turbojpeg encode of the formats we negotiate
```

## Design decisions

### Shared capture core as a static library

Both executables link a `camera_capture` static library defined in
`CMakeLists.txt`. The libcamera lifecycle (the 19 numbered steps in
`camera_capture.cpp`) is implemented once and exercised identically by
the diagnostic tool and the production node. The class deliberately
exposes only:

* `open(config)` — bring the pipeline up to "configured, allocated".
* `start(cb)` — begin streaming, deliver frames to `cb`.
* `stop()` — idempotent teardown.
* `streamConfig()` — the negotiated `StreamConfiguration`.

The `Frame` struct passed to the callback owns no memory; its `data`
pointer is valid only for the duration of the callback. Anything the
callback wants to keep must be copied. This contract avoids both an
allocation per frame and the "did anyone unmap this buffer?" footgun.

### Pixel format negotiation, not assertion

We never tell libcamera what pixel format to produce. We pick the
geometry, hand the `Viewfinder` role to `generateConfiguration()`, and
read back whatever `validate()` settles on. This is the only behaviour
that is portable across the vc4-libcamera (Pi) pipeline handler, the
USB-webcam pipeline handler, and any other backend we might run against.

The negotiated format is logged loudly at startup and carried in
`Frame::pixel_format`. Downstream consumers (`camera_check` print,
`camera_publisher_node` raw publish, `JpegEncoder::encode`) all
inspect the format before doing anything with the bytes.

### Stride correctness

Every place that touches pixel bytes uses `StreamConfiguration::stride`
(carried into `Frame::stride`). Specifically:

* `camera_check` includes `stride=` in its per-frame line.
* `camera_publisher_node` sets `sensor_msgs::msg::Image::step = stride`.
* `JpegEncoder` passes `stride` as TurboJPEG's `pitch` parameter (or,
  for YUV420, as the Y plane stride, with chroma stride = stride / 2).

A common bug is to assume `width * bpp`; the Pi vc4 pipeline does pad
rows in some modes. The capture core comments call this out explicitly.

### Threading

libcamera dispatches `requestCompleted` on a thread it owns. Our
callback runs on that thread. The implications:

* `camera_check`'s callback writes to a `std::atomic<uint64_t>`
  frame counter and writes raw files to disk. Both are safe from
  any thread.
* `camera_publisher_node`'s callback calls
  `rclcpp::Publisher<...>::publish()`, which is documented thread-safe
  in rclcpp, and `RCLCPP_*_THROTTLE` macros, which take a `Clock` (also
  thread-safe).
* `CameraCapture` itself holds a `std::mutex` (`cb_mutex_`) around the
  user callback, so that `stop()` cannot drop the callback's captured
  state while an in-flight invocation is using it. This is the
  single lock in the hot path; it's only contended at shutdown.

The signal handlers in both `main()`s flip an `std::atomic<bool>` and
exit; they never touch libcamera or rclcpp objects directly. The
spin/wait loop sees the flag and does the actual teardown on the main
thread.

### Teardown race (fixed): `stopping_` flag

Earlier versions of `CameraCapture::stop()` called `camera_->stop()`
first and then flipped `streaming_ = false`. The libcamera completion
thread could land an `onRequestCompleted()` call between those two
steps: it would observe `streaming_ == true`, hit the requeue path,
and call `queueRequest()` on a pipeline that libcamera had already
moved to `Stopping`. libcamera rejects that with `-EACCES (-13)` and
logs:

```
Camera in Stopping state trying queueRequest()
queueRequest (recycle) failed: -13
```

We now carry a separate `std::atomic<bool> stopping_` set inside
`stop()` **before** `camera_->stop()`. `onRequestCompleted()` checks
it (with `memory_order_acquire`) just before the reuse/requeue path
and bails out if it's set. `start()` clears it on each fresh run so a
restart is clean. This atomic is the synchronization point between the
libcamera callback thread and `stop()`'s caller; no other lock needed
on that path.

### Why a `--mode` argv flag and not a launch parameter

Both modes are exposed as the same node. The spec asked for the mode
to be a command-line argument rather than a compile-time switch so it
is impossible to ship a binary that publishes the wrong type. Using a
plain argv flag (rather than a ROS parameter) keeps `camera_check` and
`camera_publisher_node` symmetrical and makes the `--mode` value
visible to anyone looking at `ps` / a systemd unit, not buried in a
parameter YAML.

### JPEG encoder scope

`JpegEncoder` deliberately knows about only the formats we have observed
or expect on the Pi 4 + IMX219 Viewfinder path:

* `RGB888`, `BGR888`, `XRGB8888`, `XBGR8888`, `RGBX8888`, `BGRX8888`,
  `RGBA8888`, `BGRA8888`, `ARGB8888`, `ABGR8888` — packed, via
  `tjCompress2`.
* `YUV420` (I420) — 3 contiguous planes, via `tjCompressFromYUVPlanes`.

Anything else returns `-ENOTSUP` and the node logs a throttled warning.
This was a deliberate choice over silently re-interpreting bytes.

## Things I am NOT sure about and want to confirm on the real hardware

These are the things the host build cannot validate. Each one is a
specific check to run on the Pi:

1. **Actual negotiated format on the Pi.** On the laptop's USB webcam
   the Viewfinder role produced MJPEG. The Pi vc4-libcamera pipeline
   normally produces `XRGB8888` or `YUV420` for Viewfinder, but I have
   not personally confirmed which one on this image. If the negotiated
   format prints anything else, `JpegEncoder` needs the new format
   added.

2. **Actual achieved frame rate at 1640x1232.** `FrameDurationLimits`
   asks for 30 fps; whether the sensor can sustain it depends on
   exposure and the pipeline. `camera_check`'s per-frame `dt=` is the
   ground truth — run it for a few seconds and read the prints.

3. **dmabuf mmap behaviour.** On x86 with simple-pipeline the dmabuf
   `mmap` Just Works. On the Pi the dmabuf fds come from the v4l2
   bcm2835-isp driver; reads through the mmap may or may not be
   cache-coherent. If `camera_check` raw dumps come back garbled but
   the frame count and timing look right, the dmabuf needs
   `DMA_BUF_IOCTL_SYNC` calls around CPU access. Adding this is a
   ~10 line change in `onRequestCompleted` if it turns out to be
   necessary.

4. **`bytesused` accuracy.** We currently use
   `metadata.planes()[0].bytesused` as the "actual filled bytes" for
   plane 0, falling back to `plane.length` if metadata is empty. The
   vc4 pipeline handler is known to fill `bytesused`; pipelines that
   do not will report 0, which we'd then misinterpret. If raw dumps
   on the Pi are empty (0 bytes), this is the first thing to check.

5. **Buffer pool exhaustion under slow subscriber.** With
   `bufferCount = 4`, four requests are always in flight. If the ROS
   publish path stalls (slow consumer + reliable QoS upstream), the
   re-queue path will eventually fall behind. We use the sensor_data
   profile (best-effort, depth 5) precisely to avoid this, but a real
   load test on the Pi is the only confirmation.

6. **Permissions.** libcamera on the Pi typically wants the user in
   the `video` group (and on some images also `render`). The Yocto
   image must include those groups for the dashboard service user.

## Active diagnostics in the code

### `publishJpeg` byte-count log (throttled)

`camera_publisher_node.cpp::publishJpeg` logs, at ~1 Hz, the JPEG
buffer size we are about to assign into `CompressedImage::data`
together with the negotiated `width`/`height`/`stride`/`format`. This
is **diagnostic-only** — no sizing logic was changed. It is in place
to confirm on real hardware that `buf.size()` matches the actual JPEG
byte count produced by `JpegEncoder::encode` (TurboJPEG's
`jpeg_size`). An independent source review (Codex) found the
JPEG/CompressedImage path correct: `data.size()` already equals
TurboJPEG's returned compressed length, and `out.assign` /
`data.assign` are doing the right thing. The log is here only to
verify that on-target reality matches that review.

### Unconfirmed: "sequence size exceeds remaining buffer"

A serialization symptom of this shape has been observed at runtime
but **could not be reproduced from the source** under review. The
working hypothesis is a **deployed-binary / source mismatch** (a
stale binary on the Pi, not a code defect). That is being tested
separately by full rebuild + redeploy; the throttled diagnostic above
will be the source of truth once the freshly built binary runs on
hardware. Do not "fix" the JPEG sizing code on the basis of this
symptom alone — the reviewed code is correct.

## Process / status

* Built: ✓ (colcon build clean on Ubuntu 24.04 / ROS 2 Jazzy).
* Self-tested on host with a USB webcam: ✓ (frames flow, clean SIGINT).
* End-to-end run on the Pi: NOT YET DONE — requires hardware access.
* Yocto recipe: out of scope of this PR.

## Changelog

* Teardown race **F1**: `CameraCapture` now carries a dedicated
  `std::atomic<bool> stopping_` flag set inside `stop()` before
  `camera_->stop()`. `onRequestCompleted()` checks it before the
  reuse/requeue path and exits early during teardown. Eliminates the
  "Camera in Stopping state trying queueRequest()" / "-EACCES (-13)"
  spam observed on SIGINT. See the "Teardown race" section above.
* Added throttled `publishJpeg` diagnostic line (size + geometry +
  format). Observation only; no sizing logic touched. See the
  "Active diagnostics" section above.
