cmake_minimum_required(VERSION 3.16)
project(camera_publisher CXX)

# ---------------------------------------------------------------------------
# Toolchain / language standard
# ---------------------------------------------------------------------------
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Cross-compile support (Yocto SDK)
#
# When building via the Yocto SDK, ROS 2 is installed at
# $OECORE_TARGET_SYSROOT/opt/ros/humble, which is NOT on CMake's default
# search path. The OE toolchain file also restricts find_package() to the
# sysroot (CMAKE_FIND_ROOT_PATH), so simply exporting AMENT_PREFIX_PATH is
# not enough — the ROS prefix must be added to CMAKE_PREFIX_PATH explicitly.
# This block is inert for a native build (the env var is undefined there).
# ---------------------------------------------------------------------------
if(DEFINED ENV{OECORE_TARGET_SYSROOT})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{OECORE_TARGET_SYSROOT}/opt/ros/humble")
endif()

# ---------------------------------------------------------------------------
# ROS 2 / ament dependencies
# ---------------------------------------------------------------------------
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

# ---------------------------------------------------------------------------
# System dependencies via pkg-config
#   libcamera     -> the C++ API (libcamera::CameraManager etc.)
#   libturbojpeg  -> the simple TurboJPEG API for JPEG-encoding frames
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCAMERA REQUIRED IMPORTED_TARGET libcamera)
pkg_check_modules(LIBTURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)

include_directories(include)

# ---------------------------------------------------------------------------
# Shared capture core
#
# Both executables drive the same libcamera capture pipeline. We build that
# pipeline as a small static library so the lifecycle code is written once
# and exercised identically by camera_check and camera_publisher.
# ---------------------------------------------------------------------------
add_library(camera_capture STATIC
  src/camera_capture.cpp
)
target_include_directories(camera_capture PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(camera_capture PUBLIC
  PkgConfig::LIBCAMERA
)

# ---------------------------------------------------------------------------
# Executable 1: camera_check  (no ROS dependency)
# ---------------------------------------------------------------------------
add_executable(camera_check src/camera_check.cpp)
target_link_libraries(camera_check PRIVATE camera_capture)

# ---------------------------------------------------------------------------
# Executable 2: camera_publisher  (ROS 2 node)
# ---------------------------------------------------------------------------
add_executable(camera_publisher_node
  src/camera_publisher_node.cpp
  src/jpeg_encoder.cpp
)
# ament_target_dependencies uses the plain target_link_libraries form
# internally, so we MUST stay on the plain form here too (mixing
# keyword PRIVATE and plain forms on the same target is an error).
target_link_libraries(camera_publisher_node
  camera_capture
  PkgConfig::LIBTURBOJPEG
)
ament_target_dependencies(camera_publisher_node
  rclcpp
  sensor_msgs
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS
  camera_check
  camera_publisher_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
