#include "app/preview/frame_buffer_pool.h"

#include <QMutexLocker>
#include <QPixelFormat>

#include <algorithm>
#include <new>

namespace {

// Heap record handed to QImage as cleanup info. It keeps only a weak reference,
// so a pool destroyed while frames are still on screen frees those buffers
// directly instead of resurrecting itself.
struct ReleaseHandle {
  std::weak_ptr<FrameBufferPool> pool;
  uchar *base = nullptr;
  uchar *aligned = nullptr;
  qint64 capacity = 0;
};

} // namespace

std::shared_ptr<FrameBufferPool> FrameBufferPool::create(qint64 byteBudget) {
  return std::shared_ptr<FrameBufferPool>(new FrameBufferPool(byteBudget));
}

FrameBufferPool::FrameBufferPool(qint64 byteBudget)
    : m_byteBudget(qMax<qint64>(0, byteBudget)) {}

FrameBufferPool::~FrameBufferPool() {
  // Live buffers are owned by their QImage copies from here on; the release
  // trampoline sees an expired weak_ptr and frees them.
  QMutexLocker locker(&m_mutex);
  for (const Buffer &buffer : m_idle)
    destroy(buffer);
  m_idle.clear();
  m_idleBytes = 0;
}

qsizetype FrameBufferPool::bytesPerLine(int width, QImage::Format format) {
  const int depth = QImage::toPixelFormat(format).bitsPerPixel();
  const qsizetype minimum = (qsizetype(qMax(0, width)) * qMax(1, depth) + 7) / 8;
  const qsizetype remainder = minimum % kRowAlignment;
  return remainder == 0 ? minimum : minimum + (kRowAlignment - remainder);
}

FrameBufferPool::Buffer FrameBufferPool::allocate(qint64 capacity) {
  Buffer buffer;
  // Over-allocate so the first row can start on an aligned address without
  // depending on a platform aligned-allocation function.
  buffer.base = new (std::nothrow) uchar[std::size_t(capacity + kRowAlignment)];
  if (!buffer.base)
    return {};
  const auto address = reinterpret_cast<quintptr>(buffer.base);
  const quintptr offset = address % kRowAlignment;
  buffer.aligned = buffer.base + (offset == 0 ? 0 : kRowAlignment - offset);
  buffer.capacity = capacity;
  return buffer;
}

void FrameBufferPool::destroy(const Buffer &buffer) { delete[] buffer.base; }

QImage FrameBufferPool::acquire(const QSize &size, QImage::Format format) {
  if (size.width() <= 0 || size.height() <= 0)
    return {};

  const qsizetype stride = bytesPerLine(size.width(), format);
  const qint64 needed = qint64(stride) * size.height();
  if (needed <= 0)
    return {};

  Buffer buffer;
  {
    QMutexLocker locker(&m_mutex);
    const auto reusable =
        std::find_if(m_idle.begin(), m_idle.end(),
                     [needed](const Buffer &candidate) {
                       return candidate.capacity >= needed;
                     });
    if (reusable != m_idle.end()) {
      buffer = *reusable;
      m_idleBytes -= buffer.capacity;
      m_idle.erase(reusable);
    } else {
      // The minimum buffer count wins over the budget: without it a budget
      // smaller than two frames would drop every frame forever.
      const bool withinBudget = m_allocatedBytes + needed <= m_byteBudget;
      if (!withinBudget && m_liveBuffers >= kMinimumBuffers)
        return {};
      buffer = allocate(needed);
      if (!buffer.base)
        return {};
      m_allocatedBytes += buffer.capacity;
    }
    ++m_liveBuffers;
  }

  auto *handle = new ReleaseHandle{weak_from_this(), buffer.base, buffer.aligned,
                                   buffer.capacity};
  QImage image(buffer.aligned, size.width(), size.height(), stride, format,
               &FrameBufferPool::releaseTrampoline, handle);
  if (image.isNull()) {
    delete handle;
    QMutexLocker locker(&m_mutex);
    --m_liveBuffers;
    m_allocatedBytes -= buffer.capacity;
    destroy(buffer);
    return {};
  }
  return image;
}

void FrameBufferPool::releaseTrampoline(void *info) {
  auto *handle = static_cast<ReleaseHandle *>(info);
  if (!handle)
    return;
  // Called on whichever thread drops the last copy of the frame, which is
  // usually the GUI thread and never the thread that acquired it.
  if (const std::shared_ptr<FrameBufferPool> pool = handle->pool.lock())
    pool->release(Buffer{handle->base, handle->aligned, handle->capacity});
  else
    destroy(Buffer{handle->base, handle->aligned, handle->capacity});
  delete handle;
}

void FrameBufferPool::release(const Buffer &buffer) {
  QMutexLocker locker(&m_mutex);
  m_liveBuffers = qMax(0, m_liveBuffers - 1);
  // Anything over the budget is returned to the allocator rather than parked in
  // the idle list, so a burst never becomes a permanent memory floor.
  if (m_idleBytes + buffer.capacity > m_byteBudget) {
    m_allocatedBytes -= buffer.capacity;
    destroy(buffer);
    return;
  }
  m_idleBytes += buffer.capacity;
  m_idle.push_back(buffer);
}

void FrameBufferPool::trim() {
  QMutexLocker locker(&m_mutex);
  for (const Buffer &buffer : m_idle) {
    m_allocatedBytes -= buffer.capacity;
    destroy(buffer);
  }
  m_idle.clear();
  m_idleBytes = 0;
}

qint64 FrameBufferPool::byteBudget() const {
  QMutexLocker locker(&m_mutex);
  return m_byteBudget;
}

void FrameBufferPool::setByteBudget(qint64 bytes) {
  const qint64 budget = qMax<qint64>(0, bytes);
  QMutexLocker locker(&m_mutex);
  if (budget == m_byteBudget)
    return;
  m_byteBudget = budget;
  // Idle buffers above the new ceiling go back to the allocator now instead of
  // waiting for a release to notice, so shrinking the budget actually frees.
  while (m_idleBytes > m_byteBudget && !m_idle.empty()) {
    const Buffer buffer = m_idle.back();
    m_idle.pop_back();
    m_idleBytes -= buffer.capacity;
    m_allocatedBytes -= buffer.capacity;
    destroy(buffer);
  }
}

qint64 FrameBufferPool::allocatedBytes() const {
  QMutexLocker locker(&m_mutex);
  return m_allocatedBytes;
}

qint64 FrameBufferPool::idleBytes() const {
  QMutexLocker locker(&m_mutex);
  return m_idleBytes;
}

int FrameBufferPool::idleBuffers() const {
  QMutexLocker locker(&m_mutex);
  return int(m_idle.size());
}

int FrameBufferPool::liveBuffers() const {
  QMutexLocker locker(&m_mutex);
  return m_liveBuffers;
}
