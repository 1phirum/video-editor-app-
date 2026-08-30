#include "app/preview/scrub_frame_service.h"

#include "app/preview/decode_session.h"
#include "app/preview/decode_work_governor.h"
#include "app/preview/frame_cache.h"
#include "app/preview/preview_failure_registry.h"
#include "app/preview/seek_thumbnail_extractor.h"

#include <QMutexLocker>

#include <utility>

namespace {
// Used until a source's real GOP length is known. Two seconds is the usual
// keyframe spacing, so half of it is a fair "close enough" window for the
// placeholder frame shown mid-drag.
constexpr qint64 kDefaultToleranceMs = 1000;
// How stale a stand-in frame is allowed to be. Half a GOP is the right window on
// a normal source, but a long-GOP recording (10 s between keyframes is ordinary
// for a screen capture) would make that half window five seconds: the monitor
// would answer a whole scrub with one picture and look frozen. Past this bound
// the cached neighbour is refused, the request goes to the worker, and the
// worker answers with the frame that belongs to the position asked for.
constexpr qint64 kMaximumToleranceMs = 500;
} // namespace

ScrubFrameService::ScrubFrameService(QObject *parent) : QObject(parent) {
  if (available())
    m_worker = std::thread([this]() { run(); });
}

ScrubFrameService::~ScrubFrameService() {
  {
    QMutexLocker locker(&m_mutex);
    m_shutdown = true;
    m_hasPending = false;
  }
  // The decode in flight can be inside a blocking read on a slow disk; the token
  // is what makes the join bounded.
  m_cancelCurrent.store(true, std::memory_order_release);
  m_wake.wakeAll();
  if (m_worker.joinable())
    m_worker.join();
}

bool ScrubFrameService::available() {
  return SeekThumbnailExtractor::available();
}

QString ScrubFrameService::sourceKeyFor(const QString &path) {
  {
    QMutexLocker locker(&m_mutex);
    const auto found = m_sourceKeyByPath.constFind(path);
    if (found != m_sourceKeyByPath.constEnd())
      return *found;
  }
  // One stat per file rather than one per scrub tick.
  const QString key = FrameCache::sourceKeyFor(path);
  QMutexLocker locker(&m_mutex);
  if (m_sourceKeyByPath.size() > 64)
    m_sourceKeyByPath.clear();
  m_sourceKeyByPath.insert(path, key);
  return key;
}

qint64 ScrubFrameService::toleranceFor(const QString &sourceKey) const {
  QMutexLocker locker(&m_mutex);
  const auto found = m_gopBySource.constFind(sourceKey);
  if (found == m_gopBySource.constEnd())
    return qMin(kDefaultToleranceMs, kMaximumToleranceMs);
  return qBound<qint64>(1, *found / 2, kMaximumToleranceMs);
}

void ScrubFrameService::rememberTolerance(const QString &sourceKey,
                                          qint64 gopMs) {
  QMutexLocker locker(&m_mutex);
  if (m_gopBySource.size() > 64)
    m_gopBySource.clear();
  m_gopBySource.insert(sourceKey, qMax<qint64>(1, gopMs));
}

bool ScrubFrameService::request(const QString &path, qint64 positionMs,
                                const QSize &maximumSize, bool exact) {
  if (path.isEmpty() || !available())
    return false;

  Task task;
  task.path = path;
  task.sourceKey = sourceKeyFor(path);
  task.positionMs = qMax<qint64>(0, positionMs);
  task.maximumSize = maximumSize.isEmpty() ? QSize(960, 540) : maximumSize;
  task.exact = exact;
  task.generation = m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;

  // Served here, on the caller's thread, when the picture is already in memory:
  // a scrub back over ground already covered should not wait for a queue.
  FrameCache::Key key;
  key.sourceKey = task.sourceKey;
  key.bucketMs = task.positionMs;
  key.width = task.maximumSize.width();
  key.height = task.maximumSize.height();
  key.exact = exact;
  qint64 matchedBucket = -1;
  const QImage hit = FrameCache::instance().nearest(
      key, exact ? 0 : toleranceFor(task.sourceKey), &matchedBucket);
  bool servedFromCache = false;
  if (!hit.isNull()) {
    publish(hit, task.sourceKey, matchedBucket);
    m_cacheHits.fetch_add(1, std::memory_order_relaxed);
    servedFromCache = true;
  }

  {
    QMutexLocker locker(&m_mutex);
    if (m_shutdown)
      return servedFromCache;
    if (m_hasPending)
      m_coalesced.fetch_add(1, std::memory_order_relaxed);
    m_pending = task;
    m_hasPending = true;
  }
  // The frame being decoded right now is for an older position, so it is already
  // worthless; aborting it frees the worker for the newest one immediately.
  m_cancelCurrent.store(true, std::memory_order_release);
  m_wake.wakeAll();
  return servedFromCache;
}

