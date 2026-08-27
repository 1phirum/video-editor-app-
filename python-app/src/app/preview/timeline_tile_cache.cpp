#include "app/preview/timeline_tile_cache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMutexLocker>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr auto kSubdirectory = "timeline-previews/tiles";

QString cacheRoot() {
  QString root = qEnvironmentVariable("CUTPRO_CACHE_DIR");
  if (root.isEmpty())
    root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir::cleanPath(root);
}
} // namespace

TimelineTileCache &TimelineTileCache::instance() {
  static TimelineTileCache cache;
  return cache;
}

TimelineTileCache::TimelineTileCache(qint64 memoryBudgetBytes)
    : m_memory(memoryBudgetBytes > 0 ? memoryBudgetBytes
                                     : kDefaultMemoryBudgetBytes) {}

QString TimelineTileCache::root() {
  return QDir(cacheRoot()).filePath(QString::fromLatin1(kSubdirectory));
}

QString TimelineTileCache::fingerprintFor(const QString &path) {
  if (path.isEmpty())
    return {};
  const QFileInfo source(path);
  const QString canonical = source.canonicalFilePath();
  const QByteArray key =
      (canonical.isEmpty() ? QDir::cleanPath(source.absoluteFilePath())
                           : canonical)
          .toUtf8() +
      '|' + QByteArray::number(source.lastModified().toMSecsSinceEpoch()) + '|' +
      QByteArray::number(source.size());
  return QString::fromLatin1(
             QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex())
      .left(24);
}
FrameCache::Key TimelineTileCache::keyFor(const QString &fingerprint,
                                          qint64 bucketMs) const {
  FrameCache::Key key;
  key.sourceKey = fingerprint;
  key.bucketMs = bucketMs;
  // All tiles share one decode bound, so the size fields only have to separate
  // them from the monitor's stills inside a shared key space.
  key.width = kTileMaximumSide;
  key.height = 0;
  return key;
}

QString TimelineTileCache::tilePath(const QString &fingerprint,
                                    qint64 bucketMs) const {
  if (fingerprint.isEmpty())
    return {};
  return QDir(QDir(root()).filePath(fingerprint))
      .filePath(QString::number(qMax<qint64>(0, bucketMs)) +
                QStringLiteral(".jpg"));
}

bool TimelineTileCache::ensureDirectory(const QString &fingerprint) {
  if (fingerprint.isEmpty())
    return false;
  {
    QMutexLocker locker(&m_mutex);
    if (m_preparedDirectories.contains(fingerprint))
      return true;
  }
  // One mkpath per source instead of one per tile: a zoomed-in pan over a long
  // clip asks for hundreds of tiles and every one of them would otherwise stat
  // the directory tree again.
  if (!QDir().mkpath(QDir(root()).filePath(fingerprint)))
    return false;
  QMutexLocker locker(&m_mutex);
  if (m_preparedDirectories.size() > 64)
    m_preparedDirectories.clear();
  m_preparedDirectories.insert(fingerprint);
  return true;
}

QImage TimelineTileCache::memoryTile(const QString &fingerprint,
                                     qint64 bucketMs) {
  if (fingerprint.isEmpty() || bucketMs < 0)
    return {};
  const QImage hit = m_memory.lookup(keyFor(fingerprint, bucketMs));
  if (!hit.isNull())
    m_memoryHits.fetch_add(1, std::memory_order_relaxed);
  return hit;
}
QImage TimelineTileCache::tile(const QString &fingerprint, qint64 bucketMs) {
  QImage hit = memoryTile(fingerprint, bucketMs);
  if (!hit.isNull())
    return hit;
  const QString file = tilePath(fingerprint, bucketMs);
  if (file.isEmpty()) {
    m_misses.fetch_add(1, std::memory_order_relaxed);
    return {};
  }
  QImage stored;
  if (!stored.load(file, "JPG") || stored.isNull()) {
    m_misses.fetch_add(1, std::memory_order_relaxed);
    return {};
  }
  m_diskHits.fetch_add(1, std::memory_order_relaxed);
  // Promoted, so the next repaint of the same slot does not touch the disk.
  m_memory.insert(keyFor(fingerprint, bucketMs), stored);
  return stored;
}

void TimelineTileCache::insertMemoryOnly(const QString &fingerprint,
                                         qint64 bucketMs,
                                         const QImage &image) {
  if (fingerprint.isEmpty() || bucketMs < 0 || image.isNull())
    return;
  m_memory.insert(keyFor(fingerprint, bucketMs), image);
}

