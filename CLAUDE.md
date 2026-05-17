# CLAUDE.md — project working notes

This file is a high-level log of what is being built, what has been
achieved, what is still open, and the design approaches that have been
agreed on. It is intended to give a future session (or a teammate)
enough context to continue without re-deriving everything from `git log`.

Detailed package-internal documentation lives next to the code it
describes (e.g. `camera-publisher/README.md`, `camera-publisher/NOTES.md`).

---

## Project: car-dashboard

A Raspberry Pi 4 in-vehicle dashboard. The Pi runs a custom Yocto image
(branch `scarthgap`, ROS 2 Humble + Qt 6 + ROS-aware multimedia).

### Top-level layout

```
Car_Dashboard/
├── yocto/                          # the Yocto image (poky + meta layers, dashboard image recipe)
├── camera-publisher/               # ROS 2 package: libcamera capture + raw/JPEG publisher
└── qt/                             # (future) Qt 6 dashboard application
```

`camera-publisher/` is a first-party ROS 2 package living in-repo. It
is **not** a git submodule -- it is developed alongside the Yocto
image so changes can be iterated atomically.

---

## Goals

* P0: stream live IMX219 camera frames from the Pi to the dashboard
  application over ROS 2, with reasonable latency and frame rate.
* P0: be able to debug the capture path in isolation (no ROS) so that
  when the streaming pipeline misbehaves, the search space is
  narrowable to "camera vs network vs encoding vs subscriber".
* P1: portable across ROS 2 distros — Pi/Yocto image uses Humble,
  laptop dev uses Jazzy. Source code must not depend on distro-specific
  APIs.
* P1: clean teardown on SIGINT everywhere. Leaving a camera acquired
  on the Pi requires a reboot to recover.

## Non-goals (for now)

* Image processing (debayering / colour correction / white balance).
* Multi-camera support.
* Hardware-accelerated encode (we use libjpeg-turbo on the CPU; the
  Pi 4 can sustain 1080p30 JPEG on a single core).

---

## Current approach (and why)

### libcamera C++ API (not GStreamer, not v4l2, not the `cam` tool)

* GStreamer would couple us to a particular ROS bridge component and
  hide the pipeline behind a string DSL.
* Raw v4l2 ioctls would force us to write our own pipeline handler
  for the bcm2835-isp, which is exactly what libcamera already is.
* The `cam` tool is a debug binary, not a library.

The libcamera C++ API gives us the actual abstractions (Camera,
Stream, Request, FrameBuffer) at the right level. The capture core
is heavily commented because the asynchronous request/completion
model is the part that most often trips people up.

### Two binaries sharing one capture core

The shared `camera_capture` static library means the libcamera
lifecycle is implemented once and exercised by both:

* `camera_check` — no-ROS isolation tool that prints per-frame stats
  and dumps raw frames.
* `camera_publisher_node` — ROS 2 node publishing either raw
  `sensor_msgs/Image` or JPEG `sensor_msgs/CompressedImage`.

Mode is a required command-line argument so it's impossible to ship a
binary that emits the wrong type, and `ros2 topic list` shows the mode
unambiguously (different topic AND different message type per mode).

### sensor_data QoS for video

Best-effort, depth 5. Dropping a stale frame to deliver a fresh one is
the correct trade-off for live video.

---

## Milestones

| ID  | Milestone                                                                 | Status |
| --- | ------------------------------------------------------------------------- | ------ |
| M1  | Yocto image boots on Pi 4 (scarthgap)                                      | ✅ done |
| M2  | ROS 2 Humble in the image                                                  | ✅ done |
| M3  | Qt 6 runtime in the image                                                  | ✅ done |
| M4  | `camera-publisher` package: builds clean against ROS 2 Jazzy on host       | ✅ done |
| M5  | `camera_check` runs end-to-end on Pi against IMX219, sustains target fps   | ⏳ pending hardware test |
| M6  | `camera_publisher_node --mode raw` publishes on Pi, subscriber sees stream | ⏳ pending hardware test |
| M7  | `camera_publisher_node --mode jpeg` publishes on Pi, subscriber sees stream | ⏳ pending hardware test |
| M8  | Yocto recipe for `camera-publisher` in `yocto/car-dashboard/`              | ⏳ pending |
| M9  | Qt dashboard subscribes to image stream and renders                       | ⏳ future |

---

## What was just added (this change)

* `camera-publisher/` — the ROS 2 package described above. Builds
  clean on host with `colcon build`.
* `.gitignore` — added rules for raw frame dumps, the `scp_pcis/`
  scratch dir, and the colcon `build/install/log/` directories.

## What is still left

1. **Hardware bring-up on the Pi.** Run `camera_check` first, confirm
   the negotiated format is one of the formats `JpegEncoder` knows
   about. If not, extend the encoder. See
   `camera-publisher/NOTES.md` for the explicit "verify on real
   hardware" list.
2. **Yocto recipe for `camera-publisher`.** A new
   `recipes-multimedia/camera-publisher_0.1.bb` under
   `yocto/car-dashboard/` that builds the package via the
   `ros_ament_cmake` bbclass and adds it to the dashboard image's
   `IMAGE_INSTALL`. Out of scope of this PR.
3. **Qt subscriber.** A QML / C++ subscriber that consumes
   `/camera/image_compressed` and renders it. Out of scope of this PR.

---

## Conventions worth knowing for future work

* All ROS packages live in-repo (`camera-publisher/`, future siblings)
  unless there is a strong reason to vendor them via Yocto. This
  keeps day-to-day iteration in one git history.
* libcamera + libjpeg-turbo are exposed via pkg-config in CMake. New
  ROS packages should follow the same pattern rather than wiring up
  per-distro `find_package` recipes.
* Per-frame log lines include format + stride + dt so achieved frame
  rate is visible without an external tool.