void ScrubFrameService::prewarm(const QString &path, const QSize &maximumSize) {
  if (path.isEmpty() || !available())
    return;
  Task task;
  task.path = path;
  task.sourceKey = sourceKeyFor(path);
  task.maximumSize = maximumSize.isEmpty() ? QSize(960, 540) : maximumSize;
  task.prewarmOnly = true;
  task.generation = m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  {
    QMutexLocker locker(&m_mutex);
    if (m_shutdown)
      return;
    // A prewarm must never displace a real scrub request.
    if (m_hasPending)
      return;
    m_pending = task;
    m_hasPending = true;
  }
  m_wake.wakeAll();
}

void ScrubFrameService::cancel() {
  {
    QMutexLocker locker(&m_mutex);
    m_hasPending = false;
  }
  m_generation.fetch_add(1, std::memory_order_acq_rel);
  m_cancelCurrent.store(true, std::memory_order_release);
}

void ScrubFrameService::forget(const QString &path) {
  const QString sourceKey = sourceKeyFor(path);
  DecodeSessionCache::instance().drop(path);
  FrameCache::instance().clearSource(sourceKey);
  PreviewFailureRegistry::instance().forget(sourceKey);
  QMutexLocker locker(&m_mutex);
  m_gopBySource.remove(sourceKey);
  m_sourceKeyByPath.remove(path);
}

void ScrubFrameService::run() {
  for (;;) {
    Task task;
    {
      QMutexLocker locker(&m_mutex);
      while (!m_hasPending && !m_shutdown)
        m_wake.wait(&m_mutex);
      if (m_shutdown)
        return;
      task = m_pending;
      m_hasPending = false;
    }
    // Cleared only now: a cancel raised while this task was queued was aimed at
    // the previous decode, not at this one.
    m_cancelCurrent.store(false, std::memory_order_release);
    setBusy(true);
    process(task);
    setBusy(false);
  }
}

void ScrubFrameService::process(const Task &task) {
  DecodeSession::Profile profile;
  profile.maximumFrameSize = task.maximumSize;
  profile.exactSeek = task.exact;
  // A scrub frame that takes longer than this is not worth waiting for: the
  // playhead has moved on. The next request will ask again from a warm session.
  profile.frameTimeBudgetMs = task.prewarmOnly ? 8000 : 1500;

  // The monitor frame is what the user is waiting for, so it is admitted ahead
  // of thumbnails - but it still takes a slot, so it cannot pile onto a machine
  // that is already decoding as much as it can.
  DecodeWorkGovernor::Admission admission =
      DecodeWorkGovernor::instance().admit(
          DecodeWorkGovernor::Class::Interactive, 1200);
  if (!admission.granted())
    return;

  DecodeSessionCache::Lease lease =
      DecodeSessionCache::instance().checkout(task.path, profile);
  if (!lease.valid()) {
    if (!m_cancelCurrent.load(std::memory_order_acquire))
      setError(QStringLiteral("Could not open %1 for preview.").arg(task.path));
    return;
  }
  lease->setCancelToken(&m_cancelCurrent);

  const KeyframeIndex &keyframes = lease->keyframes();
  rememberTolerance(task.sourceKey, keyframes.gopSpanMs());
  if (task.prewarmOnly)
    return;
  if (superseded(task.generation))
    return;

  // Snapped to the keyframe the seek will actually land on, so every position
  // inside one GOP shares a cache slot instead of decoding the same picture
  // again.
  const qint64 bucketMs =
      task.exact ? task.positionMs : keyframes.bucketFor(task.positionMs);

  FrameCache::Key key;
  key.sourceKey = task.sourceKey;
  key.bucketMs = bucketMs;
  key.width = task.maximumSize.width();
  key.height = task.maximumSize.height();
  key.exact = task.exact;

  QImage frame = FrameCache::instance().lookup(key);
  if (frame.isNull()) {
    // A position this source has just failed on is not asked again until its
    // cool-off expires: on a damaged or awkward span the failure costs the whole
    // time budget, and a drag would otherwise repeat it for every step.
    if (!PreviewFailureRegistry::instance().shouldAttempt(task.sourceKey,
                                                          bucketMs))
      return;
    frame = lease->frameAt(task.positionMs);
    if (!frame.isNull()) {
      FrameCache::instance().insert(key, frame);
      PreviewFailureRegistry::instance().noteSuccess(task.sourceKey, bucketMs);
      m_decodes.fetch_add(1, std::memory_order_relaxed);
    } else if (!m_cancelCurrent.load(std::memory_order_acquire) &&
               !superseded(task.generation)) {
      PreviewFailureRegistry::instance().noteFailure(task.sourceKey, bucketMs);
    }
  } else {
    m_cacheHits.fetch_add(1, std::memory_order_relaxed);
  }

  if (frame.isNull()) {
    // A cancelled decode is the normal outcome of a fast drag, not a failure.
    if (!m_cancelCurrent.load(std::memory_order_acquire) &&
        !superseded(task.generation))
      setError(lease->error());
    return;
  }
  if (superseded(task.generation))
    return;
  setError(QString());
  publish(frame, task.sourceKey, bucketMs);
  m_served.fetch_add(1, std::memory_order_relaxed);
}

