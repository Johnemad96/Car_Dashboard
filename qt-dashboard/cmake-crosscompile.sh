#!/usr/bin/env bash
# Configure + build qt-dashboard via the Yocto SDK (cross-compile for the target).
# Requires setup-cross-env.sh to have been sourced first (sets OECORE_*).
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-rpi"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
rm -rf -- *

cmake "${SCRIPT_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${OECORE_NATIVE_SYSROOT}/usr/share/cmake/OEToolchainConfig.cmake" \
    -DQT_HOST_PATH="${HOME}/Qt/6.8.3/gcc_64" \
    -DQT_HOST_PATH_CMAKE_DIR="${HOME}/Qt/6.8.3/gcc_64/lib/cmake"

make -j"$(nproc)"