void TimelineTileCache::insert(const QString &fingerprint, qint64 bucketMs,
                               const QImage &image) {
  insertMemoryOnly(fingerprint, bucketMs, image);
  if (fingerprint.isEmpty() || bucketMs < 0 || image.isNull())
    return;
  if (!ensureDirectory(fingerprint))
    return;
  const QString file = tilePath(fingerprint, bucketMs);
  if (file.isEmpty())
    return;
  // Written through a temporary name: a tile half-written when the app is closed
  // would otherwise be loaded as a corrupt JPEG on the next run.
  const QString temporary = file + QStringLiteral(".part");
  if (!image.save(temporary, "JPG", kJpegQuality)) {
    QFile::remove(temporary);
    return;
  }
  QFile::remove(file);
  if (!QFile::rename(temporary, file)) {
    QFile::remove(temporary);
    return;
  }
  m_writes.fetch_add(1, std::memory_order_relaxed);
  // Self-bounding: panning a zoomed-in eight hour clip can write thousands of
  // tiles and nothing else would ever delete them. Sweeping every few hundred
  // writes keeps the walk off the common path while still capping the tree.
  if (m_writes.load(std::memory_order_relaxed) % 512 == 0)
    sweep(kDefaultDiskBudgetBytes);
}
void TimelineTileCache::forget(const QString &fingerprint) {
  if (fingerprint.isEmpty())
    return;
  m_memory.clearSource(fingerprint);
  {
    QMutexLocker locker(&m_mutex);
    m_preparedDirectories.remove(fingerprint);
  }
  QDir directory(QDir(root()).filePath(fingerprint));
  if (directory.exists())
    directory.removeRecursively();
}

void TimelineTileCache::clearMemory() { m_memory.clear(); }

qint64 TimelineTileCache::sweep(qint64 maximumBytes) {
  QDir parent(root());
  if (!parent.exists())
    return 0;
  struct Bucket {
    QString path;
    qint64 bytes = 0;
    qint64 lastUsedMs = 0;
  };
  QList<Bucket> buckets;
  qint64 total = 0;
  const QFileInfoList sources =
      parent.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo &source : sources) {
    Bucket bucket;
    bucket.path = source.absoluteFilePath();
    bucket.lastUsedMs = source.lastModified().toMSecsSinceEpoch();
    const QFileInfoList tiles =
        QDir(bucket.path).entryInfoList(QDir::Files);
    for (const QFileInfo &tile : tiles) {
      bucket.bytes += tile.size();
      // A source directory's own timestamp does not move when a tile inside it
      // is read, so the newest tile is the better measure of recent use.
      bucket.lastUsedMs =
          qMax(bucket.lastUsedMs, tile.lastModified().toMSecsSinceEpoch());
    }
    total += bucket.bytes;
    buckets.append(bucket);
  }
  if (total <= maximumBytes)
    return total;
  std::sort(buckets.begin(), buckets.end(),
            [](const Bucket &a, const Bucket &b) {
              return a.lastUsedMs < b.lastUsedMs;
            });
  for (const Bucket &bucket : buckets) {
    if (total <= maximumBytes)
      break;
    if (QDir(bucket.path).removeRecursively())
      total -= bucket.bytes;
  }
  QMutexLocker locker(&m_mutex);
  m_preparedDirectories.clear();
  return total;
}
qint64 TimelineTileCache::memoryBytes() const { return m_memory.bytes(); }

int TimelineTileCache::memoryCount() const { return m_memory.count(); }

quint64 TimelineTileCache::memoryHits() const {
  return m_memoryHits.load(std::memory_order_relaxed);
}

quint64 TimelineTileCache::diskHits() const {
  return m_diskHits.load(std::memory_order_relaxed);
}

quint64 TimelineTileCache::misses() const {
  return m_misses.load(std::memory_order_relaxed);
}

quint64 TimelineTileCache::writes() const {
  return m_writes.load(std::memory_order_relaxed);
}

QVariantMap TimelineTileCache::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("tileMemoryBytes")] = memoryBytes();
  stats[QStringLiteral("tileMemoryEntries")] = memoryCount();
  stats[QStringLiteral("tileMemoryHits")] = qulonglong(memoryHits());
  stats[QStringLiteral("tileDiskHits")] = qulonglong(diskHits());
  stats[QStringLiteral("tileMisses")] = qulonglong(misses());
  stats[QStringLiteral("tileWrites")] = qulonglong(writes());
  return stats;
}
