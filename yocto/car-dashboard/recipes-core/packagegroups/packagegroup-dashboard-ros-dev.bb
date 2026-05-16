DESCRIPTION = "ROS 2 development packages needed by the car dashboard"
LICENSE = "MIT"

inherit packagegroup
inherit ros_distro_${ROS_DISTRO}

PACKAGES = "${PN}"

RDEPENDS:${PN} = "\
     ament-package \
     ament-cmake \
     ament-cmake-core \
     ament-cmake-export-dependencies \
     ament-cmake-export-include-directories \
     ament-cmake-export-libraries \
     ament-cmake-export-targets \
     ament-cmake-export-definitions \
     ament-cmake-export-link-flags \
     ament-cmake-export-interfaces \
     ament-cmake-include-directories \
     ament-cmake-libraries \
     ament-cmake-target-dependencies \
     ament-cmake-test \
     ament-cmake-version \
     ament-index-cpp \
     rcutils \
     rcpputils \
     rmw \
     rmw-implementation-cmake \
     rmw-implementation \
     rosidl-runtime-c \
     rosidl-runtime-cpp \
     rosidl-typesupport-c \
     rosidl-typesupport-cpp \
     rosidl-typesupport-interface \
     rosidl-cmake \
     rosidl-generator-c \
     rosidl-generator-cpp \
     rosidl-typesupport-fastrtps-c \
     rosidl-typesupport-fastrtps-cpp \
     rosidl-default-runtime \
     fastcdr \
     fastrtps \
     fastrtps-cmake-module \
     builtin-interfaces \
     rcl \
     rcl-interfaces \
     rcl-yaml-param-parser \
     rcl-logging-interface \
     rcl-logging-spdlog \
     libyaml-vendor \
     libstatistics-collector \
     spdlog-vendor \
     statistics-msgs \
     tracetools \
     std-msgs \
     sensor-msgs \
     geometry-msgs \
     rosgraph-msgs \
     rclcpp \
     ament-cmake-ros \
     ament-cmake-gen-version-h \
     ament-cmake-gmock \
     ament-cmake-gtest \
     ament-cmake-pytest \
     ament-cmake-python \
     foonathan-memory \
     foonathan-memory-staticdev \
     rosidl-adapter \
 "