#pragma once

#include <QImage>
#include <QMutex>
#include <QSize>
#include <QtGlobal>

#include <memory>
#include <vector>

// Recycled pixel buffers for decoded preview frames, with a hard memory ceiling.
//
// The preview decoder used to allocate a fresh QImage for every decoded frame
// and deep-copy it again on every read, so a 24 fps 540p session churned tens of
// megabytes per second through the allocator and a 4K session far more. This
// pool hands out QImages that wrap pooled buffers instead: the QImage itself is
// still refcounted and safe to copy across threads, but when the last copy is
// destroyed the buffer returns here rather than to the allocator.
//
// The budget is what keeps memory bounded on long or high-resolution sources.
// When every buffer is still held by the UI and the budget is spent, acquire()
// returns a null QImage and the caller drops that frame - dropping is always
// preferable to growing a queue the monitor can never catch up with.
class FrameBufferPool final : public std::enable_shared_from_this<FrameBufferPool> {
public:
  // Rows are padded to this alignment so swscale's SIMD paths stay on their
  // fast branches when writing into a pooled buffer.
  static constexpr qsizetype kRowAlignment = 32;
  // Even with an exhausted budget the pool keeps enough buffers for one frame
  // on screen plus one being decoded, so a small budget can never stall
  // playback completely.
  static constexpr int kMinimumBuffers = 2;

  static std::shared_ptr<FrameBufferPool> create(qint64 byteBudget);
  ~FrameBufferPool();

  FrameBufferPool(const FrameBufferPool &) = delete;
  FrameBufferPool &operator=(const FrameBufferPool &) = delete;

  // A QImage backed by a pooled buffer, or a null QImage when the budget is
  // spent and nothing is free. Never blocks.
  QImage acquire(const QSize &size, QImage::Format format = QImage::Format_RGBA8888);

  // Bytes per line the pool uses for a given width, exposed so callers can size
  // their own strides identically.
  static qsizetype bytesPerLine(int width, QImage::Format format);

  qint64 byteBudget() const;
  // The budget follows the frame geometry: a 1080p RGBA frame is nine times a
  // 360p one, so a ceiling sized for the small frame drops every large frame.
  // Called from the decode thread when the source geometry changes.
  void setByteBudget(qint64 bytes);
  // Everything the pool owns: idle buffers plus buffers still referenced by a
  // published frame.
  qint64 allocatedBytes() const;
  qint64 idleBytes() const;
  int idleBuffers() const;
  int liveBuffers() const;

  // Release the idle buffers. Called when the frame geometry changes so buffers
  // sized for the previous source do not sit on the budget.
  void trim();

private:
  struct Buffer {
    uchar *base = nullptr;
    uchar *aligned = nullptr;
    qint64 capacity = 0;
  };

  explicit FrameBufferPool(qint64 byteBudget);

  static void releaseTrampoline(void *info);
  void release(const Buffer &buffer);
  static Buffer allocate(qint64 capacity);
  static void destroy(const Buffer &buffer);

  mutable QMutex m_mutex;
  std::vector<Buffer> m_idle;
  qint64 m_byteBudget = 0;
  qint64 m_allocatedBytes = 0;
  qint64 m_idleBytes = 0;
  int m_liveBuffers = 0;
};
