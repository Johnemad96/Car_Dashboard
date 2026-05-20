// SPDX-License-Identifier: MIT
//
// camera_capture.cpp
//
// Implementation of the libcamera-based capture core. This file is
// intentionally heavy on commentary because libcamera's lifecycle is
// not obvious from a quick reading of its headers and the official
// examples are scattered. The intent is that a reader new to libcamera
// can follow the sequence end-to-end here.
//
// High-level lifecycle:
//
//   1. CameraManager::start()       -- spin up the pipeline-handler
//                                      machinery; enumerate cameras.
//   2. cm->cameras() / cm->get()    -- pick the camera we want.
//   3. camera->acquire()            -- claim exclusive use.
//   4. camera->generateConfiguration({StreamRole::Viewfinder})
//                                   -- ask the pipeline handler what
//                                      a sensible default stream looks
//                                      like for our use case.
//   5. mutate StreamConfiguration   -- override width/height/etc.
//   6. config->validate()           -- libcamera adjusts to a legal
//                                      combination of (format,size,stride).
//                                      You MUST re-read the config after.
//   7. camera->configure(config)    -- commit.
//   8. FrameBufferAllocator::allocate(stream)
//                                   -- libcamera produces DMA-capable
//                                      buffers (typically dmabuf fds).
//   9. mmap each plane fd           -- so we can read pixels from CPU.
//  10. createRequest() + addBuffer()
//                                   -- one Request per buffer in the pool.
//  11. camera->requestCompleted.connect(...)
//                                   -- subscribe to the async completion
//                                      signal. libcamera dispatches this
//                                      on its own thread.
//  12. camera->start()              -- pipeline starts running.
//  13. queueRequest(req) for each   -- prime the pump so the pipeline
//                                      has something to fill.
//
//   ...frames arrive asynchronously via requestCompleted...
//
//  14. camera->stop()               -- halt the pipeline.
//  15. requests_.clear()            -- destroy outstanding Request objects.
//  16. munmap each plane            -- release CPU mappings.
//  17. allocator->free(stream)      -- give buffers back.
//  18. camera->release()            -- relinquish exclusivity.
//  19. CameraManager::stop()        -- happens implicitly on dtor.
//
// Every numbered step above maps to a labelled block below.
//
#include "camera_publisher/camera_capture.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/formats.h>

