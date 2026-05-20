SUMMARY = "Core image with extras for dashboard"
LICENSE = "MIT"
inherit core-image

# make sure X11 is enabled in the distro features (Sato uses X11)
# DISTRO_FEATURES:append = " x11"

# IMAGE_FEATURES += "x11-base x11-sato"

# reuse Sato content via packagegroup

IMAGE_INSTALL += " \
    linux-firmware-rpidistro-bcm43456 \
    iw \
    wireless-regdb-static \
    busybox-udhcpc \
    connman \
    connman-client \
    connman-config \
    avahi-daemon \
    avahi-autoipd \
"
# Enable SSH daemon to start on boot
EXTRA_IMAGE_FEATURES += " ssh-server-openssh"

#IMAGE_INSTALL:append = " wireless-regdb-static"

# In your local.conf or image recipe
IMAGE_INSTALL:append = " rng-tools"

#deps for ros2
IMAGE_INSTALL:append = " packagegroup-ros2-demos"

IMAGE_INSTALL:append = " ros-env"

#QT
# Qt6 runtime libraries needed for QML dashboard rendering
# qtbase: core Qt library (event loop, networking, basic types)
# qtdeclarative: QML engine and QtQuick - required for .qml files to run
# qtquickcontrols2: QtQuick controls used in QML UI components
# mesa: open source OpenGL implementation - provides OpenGL ES on the Pi
# mesa-driver-vc4: the actual GPU driver for Pi's VideoCore IV/V GPU
#                  without this, OpenGL calls have nothing to talk to
#                  and Qt's eglfs platform plugin cannot render anything
IMAGE_INSTALL:append = " qtbase qtdeclarative mesa"
IMAGE_INSTALL:append = " qtdeclarative-tools"
IMAGE_INSTALL:append = " qtbase-plugins"

# fonts for qt
IMAGE_INSTALL:append = " ttf-dejavu-sans ttf-dejavu-sans-mono fontconfig"

# IMAGE_INSTALL:append = " qtdeclarative-dev qtbase-staticdev"
TOOLCHAIN_TARGET_TASK:append = " qtbase-staticdev qtdeclarative-staticdev"

# EXTRA_OECMAKE:append:pn-qtbase = " -DQT_BUILD_EXAMPLES=OFF"
# TOOLCHAIN_TARGET_TASK:append = " packagegroup-ros2-demos-dev"
# TOOLCHAIN_TARGET_TASK:append = " packagegroup-ros-world-dev"
TOOLCHAIN_TARGET_TASK:append = " packagegroup-dashboard-ros-dev"

#TOOLCHAIN_HOST_TASK:append = " nativesdk-python3-ament-package"

IMAGE_INSTALL:append = " v4l-utils libcamera libturbojpeg sensor-msgs gstreamer1.0 gstreamer1.0-plugins-bad gstreamer1.0-plugins-good"
KERNEL_MODULE_AUTOLOAD:append = " imx219 bcm2835-unicam bcm2835-isp"
