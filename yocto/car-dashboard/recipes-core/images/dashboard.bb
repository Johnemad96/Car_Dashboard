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