# Car Dashboard

A Raspberry Pi 4 in-vehicle dashboard prototype. **Work in progress** — none
of the components below are production-ready; this repository tracks the
day-to-day development of the system end-to-end (Yocto image, camera
capture, and Qt UI) in a single monorepo so changes can be iterated
atomically.

## Components

- **`yocto/`** — the Yocto build for the target image (poky scarthgap +
  meta-openembedded, meta-raspberrypi, meta-ros, meta-qt6). Contains the
  custom `meta-car-dashboard` layer with the image recipe, ROS 2 Humble +
  Qt 6 runtime, and wifi/connman setup. Upstream layers (poky, meta-oe,
  meta-rpi, meta-ros, meta-qt6) are git submodules; everything under
  `yocto/car-dashboard/` is first-party.
- **`rpi4-camera-imx219/`** — Pi-4 + IMX219 camera capture stack.
  `camera-publisher/` is a ROS 2 package (libcamera + libjpeg-turbo) that
  exposes the IMX219 sensor as a raw `sensor_msgs/Image` or compressed
  `sensor_msgs/CompressedImage` topic, plus a no-ROS `camera_check` binary
  for isolating the capture path.
- **`qt-dashboard/`** — Qt 6 / QML dashboard UI. Subscribes to the speed
  topic and the camera stream from `rpi4-camera-imx219/` and renders the
  speedometer, tachometer, and live camera panel. Builds natively on the
  developer laptop (Qt 6.8 + ROS 2 Jazzy) and cross-compiles for the Pi
  via the Yocto SDK.
