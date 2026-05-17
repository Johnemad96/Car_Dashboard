// SPDX-License-Identifier: MIT
//
// jpeg_encoder.hpp
//
// Thin wrapper around the TurboJPEG ("turbojpeg.h") API. Handles the
// handful of pixel formats we actually negotiate out of libcamera on
// the Pi 4 + IMX219 path. If a format is delivered that we don't know
// how to encode, encode() returns -ENOTSUP and the caller must skip
// the frame and log loudly.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <libcamera/pixel_format.h>

namespace camera_publisher {

class JpegEncoder {
 public:
  JpegEncoder();
  ~JpegEncoder();

  JpegEncoder(const JpegEncoder &) = delete;
  JpegEncoder &operator=(const JpegEncoder &) = delete;

  // Encodes src (one stream frame from libcamera, of the given pixel
  // format / dims / stride) into a JPEG. The encoded bytes are written
  // into out (cleared on entry). quality is the standard 1..100 JPEG
  // quality factor.
  //
  // Returns 0 on success, -ENOTSUP for an unsupported pixel_format,
  // -EIO for an encode failure (TurboJPEG error logged to stderr).
  int encode(const std::uint8_t *src,
             std::size_t src_len,
             std::uint32_t width,
             std::uint32_t height,
             std::uint32_t stride,
             libcamera::PixelFormat pixel_format,
             int quality,
             std::vector<std::uint8_t> &out);

 private:
  void *tj_handle_ = nullptr;  // tjhandle (opaque)
};

}  // namespace camera_publisher
