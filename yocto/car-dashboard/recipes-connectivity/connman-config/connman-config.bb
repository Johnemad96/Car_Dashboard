# ==============================================================================
# ConnMan WiFi Auto-Connect Configuration
# ==============================================================================
# Installs pre-configured WiFi networks that ConnMan uses to automatically
# connect on boot. Networks are prioritized by signal strength and availability.
# ==============================================================================

SUMMARY = "ConnMan WiFi auto-connect configuration"
DESCRIPTION = "Pre-configured WiFi networks for automatic connection"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Source: wifi.config from files/ directory
# Contains WiFi SSIDs, passwords, and connection settings
SRC_URI = "file://wifi.config"

# Source directory - files are in UNPACKDIR (Yocto 5.0+)
# For older Yocto: use ${WORKDIR} instead
S = "${UNPACKDIR}"

do_install() {
    # Install WiFi configuration to /var/lib/connman/
    # ConnMan reads *.config files from this directory on startup
    install -d ${D}${localstatedir}/lib/connman/
    
    # Set 0600 permissions - only root can read WiFi passwords
    install -m 0600 ${S}/wifi.config ${D}${localstatedir}/lib/connman/wifi.config
}

# Explicitly list installed files (required for non-standard paths)
FILES:${PN} = "${localstatedir}/lib/connman/wifi.config"

# Runtime dependency - ensure ConnMan is installed
RDEPENDS:${PN} = "connman"

# enabling wifi bluetooth
# MACHINE_FEATURES:append = " wifi bluetooth"
# ==============================================================================
# Usage:
#   1. Add networks to files/wifi.config
#   2. Add to image: IMAGE_INSTALL:append = " connman-config"
#   3. Build: bitbake <image-name>
#
# Config format: https://git.kernel.org/pub/scm/network/connman/connman.git/tree/doc/config-format.txt
# ==============================================================================