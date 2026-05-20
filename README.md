# Car dashboard

A Raspberry Pi 4 in-vehicle dashboard prototype built on a custom Yocto Linux image, with a Qt 6 / QML UI driven by ROS 2 and a live MIPI CSI-2 camera pipeline from a Pi Camera Module v2.

## Prototype scope

This is a working preliminary prototype intended as a portfolio piece. The end-to-end data path -- IMX219 sensor through libcamera, ROS 2 publication, network transport, and Qt-side decode and display on the Pi -- is verified on real hardware. UI polish, simulator integration for vehicle-state topics, and additional features are planned and tracked in the limitations section below.

## Demo

[![Dashboard demo](https://img.youtube.com/vi/EGEhZwWESzY/maxresdefault.jpg)](https://youtu.be/EGEhZwWESzY)

Recording: live IMX219 frames captured on the Pi via libcamera, published as JPEG over ROS 2, and rendered in the Qt/QML dashboard alongside the speed and RPM gauges (both currently driven by placeholder data). Click the thumbnail to watch on YouTube.

## What's in the repo

**`yocto/`** -- a custom Yocto Scarthgap 5.0 LTS image for the Pi 4. The first-party project layer `car-dashboard/` contains the image recipe (`dashboard.bb`), a `packagegroup-dashboard-ros-dev` for ROS SDK generation, and a `libcamera_%.bbappend` that pins the pipeline handler set. Upstream layers (`poky`, `meta-openembedded`, `meta-raspberrypi`, `meta-ros`, `meta-qt6`) are tracked as git submodules and pinned to the matching Scarthgap branches.

**`rpi4-camera-imx219/`** -- the Pi 4 + IMX219 camera stack. `camera-publisher/` is a ROS 2 package built directly on the libcamera C++ API. It ships two executables that share a single capture core: `camera_check` (no-ROS isolation tool that prints per-frame stats and optionally dumps raw frames) and `camera_publisher_node` (ROS 2 node with a runtime `--mode raw|jpeg` flag selecting between `sensor_msgs/Image` and `sensor_msgs/CompressedImage` output).

**`qt-dashboard/`** -- a Qt 6.8 / QML dashboard application. It runs on the Pi under `eglfs` directly on the framebuffer and uses `rclcpp` to subscribe to vehicle-state and camera topics. Vehicle-state topics will be fed from a CARLA simulator rosbag replayed from a laptop on the same DDS domain. Camera subscription uses `rclcpp::SensorDataQoS()` to match the publisher; the rclcpp callback marshals each frame to the Qt GUI thread via `QMetaObject::invokeMethod` with `Qt::QueuedConnection` before touching any QObject state.

## Pipeline overview

The data path from photons to pixels on the dashboard runs: IMX219 sensor -> MIPI CSI-2 serial link -> Unicam (the BCM2711 CSI receiver block on the Pi 4) -> libcamera with the rpi/vc4 pipeline handler driving the bcm2835 ISP -> JPEG encode via libjpeg-turbo on the CPU -> ROS 2 publisher (`sensor_msgs/CompressedImage`, sensor_data QoS) -> DDS transport (shared memory locally, UDP across the network) -> Qt dashboard subscriber on the Pi -> JPEG decode into a `QImage` -> display in QML via a `QQuickImageProvider`.

## What works on hardware

- Custom Yocto Scarthgap 5.0 LTS image boots on the Pi 4 with the Pi camera enabled via the modern Unicam / libcamera stack, not the legacy MMAL / `start_x=1` firmware path.
- `camera_check` captures frames from the IMX219 via libcamera on the Pi and reports per-frame format, stride, and inter-frame delta.
- `camera_publisher_node --mode jpeg` publishes valid `sensor_msgs/CompressedImage` frames at sustained ~15 fps measured via `ros2 topic hz`.
- The Qt / QML dashboard, cross-compiled via the Yocto SDK, runs under `eglfs` on the Pi, subscribes to the camera topic, decodes the JPEG, and displays it.
- End-to-end live capture from the physical camera to the dashboard display is verified on real hardware (see Demo).

## Known limitations and planned work

- One physical camera (Pi Camera) is integrated; two CARLA-simulated camera streams are designed but not yet wired in.
- Dashboard UI is functional but not visually polished; layout responsiveness across panel toggles and an `eglfs`-safe `ESC` / quit handler are planned.
- JPEG decoding currently runs on the Qt GUI thread. This is acceptable at the present resolution and rate, but moving the decode off-thread is planned.
- No frame-drop backpressure beyond the best-effort QoS contract on the camera topic.
- The camera topic name is currently hardcoded.
- A Yocto recipe for `camera-publisher` is not yet written; the binary is cross-compiled via the SDK and copied to the device.


## Hardware and software versions

| Component             | Version                            |
| --------------------- | ---------------------------------- |
| Hardware              | Raspberry Pi 4 Model B             |
| Camera                | Pi Camera Module v2 (Sony IMX219)  |
| Yocto                 | Scarthgap 5.0 LTS                  |
| ROS 2 (target)        | Humble                             |
| ROS 2 (dev laptop)    | Jazzy                              |
| Host OS (dev laptop)  | Ubuntu 24.04                       |
| Qt                    | 6.8.3                              |

## Documentation

Detailed debugging and design notes live under `docs/` and are intended to be read in numeric order; each file covers one slice of the bring-up so a reader can follow the chain from image build to running pipeline.

- [`docs/01-yocto-qt-ros-cross-compile.md`](docs/01-yocto-qt-ros-cross-compile.md)
- [`docs/02-camera-pipeline-bringup.md`](docs/02-camera-pipeline-bringup.md)
- [`docs/03-camera-publisher-cross-compile.md`](docs/03-camera-publisher-cross-compile.md)
- [`docs/04-pipeline-layers-explained.md`](docs/04-pipeline-layers-explained.md)
