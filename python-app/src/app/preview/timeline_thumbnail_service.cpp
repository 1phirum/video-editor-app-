#include "app/preview/timeline_thumbnail_service.h"

#include "app/preview/decode_work_governor.h"
#include "app/preview/media_token_registry.h"
#include "app/preview/preview_failure_registry.h"
#include "app/preview/seek_thumbnail_extractor.h"
#include "app/preview/timeline_tile_cache.h"

#include <QMutexLocker>
#include <QSize>

namespace {
// A tile that takes longer than this is not worth the wait: the strip draws the
// neighbouring cell meanwhile and the next repaint asks again from a session
// that is by then warm.
constexpr int kTileTimeBudgetMs = 4000;
// Long sources hold a few thousand keyframe timestamps each. Sixteen indexes is
// a whole timeline's worth of distinct sources and still a rounding error next
// to one decoded frame.
constexpr int kMaximumRememberedIndexes = 16;
// How long a tile worker waits for one of the governor's decode slots. Long
// enough to ride out another source's seek, short enough that a screenful of
// cells does not queue for seconds behind two of them.
constexpr int kTileAdmissionWaitMs = 2000;
} // namespace

TimelineThumbnailService &TimelineThumbnailService::instance() {
  static TimelineThumbnailService service;
  return service;
}

TimelineThumbnailService::TimelineThumbnailService() = default;

bool TimelineThumbnailService::available() {
  return SeekThumbnailExtractor::available();
}

// One registry for every on-demand provider: a clip's tile URL and its waveform
// URL carry the same handle, so QML asks for a token once per source.
QString TimelineThumbnailService::tokenFor(const QString &path) {
  return MediaTokenRegistry::instance().token(path);
}

QString TimelineThumbnailService::pathForToken(const QString &token) const {
  return MediaTokenRegistry::instance().path(token);
}
QString TimelineThumbnailService::fingerprintFor(const QString &path) {
  {
    QMutexLocker locker(&m_mutex);
    const auto found = m_fingerprintByPath.constFind(path);
    if (found != m_fingerprintByPath.constEnd())
      return *found;
  }
  // One stat per source instead of one per tile: a screenful of thumbnails is
  // dozens of lookups and they all resolve to the same file.
  const QString fingerprint = TimelineTileCache::fingerprintFor(path);
  QMutexLocker locker(&m_mutex);
  if (m_fingerprintByPath.size() > 64)
    m_fingerprintByPath.clear();
  m_fingerprintByPath.insert(path, fingerprint);
  return fingerprint;
}

qint64 TimelineThumbnailService::snapped(const QString &path,
                                        qint64 positionMs) const {
  QMutexLocker locker(&m_mutex);
  const auto found = m_keyframesByPath.constFind(path);
  if (found == m_keyframesByPath.constEnd())
    return -1;
  return found->bucketFor(qMax<qint64>(0, positionMs));
}

void TimelineThumbnailService::rememberKeyframes(const QString &path,
                                                 const KeyframeIndex &index) {
  QMutexLocker locker(&m_mutex);
  if (m_keyframesByPath.size() >= kMaximumRememberedIndexes &&
      !m_keyframesByPath.contains(path))
    m_keyframesByPath.clear();
  m_keyframesByPath.insert(path, index);
}

QImage TimelineThumbnailService::cachedTile(const QString &path,
                                           qint64 positionMs) {
  if (path.isEmpty())
    return {};
  const qint64 bucketMs = snapped(path, positionMs);
  if (bucketMs < 0)
    return {};
  return TimelineTileCache::instance().tile(fingerprintFor(path), bucketMs);
}