namespace camera_publisher {

namespace {

// Small helper: log a libcamera return code with a human label.
void logErr(const char *what, int rc) {
  std::fprintf(stderr, "[camera_capture] %s failed: %d (%s)\n",
               what, rc, std::strerror(rc < 0 ? -rc : rc));
}

}  // namespace

CameraCapture::CameraCapture() = default;

CameraCapture::~CameraCapture() {
  // Best-effort teardown if the caller forgot.
  stop();
}

int CameraCapture::open(const CaptureConfig &cfg) {
  // ---- Step 1: CameraManager up ----------------------------------------
  // CameraManager is the entry point. It loads pipeline handlers
  // (vc4, rkisp1, simple, etc.) and enumerates the cameras that any of
  // them can drive. It must be start()'d before any cameras can be
  // listed, and it must outlive every Camera handle obtained from it.
  manager_ = std::make_unique<libcamera::CameraManager>();
  int rc = manager_->start();
  if (rc) {
    logErr("CameraManager::start", rc);
    manager_.reset();
    return rc;
  }

  // ---- Step 2: pick a camera -------------------------------------------
  // For this project there is only one camera on the bus (the Pi Camera
  // module). We just take the first one. A real product would match by
  // id() or by sensor model from properties::Model.
  auto cameras = manager_->cameras();
  if (cameras.empty()) {
    std::fprintf(stderr, "[camera_capture] no cameras found\n");
    return -ENODEV;
  }
  camera_ = cameras[0];
  std::fprintf(stderr, "[camera_capture] using camera id=%s\n",
               camera_->id().c_str());

  // ---- Step 3: acquire -------------------------------------------------
  // acquire() takes exclusive control. Until release() is called, no
  // other process (including the cam tool) can configure or stream
  // this camera.
  rc = camera_->acquire();
  if (rc) {
    logErr("Camera::acquire", rc);
    return rc;
  }
  acquired_ = true;

  // ---- Step 4: ask for a default config --------------------------------
  // generateConfiguration() asks the pipeline handler to fill in a
  // StreamConfiguration that is *valid for the platform* given the
  // role(s) we ask for. Viewfinder hints "low-latency preview-ish
  // 8-bit output" which is exactly what we want.
  config_ = camera_->generateConfiguration({libcamera::StreamRole::Viewfinder});
  if (!config_ || config_->size() == 0) {
    std::fprintf(stderr, "[camera_capture] generateConfiguration returned empty\n");
    return -EINVAL;
  }
  libcamera::StreamConfiguration &sc = config_->at(0);

  // ---- Step 5: override what we care about -----------------------------
  // We ONLY override geometry. Pixel format is left to libcamera so
  // the negotiation produces something the platform can actually emit
  // efficiently. This is why both downstream executables must handle
  // multiple possible formats.
  sc.size = libcamera::Size(cfg.width, cfg.height);
  // Buffer pool depth. 4 gives us slack so the pipeline never starves
  // while a previous frame is still in the callback.
  sc.bufferCount = 4;

  // ---- Step 6: validate ------------------------------------------------
  // validate() is the contract: libcamera will either return Valid
  // (we got exactly what we asked for), Adjusted (it picked the closest
  // legal alternative -- our fields have been rewritten in place),
  // or Invalid (no rescue possible).
  switch (config_->validate()) {
    case libcamera::CameraConfiguration::Valid:
      break;
    case libcamera::CameraConfiguration::Adjusted:
      std::fprintf(stderr,
                   "[camera_capture] config adjusted by libcamera "
                   "(requested %ux%u, got %ux%u, fmt=%s, stride=%u)\n",
                   cfg.width, cfg.height,
                   sc.size.width, sc.size.height,
                   sc.pixelFormat.toString().c_str(), sc.stride);
      break;
    case libcamera::CameraConfiguration::Invalid:
      std::fprintf(stderr, "[camera_capture] config invalid, cannot recover\n");
      return -EINVAL;
  }

  // ---- Step 7: commit --------------------------------------------------
  rc = camera_->configure(config_.get());
  if (rc) {
    logErr("Camera::configure", rc);
    return rc;
  }
  negotiated_ = &config_->at(0);
  stream_     = negotiated_->stream();

  std::fprintf(stderr,
               "[camera_capture] negotiated: %ux%u fmt=%s stride=%u frameSize=%u bufferCount=%u\n",
               negotiated_->size.width, negotiated_->size.height,
               negotiated_->pixelFormat.toString().c_str(),
               negotiated_->stride, negotiated_->frameSize,
               negotiated_->bufferCount);

  // ---- Step 8: allocate buffers ----------------------------------------
  // FrameBufferAllocator backs each FrameBuffer with one or more
  // dmabuf planes. Their lifetime is tied to the allocator.
  allocator_ = std::make_unique<libcamera::FrameBufferAllocator>(camera_);
  rc = allocator_->allocate(stream_);
  if (rc < 0) {
    logErr("FrameBufferAllocator::allocate", rc);
    return rc;
  }

  // ---- Step 9: mmap each plane fd --------------------------------------
  // We need CPU-accessible memory because we publish over ROS or write
  // to disk. mmap() the underlying dmabuf so reads through the returned
  // pointer pull pixels directly from the buffer.
  //
  // IMPORTANT: each plane has its own fd, length, and offset. For a
  // planar format like NV12 the FrameBuffer will report multiple
  // planes; we map every one so the consumer can reach them all.
  // For an interleaved format like BGR888 there is a single plane.
  for (const std::unique_ptr<libcamera::FrameBuffer> &buf :
       allocator_->buffers(stream_)) {
    MappedBuffer mb;
    for (const libcamera::FrameBuffer::Plane &plane : buf->planes()) {
      void *m = ::mmap(nullptr, plane.length, PROT_READ, MAP_SHARED,
                       plane.fd.get(), 0);
      if (m == MAP_FAILED) {
        std::fprintf(stderr,
                     "[camera_capture] mmap failed for plane fd=%d len=%u: %s\n",
                     plane.fd.get(), plane.length, std::strerror(errno));
        return -errno;
      }
      mb.planes.push_back({m, plane.length});
    }
    mapped_.emplace(buf.get(), std::move(mb));
  }

  // ---- Step 10: build one Request per buffer ---------------------------
  // A Request is the "fill this buffer" unit of work. We attach a
  // FrameBuffer to it once at construction and then reuse the Request
  // for every frame: when a request completes, we reset it (preserving
  // the buffer binding) and re-queue it. This avoids re-creating
  // request objects in the hot path.
  for (const std::unique_ptr<libcamera::FrameBuffer> &buf :
       allocator_->buffers(stream_)) {
    std::unique_ptr<libcamera::Request> req = camera_->createRequest();
    if (!req) {
      std::fprintf(stderr, "[camera_capture] createRequest returned null\n");
      return -ENOMEM;
    }
    rc = req->addBuffer(stream_, buf.get());
    if (rc < 0) {
      logErr("Request::addBuffer", rc);
      return rc;
    }
    requests_.push_back(std::move(req));
  }

  // ---- Convert FPS target into a frame duration window ----------------
  if (cfg.fps_target > 0.0) {
    target_frame_duration_us_ =
        static_cast<std::uint64_t>(1'000'000.0 / cfg.fps_target);
  }

  return 0;
}

int CameraCapture::start(FrameCallback cb) {
  if (!camera_) {
    std::fprintf(stderr, "[camera_capture] start() before open()\n");
    return -EINVAL;
  }
  if (streaming_) return 0;

  // Clear any stopping_ flag left over from a previous stop() so the
  // completion thread will requeue on the new run.
  stopping_.store(false, std::memory_order_release);

  {
    std::lock_guard<std::mutex> g(cb_mutex_);
    on_frame_ = std::move(cb);
  }

  // ---- Step 11: subscribe to the completion signal ---------------------
  // This is THE async edge of the API. libcamera will call the slot
  // from a thread it owns whenever a queued Request finishes. We
  // intentionally connect with a member function pointer (the default
  // direct-connection semantics) so the slot runs on libcamera's
  // thread -- there is no extra hop, and the buffer is still mapped
  // when our handler runs.
  camera_->requestCompleted.connect(this, &CameraCapture::onRequestCompleted);

  // ---- Step 12: push a frame-rate control, then start the pipeline ----
  // FrameDurationLimits is a [min, max] pair in microseconds. Setting
  // both to the same value pins the sensor to that exposure cadence
  // (within the sensor's actual limits). If we don't supply this,
  // libcamera defaults to ~30 fps for IMX219.
  libcamera::ControlList controls(libcamera::controls::controls);
  if (target_frame_duration_us_ > 0) {
    std::array<std::int64_t, 2> lim = {
        static_cast<std::int64_t>(target_frame_duration_us_),
        static_cast<std::int64_t>(target_frame_duration_us_),
    };
    controls.set(libcamera::controls::FrameDurationLimits,
                 libcamera::Span<const std::int64_t, 2>(lim));
  }

  int rc = camera_->start(&controls);
  if (rc) {
    logErr("Camera::start", rc);
    camera_->requestCompleted.disconnect(this, &CameraCapture::onRequestCompleted);
    return rc;
  }
  streaming_ = true;

  // ---- Step 13: prime the pipeline -------------------------------------
  // The first time round we hand libcamera every Request so it has the
  // full buffer pool to work with. From then on each completion handler
  // re-queues the Request it just received.
  for (auto &req : requests_) {
    rc = camera_->queueRequest(req.get());
    if (rc < 0) {
      logErr("Camera::queueRequest (priming)", rc);
      return rc;
    }
  }

  return 0;
}

void CameraCapture::stop() {
  // Idempotent teardown. Order matters:
  //   (a) set stopping_ so any in-flight onRequestCompleted() returns
  //       early without trying to re-queue. This MUST be set before
  //       camera_->stop(); otherwise a completion racing the pipeline
  //       transition will call queueRequest() on a Stopping camera,
  //       which libcamera rejects (-EACCES) and prints "Camera in
  //       Stopping state trying queueRequest()".
  //   (b) stop the pipeline, which cancels all in-flight requests.
  //   (c) flip streaming_ off and continue with resource teardown.
  if (camera_ && streaming_) {
    stopping_.store(true, std::memory_order_release);
    // ---- Step 14: stop the pipeline ------------------------------------
    camera_->stop();
    streaming_ = false;
  }

  if (camera_) {
    // Detach from the signal before destroying Request objects so a
    // late completion can't run after we've started tearing them down.
    camera_->requestCompleted.disconnect(this, &CameraCapture::onRequestCompleted);
  }

  {
    // Drop the user callback under lock so any in-flight onRequestCompleted
    // (which holds the lock while invoking the callback) is finished
    // before we release the resources it references.
    std::lock_guard<std::mutex> g(cb_mutex_);
    on_frame_ = nullptr;
  }

  // ---- Step 15: tear down requests, mappings, buffers ------------------
  requests_.clear();

  // ---- Step 16: munmap planes ----------------------------------------
  for (auto &kv : mapped_) {
    for (auto &p : kv.second.planes) {
      if (p.addr && p.length) {
        ::munmap(p.addr, p.length);
      }
    }
  }
  mapped_.clear();

  // ---- Step 17: free buffers -----------------------------------------
  if (allocator_ && stream_) {
    allocator_->free(stream_);
  }
  allocator_.reset();

  // ---- Step 18: release the camera ----------------------------------
  if (camera_ && acquired_) {
    camera_->release();
    acquired_ = false;
  }
  camera_.reset();

  // ---- Step 19: drop the manager ------------------------------------
  if (manager_) {
    manager_->stop();
    manager_.reset();
  }

  config_.reset();
  negotiated_ = nullptr;
  stream_     = nullptr;
}

void CameraCapture::onRequestCompleted(libcamera::Request *request) {
  // Cancelled requests come in during stop() -- ignore them; do not
  // re-queue (the pipeline is no longer accepting them anyway).
  if (request->status() == libcamera::Request::RequestCancelled) {
    return;
  }

  // Exactly one buffer per request in our setup, on our single stream.
  auto it = request->buffers().find(stream_);
  if (it == request->buffers().end()) {
    std::fprintf(stderr, "[camera_capture] completed request had no buffer for our stream\n");
    return;
  }
  libcamera::FrameBuffer *buf = it->second;

  auto mit = mapped_.find(buf);
  if (mit == mapped_.end() || mit->second.planes.empty()) {
    std::fprintf(stderr, "[camera_capture] completed buffer was not in mmap table\n");
    return;
  }
  const MappedPlane &plane0 = mit->second.planes.front();
  const libcamera::FrameMetadata &md = buf->metadata();

  Frame f{};
  f.data             = static_cast<const std::uint8_t *>(plane0.addr);
  // bytesused tells us how many bytes the pipeline actually filled into
  // plane 0; for fixed-format streams this equals frameSize, but we
  // trust the metadata in case the format has variable-size payloads
  // (e.g. MJPEG, which we don't request but might end up with).
  f.length           = md.planes().empty() ? plane0.length
                                           : md.planes().front().bytesused;
  f.plane_size       = plane0.length;
  f.width            = negotiated_->size.width;
  f.height           = negotiated_->size.height;
  // STRIDE: never assume width*bpp. The pipeline can pad rows to align
  // to a hardware-friendly boundary (e.g. 64 bytes). Always use the
  // libcamera-reported stride, both when reading pixels and when
  // populating sensor_msgs/Image::step downstream.
  f.stride           = negotiated_->stride;
  f.pixel_format     = negotiated_->pixelFormat;
  f.pixel_format_str = negotiated_->pixelFormat.toString();
  f.sequence         = md.sequence;
  f.timestamp_ns     = md.timestamp;

  {
    std::lock_guard<std::mutex> g(cb_mutex_);
    if (on_frame_) on_frame_(f);
  }

  // ---- Re-queue the request ------------------------------------------
  // reuse(ReuseBuffers) keeps the FrameBuffer attached so we don't
  // have to re-addBuffer() every cycle. If we forget to queue it back,
  // the pool drains and the pipeline stalls.
  //
  // Re-check both flags here:
  //   * stopping_ is set by stop() BEFORE camera_->stop(), so it is the
  //     synchronization point between this libcamera callback thread
  //     and the stop() caller. If it's set, the pipeline is on its way
  //     to Stopping and queueRequest() will be rejected with -EACCES.
  //   * streaming_ catches the post-stop steady state.
  if (stopping_.load(std::memory_order_acquire) || !streaming_) return;
  request->reuse(libcamera::Request::ReuseBuffers);
  int rc = camera_->queueRequest(request);
  if (rc < 0) {
    logErr("Camera::queueRequest (recycle)", rc);
  }
}

}  // namespace camera_publisher
