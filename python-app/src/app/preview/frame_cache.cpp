#include "app/preview/frame_cache.h"

#include <QDateTime>
#include <QFileInfo>
#include <QMutexLocker>

#include <algorithm>

QString FrameCache::Key::id() const {
  return QStringLiteral("%1|%2|%3x%4%5")
      .arg(sourceKey)
      .arg(bucketMs)
      .arg(width)
      .arg(height)
      .arg(exact ? QStringLiteral("|e") : QString());
}

QString FrameCache::sourceKeyFor(const QString &path) {
  const QFileInfo info(path);
  // Modification time and size stand in for the contents: re-rendering a proxy
  // or replacing a file in place has to invalidate its frames.
  return QStringLiteral("%1|%2|%3")
      .arg(info.absoluteFilePath())
      .arg(info.lastModified().toMSecsSinceEpoch())
      .arg(info.size());
}

FrameCache &FrameCache::instance() {
  static FrameCache cache;
  return cache;
}

FrameCache::FrameCache(qint64 budgetBytes)
    : m_budgetBytes(qMax<qint64>(4LL * 1024 * 1024, budgetBytes)) {}

QImage FrameCache::lookup(const Key &key) {
  QMutexLocker locker(&m_mutex);
  const auto found = m_entries.find(key.id());
  if (found == m_entries.end()) {
    ++m_misses;
    return {};
  }
  found->tick = ++m_tick;
  ++m_hits;
  return found->image;
}

QImage FrameCache::nearest(const Key &key, qint64 toleranceMs,
                           qint64 *matchedBucketMs) {
  if (toleranceMs <= 0) {
    QImage exactHit = lookup(key);
    if (!exactHit.isNull() && matchedBucketMs)
      *matchedBucketMs = key.bucketMs;
    return exactHit;
  }
  QMutexLocker locker(&m_mutex);
  Entry *best = nullptr;
  qint64 bestDistance = toleranceMs + 1;
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
    if (it->key.sourceKey != key.sourceKey || it->key.width != key.width ||
        it->key.height != key.height || it->key.exact != key.exact)
      continue;
    const qint64 distance = qAbs(it->key.bucketMs - key.bucketMs);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = &(*it);
    }
  }
  if (!best) {
    ++m_misses;
    return {};
  }
  best->tick = ++m_tick;
  ++m_hits;
  if (matchedBucketMs)
    *matchedBucketMs = best->key.bucketMs;
  return best->image;
}

void FrameCache::insert(const Key &key, const QImage &image) {
  if (image.isNull())
    return;
  QMutexLocker locker(&m_mutex);
  const QString id = key.id();
  const auto existing = m_entries.find(id);
  if (existing != m_entries.end())
    m_bytes -= existing->bytes;
  Entry entry;
  entry.key = key;
  // Detached: the decoder reuses its scaling buffers, and a QImage sharing them
  // would change under the cache.
  entry.image = image;
  entry.image.detach();
  entry.bytes = qint64(entry.image.sizeInBytes());
  entry.tick = ++m_tick;
  m_bytes += entry.bytes;
  m_entries.insert(id, entry);
  evictLocked();
}

void FrameCache::evictLocked() {
  while (m_bytes > m_budgetBytes && !m_entries.isEmpty()) {
    // The cache holds tens of entries, so a linear scan for the oldest is
    // cheaper than maintaining a second ordered structure.
    auto oldest = m_entries.begin();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
      if (it->tick < oldest->tick)
        oldest = it;
    }
    m_bytes -= oldest->bytes;
    m_entries.erase(oldest);
  }
}

void FrameCache::clear() {
  QMutexLocker locker(&m_mutex);
  m_entries.clear();
  m_bytes = 0;
}

void FrameCache::clearSource(const QString &sourceKey) {
  QMutexLocker locker(&m_mutex);
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    if (it->key.sourceKey == sourceKey) {
      m_bytes -= it->bytes;
      it = m_entries.erase(it);
    } else {
      ++it;
    }
  }
}

qint64 FrameCache::bytes() const {
  QMutexLocker locker(&m_mutex);
  return m_bytes;
}

qint64 FrameCache::budgetBytes() const {
  QMutexLocker locker(&m_mutex);
  return m_budgetBytes;
}

void FrameCache::setBudgetBytes(qint64 bytes) {
  QMutexLocker locker(&m_mutex);
  m_budgetBytes = qMax<qint64>(4LL * 1024 * 1024, bytes);
  evictLocked();
}

int FrameCache::count() const {
  QMutexLocker locker(&m_mutex);
  return int(m_entries.size());
}

quint64 FrameCache::hits() const {
  QMutexLocker locker(&m_mutex);
  return m_hits;
}

quint64 FrameCache::misses() const {
  QMutexLocker locker(&m_mutex);
  return m_misses;
}