bool TimelineThumbnailService::hasTile(const QString &path,
                                      qint64 positionMs) {
  if (path.isEmpty())
    return false;
  // A cold source has no index yet, so nothing can be claimed as held: the first
  // tile the prefetcher decodes is what makes this answerable at all.
  const qint64 bucketMs = snapped(path, positionMs);
  if (bucketMs < 0)
    return false;
  return !TimelineTileCache::instance()
              .memoryTile(fingerprintFor(path), bucketMs)
              .isNull();
}
TimelineThumbnailService::Tile
TimelineThumbnailService::tile(const QString &path, qint64 positionMs,
                              const std::atomic_bool *cancel) {
  Tile result;
  if (path.isEmpty()) {
    result.error = QStringLiteral("No media path was given.");
    return result;
  }
  const auto aborted = [cancel]() {
    return cancel && cancel->load(std::memory_order_acquire);
  };

  const QString fingerprint = fingerprintFor(path);
  TimelineTileCache &cache = TimelineTileCache::instance();

  // Warm source: the bucket is known without opening anything, so a tile that is
  // already held is returned on the calling thread.
  const qint64 known = snapped(path, positionMs);
  if (known >= 0) {
    result.image = cache.tile(fingerprint, known);
    if (!result.image.isNull()) {
      result.bucketMs = known;
      result.fromCache = true;
      m_serves.fetch_add(1, std::memory_order_relaxed);
      return result;
    }
  }

  if (aborted()) {
    result.cancelled = true;
    m_cancels.fetch_add(1, std::memory_order_relaxed);
    return result;
  }
  if (!available()) {
    result.error =
        QStringLiteral("This build cannot generate timeline thumbnails.");
    return result;
  }

  // Nothing past this point is cheap: a tile that misses the cache costs a seek,
  // a read and a decode on a file that may be gigabytes. Two gates stand in front
  // of it, and both report the request as cancelled rather than failed, because
  // the strip already draws a placeholder for a cancelled cell while an error
  // per cell is what filled the console.
  const qint64 attemptBucket = known >= 0 ? known : qMax<qint64>(0, positionMs);
  if (!PreviewFailureRegistry::instance().shouldAttempt(fingerprint,
                                                        attemptBucket)) {
    result.cancelled = true;
    m_cancels.fetch_add(1, std::memory_order_relaxed);
    return result;
  }
  DecodeWorkGovernor::Admission admission =
      DecodeWorkGovernor::instance().admit(
          DecodeWorkGovernor::Class::Filmstrip, kTileAdmissionWaitMs);
  if (!admission.granted()) {
    result.cancelled = true;
    m_cancels.fetch_add(1, std::memory_order_relaxed);
    return result;
  }

  DecodeSession::Profile profile;
  profile.maximumFrameSize = QSize(TimelineTileCache::kTileMaximumSide,
                                   TimelineTileCache::kTileMaximumSide);
  profile.exactSeek = false;
  profile.frameTimeBudgetMs = kTileTimeBudgetMs;

  DecodeSessionCache::Lease lease = m_sessions.checkout(path, profile);
  if (!lease.valid()) {
    if (!aborted()) {
      m_failures.fetch_add(1, std::memory_order_relaxed);
      PreviewFailureRegistry::instance().noteFailure(fingerprint, attemptBucket);
      result.error = QStringLiteral("Could not open %1 for thumbnails.").arg(path);
    } else {
      result.cancelled = true;
    }
    return result;
  }
  lease->setCancelToken(cancel);
  const KeyframeIndex &index = lease->keyframes();
  rememberKeyframes(path, index);
  const qint64 bucketMs = index.bucketFor(qMax<qint64>(0, positionMs));
  result.bucketMs = bucketMs;

  // Checked again: the first lookup used an estimated bucket, or none at all,
  // and the real index may point at a tile that is already on disk.
  if (bucketMs != known) {
    result.image = cache.tile(fingerprint, bucketMs);
    if (!result.image.isNull()) {
      result.fromCache = true;
      m_serves.fetch_add(1, std::memory_order_relaxed);
      return result;
    }
  }
  if (aborted()) {
    result.cancelled = true;
    m_cancels.fetch_add(1, std::memory_order_relaxed);
    return result;
  }

  result.image = lease->frameAt(qMax<qint64>(0, positionMs));
  if (result.image.isNull()) {
    if (aborted()) {
      result.cancelled = true;
      m_cancels.fetch_add(1, std::memory_order_relaxed);
    } else {
      m_failures.fetch_add(1, std::memory_order_relaxed);
      // Remembered so the next repaint of this cell does not spend another
      // seek, another read and another whole time budget on a position this
      // source has just proven it cannot answer.
      PreviewFailureRegistry::instance().noteFailure(fingerprint, bucketMs);
      result.error = lease->error().isEmpty()
                         ? QStringLiteral("No frame could be decoded.")
                         : lease->error();
    }
    return result;
  }

  PreviewFailureRegistry::instance().noteSuccess(fingerprint, bucketMs);
  cache.insert(fingerprint, bucketMs, result.image);
  m_decodes.fetch_add(1, std::memory_order_relaxed);
  m_serves.fetch_add(1, std::memory_order_relaxed);
  return result;
}

void TimelineThumbnailService::forget(const QString &path) {
  if (path.isEmpty())
    return;
  const QString fingerprint = fingerprintFor(path);
  m_sessions.drop(path);
  TimelineTileCache::instance().forget(fingerprint);
  // A re-imported or replaced file must not inherit the old one's suspension.
  PreviewFailureRegistry::instance().forget(fingerprint);
  QMutexLocker locker(&m_mutex);
  m_keyframesByPath.remove(path);
  m_fingerprintByPath.remove(path);
  // The token is kept: QML may still hold URLs built from it, and resolving one
  // to a path that has no tiles left is harmless.
}
QVariantMap TimelineThumbnailService::statistics() const {
  QVariantMap stats = TimelineTileCache::instance().statistics();
  stats[QStringLiteral("tileAvailable")] = available();
  stats[QStringLiteral("tileServes")] =
      qulonglong(m_serves.load(std::memory_order_relaxed));
  stats[QStringLiteral("tileDecodes")] =
      qulonglong(m_decodes.load(std::memory_order_relaxed));
  stats[QStringLiteral("tileCancels")] =
      qulonglong(m_cancels.load(std::memory_order_relaxed));
  stats[QStringLiteral("tileFailures")] =
      qulonglong(m_failures.load(std::memory_order_relaxed));
  stats[QStringLiteral("tileIdleSessions")] = m_sessions.idleSessions();
  stats[QStringLiteral("tileSessionOpens")] = qulonglong(m_sessions.opens());
  stats[QStringLiteral("tileSessionReuse")] =
      qulonglong(m_sessions.reuseHits());
  QMutexLocker locker(&m_mutex);
  stats[QStringLiteral("tileIndexedSources")] = m_keyframesByPath.size();
  return stats;
}
