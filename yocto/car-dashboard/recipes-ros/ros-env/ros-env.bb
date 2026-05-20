SUMMARY = "ROS 2 environment setup for login shells"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ros-env.sh"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/profile.d
    install -m 0755 ${WORKDIR}/ros-env.sh ${D}${sysconfdir}/profile.d/ros-env.sh
}

FILES:${PN} = "${sysconfdir}/profile.d/ros-env.sh"
