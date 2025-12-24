SUMMARY = "Core image with extras for dashboard"
LICENSE = "MIT"
inherit core-image

# make sure X11 is enabled in the distro features (Sato uses X11)
# DISTRO_FEATURES:append = " x11"

# IMAGE_FEATURES += "x11-base x11-sato"

# reuse Sato content via packagegroup

IMAGE_INSTALL += " packagegroup-core-boot"

IMAGE_INSTALL += " \
    packagegroup-base-extended \
    linux-firmware-rpidistro-bcm43456 \
    openssh \
    connman \
    connman-client \
    connman-config \
    wpa-supplicant \
    iw \
    avahi-daemon \
    avahi-autoipd \
"
# Enable SSH daemon to start on boot
EXTRA_IMAGE_FEATURES += " ssh-server-openssh"