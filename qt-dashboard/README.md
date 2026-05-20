# qt-dashboard

The Qt 6 / QML in-vehicle dashboard UI. Runs on the Raspberry Pi 4
under the dashboard Yocto image, subscribes to ROS 2 topics for vehicle
state and the camera stream, and renders the result on the attached
800x480 display.

Target ROS 2 distro is **Humble** on the Pi/Yocto image. The code is
also verified building and running on **Jazzy** + Qt 6.8 on Ubuntu
24.04 for native development.

---

## What it shows

Three side-by-side panels in a `RowLayout` (`Main.qml`):

| Panel  | Source                                                                 |
| ------ | ---------------------------------------------------------------------- |
| RPM    | derived from speed (`speed * 35`) -- placeholder, not a real RPM source |
| Speed  | `/speed` (`std_msgs/Float64`), clamped 0..200 -- placeholder           |
| Camera | `/camera/image_compressed` (JPEG, sensor_data QoS) -- live from the Pi camera |

The `/speed` topic is currently driven by dummy data (any external
publisher, e.g. `ros2 topic pub`); RPM is computed from it as a
placeholder. Both will be replaced with real signals from the CARLA
simulator rosbag (vehicle speed, engine RPM, gear, etc.) in a later
iteration. The camera panel is already wired to the real hardware
path via `camera_publisher_node --mode jpeg`.

Keyboard:

* **Enter** -- toggle the camera panel between full-width and hidden,
  with an animated reflow of the gauges.
* **Esc** -- quit.

The camera panel publishes a little blinking border so it's easy to
tell from across the cabin whether frames are still arriving.

---

## Architecture

```
+--------------------------+
|   QML (Main.qml)         |   gauges + camera Image
+-----------+--------------+
            |  Q_PROPERTY + image://rpicamera/...
+-----------v--------------+
|   DashboardBackend       |   QObject, lives on GUI thread
|   (dashboard_backend.*)  |   owns speed / rpm / cameraFrame state
+-----------+--------------+
            |  QMetaObject::invokeMethod (QueuedConnection)
+-----------v--------------+
|   ROS 2 executor thread  |   SingleThreadedExecutor::spin()
|   - VehicleDataNode      |   /speed              -> setSpeed()
|   - RpiCameraStreamNode  |   /camera/...         -> setCameraFrame()
+--------------------------+
```

Two things are worth knowing:

1. **The ROS executor runs on a dedicated `std::thread`** owned by
   `DashboardBackend`. Subscription callbacks run on that thread, so
   every cross into the Qt object goes through
   `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to land back
   on the GUI thread before touching Q_PROPERTYs or emitting signals.
2. **Camera frames don't flow through Q_PROPERTY.** The image bytes are
   too large for property bindings, so `DashboardBackend` decodes the
   JPEG into a `QImage` and exposes only a monotonic
   `cameraFrameSequence` int. QML uses that sequence number as a
   cache-busting suffix on `image://rpicamera/frame/<seq>`, and the
   actual `QImage` is served by `RpiCameraImageProvider`.

---

## Files

| File                              | Role                                                        |
| --------------------------------- | ----------------------------------------------------------- |
| `main.cpp`                        | Entry point. Inits rclcpp, builds engine, wires image provider. |
| `Main.qml`                        | UI layout: RPM canvas, speed canvas, camera panel.          |
| `dashboard_backend.{h,cpp}`       | QObject bridge. Owns state + the ROS executor thread.       |
| `vehicledatanode.{h,cpp}`         | rclcpp::Node subscribing to `/speed`.                       |
| `rpicamerastreamnode.{h,cpp}`     | rclcpp::Node subscribing to `/camera/image_compressed`.     |
| `rpicameraimageprovider.{h,cpp}`  | `QQuickImageProvider` serving the latest decoded frame.     |
| `CMakeLists.txt`                  | Build config (Qt 6.8 + rclcpp + std_msgs + sensor_msgs).    |
| `setup-cross-env.sh`              | Source the Yocto SDK env + extend `PYTHONPATH`/`AMENT_PREFIX_PATH`. |
| `cmake-crosscompile.sh`           | Configure + build into `build-rpi/` using the OE toolchain. |

---

## Topics consumed

| Topic                      | Type                              | QoS                 |
| -------------------------- | --------------------------------- | ------------------- |
| `/speed`                   | `std_msgs/msg/Float64`            | depth 10, reliable  |
| `/camera/image_compressed` | `sensor_msgs/msg/CompressedImage` | best-effort, depth 10 |

The camera topic name + type matches what `camera_publisher_node
--mode jpeg` publishes (see
`../rpi4-camera-imx219/camera-publisher/README.md`).

---

## Building

### Native (laptop, for UI iteration)

```bash
sudo apt install qt6-base-dev qt6-declarative-dev
source /opt/ros/jazzy/setup.bash

mkdir -p build && cd build
cmake ..
make -j"$(nproc)"
./appqtdashboard
```

The native build talks to whatever ROS publishers are reachable on the
same domain. Easiest smoke test is to `ros2 topic pub` a fake
`/speed`:

```bash
ros2 topic pub /speed std_msgs/Float64 "{data: 80.0}" -r 1
```

### Cross-compile for the Pi (Yocto SDK)

```bash
source ./setup-cross-env.sh        # sources the OE SDK + sets ROS paths
./cmake-crosscompile.sh            # configures + builds into build-rpi/
scp build-rpi/appqtdashboard pi:/tmp/
```

`cmake-crosscompile.sh` passes `QT_HOST_PATH` to point at the host's
Qt 6.8 install -- Qt's cross-build needs a matching host Qt for tools
like `moc` / `qmlimportscanner`. `CMakeLists.txt` adds
`$OECORE_TARGET_SYSROOT/opt/ros/humble` to `CMAKE_PREFIX_PATH` so
`find_package(rclcpp)` resolves against the SDK's ROS install.

`build-rpi/` is gitignored.

---

## Display + EGL notes

The Pi runs the app fullscreen via `eglfs` on KMS. The Yocto image
enables that by setting on `qtbase`:

```
PACKAGECONFIG:append:pn-qtbase = " eglfs gles2 kms gbm"
PACKAGECONFIG:remove:pn-qtbase = "gl"
```

so QML renders against GLES2 directly on the framebuffer with no X11 /
Wayland in the picture. If you ever see `Could not initialize EGL
display`, the `opengl` DISTRO_FEATURE or the `v3d` mesa packageconfig
is the first thing to check (`yocto/build-rpi/conf/local.conf`).
