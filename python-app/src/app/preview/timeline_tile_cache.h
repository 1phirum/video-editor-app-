#pragma once

#include "app/preview/frame_cache.h"

#include <QMutex>
#include <QSet>
#include <QString>
#include <QVariantMap>

#include <atomic>

// Storage for the timeline's on-demand thumbnails: one small JPEG per keyframe
// position, in memory and on disk.
//
// The filmstrip sheet FilmstripBuilder produces is a summary - at most 48 tiles
// for the whole source. That is the right thing when the clip is a few hundred
// pixels wide, and the wrong thing the moment the user zooms in, because one
// tile then has to cover ten minutes of an eight hour file. CapCut answers that
// by fetching the frame each visible slot actually needs, which is only
// affordable with somewhere to keep them: a memory LRU for the slots on screen
// and a disk cache so panning back over a stretch already looked at - or
// reopening the project tomorrow - costs a file read instead of a seek and a
// decode.
//
// Tiles are keyed by the keyframe bucket they were decoded from, never by the
// requested position, so every zoom level and every scroll offset that lands
// inside one GOP shares a single entry.
class TimelineTileCache final {
public:
  // Every tile is decoded and stored at this bound, whatever height the timeline
  // is currently drawing. One size means one decoder profile per source (so the
  // session pool stays warm across zoom changes) and one disk entry per bucket
  // (so zooming does not multiply the cache).
  static constexpr int kTileMaximumSide = 480;
  static constexpr qint64 kDefaultMemoryBudgetBytes = 64LL * 1024 * 1024;
  static constexpr qint64 kDefaultDiskBudgetBytes = 384LL * 1024 * 1024;
  static constexpr int kJpegQuality = 80;

  static TimelineTileCache &instance();

  explicit TimelineTileCache(
      qint64 memoryBudgetBytes = kDefaultMemoryBudgetBytes);

  // Identity of the file contents: canonical path, modification time and size.
  // A re-encoded file gets a new fingerprint, so a stale tile cannot be served.
  static QString fingerprintFor(const QString &path);
  static QString root();

  // Memory only. Cheap enough to call from the GUI thread.
  QImage memoryTile(const QString &fingerprint, qint64 bucketMs);
  // Memory, then disk. Promotes a disk hit into memory.
  QImage tile(const QString &fingerprint, qint64 bucketMs);

  void insert(const QString &fingerprint, qint64 bucketMs, const QImage &image);
  void insertMemoryOnly(const QString &fingerprint, qint64 bucketMs,
                        const QImage &image);

  void forget(const QString &fingerprint);
  void clearMemory();
  // Deletes whole source directories, oldest first, until the tile tree fits.
  qint64 sweep(qint64 maximumBytes);

  qint64 memoryBytes() const;
  int memoryCount() const;
  quint64 memoryHits() const;
  quint64 diskHits() const;
  quint64 misses() const;
  quint64 writes() const;
  QVariantMap statistics() const;

private:
  FrameCache::Key keyFor(const QString &fingerprint, qint64 bucketMs) const;
  QString tilePath(const QString &fingerprint, qint64 bucketMs) const;
  bool ensureDirectory(const QString &fingerprint);

  FrameCache m_memory;
  mutable QMutex m_mutex;
  QSet<QString> m_preparedDirectories;
  std::atomic<quint64> m_memoryHits{0};
  std::atomic<quint64> m_diskHits{0};
  std::atomic<quint64> m_misses{0};
  std::atomic<quint64> m_writes{0};
};