void ScrubFrameService::publish(const QImage &image, const QString &sourceKey,
                                qint64 bucketMs) {
  if (image.isNull())
    return;
  {
    QMutexLocker locker(&m_frameMutex);
    // Already on screen: republishing would repaint the monitor with the picture
    // it is showing, which is what made a slow drag flicker.
    if (bucketMs >= 0 && m_publishedBucketMs == bucketMs &&
        m_publishedSourceKey == sourceKey && !m_frame.isNull())
      return;
    m_frame = image;
    m_publishedSourceKey = sourceKey;
    m_publishedBucketMs = bucketMs;
  }
  emit frameReady(m_publishedRevision.fetch_add(1, std::memory_order_acq_rel) +
                  1);
}

QImage ScrubFrameService::frame() const {
  QMutexLocker locker(&m_frameMutex);
  return m_frame;
}

QString ScrubFrameService::error() const {
  QMutexLocker locker(&m_errorMutex);
  return m_error;
}

void ScrubFrameService::setError(const QString &message) {
  const QString clean = message.trimmed();
  {
    QMutexLocker locker(&m_errorMutex);
    if (clean == m_error)
      return;
    m_error = clean;
  }
  emit errorChanged();
}

void ScrubFrameService::setBusy(bool busy) {
  if (m_busy.exchange(busy, std::memory_order_acq_rel) == busy)
    return;
  emit busyChanged();
}

QVariantMap ScrubFrameService::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("served")] =
      qulonglong(m_served.load(std::memory_order_relaxed));
  stats[QStringLiteral("cacheHits")] =
      qulonglong(m_cacheHits.load(std::memory_order_relaxed));
  stats[QStringLiteral("decodes")] =
      qulonglong(m_decodes.load(std::memory_order_relaxed));
  stats[QStringLiteral("coalesced")] =
      qulonglong(m_coalesced.load(std::memory_order_relaxed));
  stats[QStringLiteral("busy")] = busy();
  stats[QStringLiteral("frameCacheEntries")] = FrameCache::instance().count();
  stats[QStringLiteral("frameCacheBytes")] = FrameCache::instance().bytes();
  stats[QStringLiteral("frameCacheHits")] =
      qulonglong(FrameCache::instance().hits());
  stats[QStringLiteral("frameCacheMisses")] =
      qulonglong(FrameCache::instance().misses());
  stats[QStringLiteral("idleSessions")] =
      DecodeSessionCache::instance().idleSessions();
  stats[QStringLiteral("sessionOpens")] =
      qulonglong(DecodeSessionCache::instance().opens());
  stats[QStringLiteral("sessionReuse")] =
      qulonglong(DecodeSessionCache::instance().reuseHits());
  return stats;
}
