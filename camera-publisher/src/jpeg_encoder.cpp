// SPDX-License-Identifier: MIT
//
// jpeg_encoder.cpp
//
// We deliberately handle a small, named set of pixel formats here.
// libcamera CAN deliver many more, but on a Pi 4 + IMX219 with a
// Viewfinder role the negotiated format is almost always one of:
//
//   - BGR888 / RGB888              (24-bit packed)
//   - XBGR8888 / XRGB8888          (32-bit packed, X channel is don't-care)
//   - YUV420                       (3 contiguous planes Y, U, V)
//
// All three appear below. Anything else is rejected with -ENOTSUP so
// the caller can log a loud "we received format X, you need to teach
// the encoder about it" message rather than silently producing garbage.
//
#include "camera_publisher/jpeg_encoder.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <turbojpeg.h>
#include <libcamera/formats.h>

namespace camera_publisher {

namespace {

// Map a libcamera packed pixel format to a TurboJPEG TJPF_* constant
// and the byte size of one source pixel. Returns true on a known
// packed format.
bool mapPacked(libcamera::PixelFormat pf, int &tjpf, int &bpp) {
  using namespace libcamera::formats;
  if (pf == RGB888)   { tjpf = TJPF_RGB;  bpp = 3; return true; }
  if (pf == BGR888)   { tjpf = TJPF_BGR;  bpp = 3; return true; }
  if (pf == XRGB8888) { tjpf = TJPF_XRGB; bpp = 4; return true; }
  if (pf == XBGR8888) { tjpf = TJPF_XBGR; bpp = 4; return true; }
  if (pf == RGBX8888) { tjpf = TJPF_RGBX; bpp = 4; return true; }
  if (pf == BGRX8888) { tjpf = TJPF_BGRX; bpp = 4; return true; }
  if (pf == RGBA8888) { tjpf = TJPF_RGBA; bpp = 4; return true; }
  if (pf == BGRA8888) { tjpf = TJPF_BGRA; bpp = 4; return true; }
  if (pf == ARGB8888) { tjpf = TJPF_ARGB; bpp = 4; return true; }
  if (pf == ABGR8888) { tjpf = TJPF_ABGR; bpp = 4; return true; }
  return false;
}

}  // namespace

JpegEncoder::JpegEncoder() {
  tj_handle_ = tjInitCompress();
  if (!tj_handle_) {
    std::fprintf(stderr, "[jpeg_encoder] tjInitCompress failed: %s\n",
                 tjGetErrorStr());
  }
}

JpegEncoder::~JpegEncoder() {
  if (tj_handle_) {
    tjDestroy(tj_handle_);
    tj_handle_ = nullptr;
  }
}

int JpegEncoder::encode(const std::uint8_t *src,
                        std::size_t src_len,
                        std::uint32_t width,
                        std::uint32_t height,
                        std::uint32_t stride,
                        libcamera::PixelFormat pixel_format,
                        int quality,
                        std::vector<std::uint8_t> &out) {
  out.clear();
  if (!tj_handle_) return -EIO;
  if (!src || width == 0 || height == 0) return -EINVAL;
  if (quality < 1) quality = 1;
  if (quality > 100) quality = 100;

  // Let TurboJPEG allocate the output buffer; we copy out at the end.
  // (tjAlloc/tjFree would let us pre-size, but the realloc path is
  // fine here and frees us from caring about a maximum size.)
  unsigned char  *jpeg_buf  = nullptr;
  unsigned long   jpeg_size = 0;

  int tjpf = 0, bpp = 0;
  if (mapPacked(pixel_format, tjpf, bpp)) {
    // Packed branch. tjCompress2 takes a "pitch" (bytes per row);
    // libcamera's stride is exactly that. Never substitute width*bpp.
    if (src_len < static_cast<std::size_t>(stride) * height) {
      std::fprintf(stderr,
                   "[jpeg_encoder] short buffer for %ux%u stride=%u: have %zu need %u\n",
                   width, height, stride, src_len,
                   stride * height);
      return -EINVAL;
    }
    int rc = tjCompress2(static_cast<tjhandle>(tj_handle_),
                         src,
                         static_cast<int>(width),
                         static_cast<int>(stride),
                         static_cast<int>(height),
                         tjpf,
                         &jpeg_buf, &jpeg_size,
                         TJSAMP_420,
                         quality,
                         0);
    if (rc != 0) {
      std::fprintf(stderr, "[jpeg_encoder] tjCompress2 failed: %s\n",
                   tjGetErrorStr2(static_cast<tjhandle>(tj_handle_)));
      if (jpeg_buf) tjFree(jpeg_buf);
      return -EIO;
    }
  } else if (pixel_format == libcamera::formats::YUV420) {
    // 3-plane I420: Y full-size, then U and V at half-resolution.
    // libcamera reports the Y stride; the chroma stride is half by
    // definition for I420. We compute plane base pointers within the
    // single contiguous mapping that libcamera handed us.
    const int y_stride  = static_cast<int>(stride);
    const int uv_stride = y_stride / 2;
    const std::size_t y_plane_size  = static_cast<std::size_t>(y_stride)  * height;
    const std::size_t uv_plane_size = static_cast<std::size_t>(uv_stride) * (height / 2);
    if (src_len < y_plane_size + 2 * uv_plane_size) {
      std::fprintf(stderr,
                   "[jpeg_encoder] short YUV420 buffer: have %zu need %zu\n",
                   src_len, y_plane_size + 2 * uv_plane_size);
      return -EINVAL;
    }
    const unsigned char *planes[3] = {
        src,
        src + y_plane_size,
        src + y_plane_size + uv_plane_size,
    };
    const int strides[3] = { y_stride, uv_stride, uv_stride };
    int rc = tjCompressFromYUVPlanes(static_cast<tjhandle>(tj_handle_),
                                     planes,
                                     static_cast<int>(width),
                                     strides,
                                     static_cast<int>(height),
                                     TJSAMP_420,
                                     &jpeg_buf, &jpeg_size,
                                     quality,
                                     0);
    if (rc != 0) {
      std::fprintf(stderr,
                   "[jpeg_encoder] tjCompressFromYUVPlanes failed: %s\n",
                   tjGetErrorStr2(static_cast<tjhandle>(tj_handle_)));
      if (jpeg_buf) tjFree(jpeg_buf);
      return -EIO;
    }
  } else {
    std::fprintf(stderr,
                 "[jpeg_encoder] unsupported pixel format %s -- extend "
                 "mapPacked()/YUV branch in jpeg_encoder.cpp\n",
                 pixel_format.toString().c_str());
    return -ENOTSUP;
  }

  out.assign(jpeg_buf, jpeg_buf + jpeg_size);
  tjFree(jpeg_buf);
  return 0;
}

}  // namespace camera_publisher
