#include "app/preview/preview_failure_registry.h"

#include <QDateTime>
#include <QMutexLocker>

#include <algorithm>

namespace {
qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

qint64 grown(qint64 base, int repeats, qint64 ceiling) {
  // Doubling, but only for the first few repeats: the point is to back off, not
  // to give up for minutes on a source the user is actively working with.
  const int steps = qBound(0, repeats, 4);
  qint64 span = base;
  for (int i = 0; i < steps; ++i)
    span *= 2;
  return qMin(span, ceiling);
}
} // namespace

PreviewFailureRegistry &PreviewFailureRegistry::instance() {
  static PreviewFailureRegistry registry;
  return registry;
}

bool PreviewFailureRegistry::shouldAttempt(const QString &sourceKey,
                                           qint64 bucketMs) {
  if (sourceKey.isEmpty())
    return true;
  const qint64 now = nowMs();
  QMutexLocker locker(&m_mutex);
  const auto found = m_sources.find(sourceKey);
  if (found == m_sources.end())
    return true;
  if (found->suspendedUntilMs > now) {
    locker.unlock();
    m_skippedSources.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const auto bucket = found->buckets.constFind(bucketMs);
  if (bucket == found->buckets.constEnd() || bucket->retryAtMs <= now)
    return true;
  locker.unlock();
  m_skippedBuckets.fetch_add(1, std::memory_order_relaxed);
  return false;
}

bool PreviewFailureRegistry::sourceSuspended(const QString &sourceKey) const {
  if (sourceKey.isEmpty())
    return false;
  const qint64 now = nowMs();
  QMutexLocker locker(&m_mutex);
  const auto found = m_sources.constFind(sourceKey);
  return found != m_sources.constEnd() && found->suspendedUntilMs > now;
}

void PreviewFailureRegistry::noteFailure(const QString &sourceKey,
                                         qint64 bucketMs) {
  if (sourceKey.isEmpty())
    return;
  const qint64 now = nowMs();
  bool suspended = false;
  {
    QMutexLocker locker(&m_mutex);
    Source &source = m_sources[sourceKey];
    Bucket &bucket = source.buckets[bucketMs];
    bucket.retryAtMs =
        now + grown(kBucketCooloffMs, bucket.failures, kBucketCooloffCeilingMs);
    ++bucket.failures;

    if (source.buckets.size() > kMaximumBucketsPerSource) {
      // Drop the entries whose cool-off ends soonest: they are the ones closest
      // to being retried anyway, so forgetting them costs the least.
      QList<qint64> keys = source.buckets.keys();
      std::sort(keys.begin(), keys.end(), [&source](qint64 a, qint64 b) {
        return source.buckets.value(a).retryAtMs <
               source.buckets.value(b).retryAtMs;
      });
      const int excess = source.buckets.size() - kMaximumBucketsPerSource;
      for (int i = 0; i < excess && i < keys.size(); ++i)
        source.buckets.remove(keys[i]);
    }

    if (++source.consecutiveFailures >= kSourceFailureThreshold) {
      source.consecutiveFailures = 0;
      source.suspendedUntilMs =
          now + grown(kSourceSuspendMs, source.suspensions,
                      kSourceSuspendCeilingMs);
      ++source.suspensions;
      suspended = true;
    }
    trimLocked();
  }
  m_failures.fetch_add(1, std::memory_order_relaxed);
  if (suspended)
    m_suspensions.fetch_add(1, std::memory_order_relaxed);
}

void PreviewFailureRegistry::noteSuccess(const QString &sourceKey,
                                         qint64 bucketMs) {
  m_successes.fetch_add(1, std::memory_order_relaxed);
  if (sourceKey.isEmpty())
    return;
  QMutexLocker locker(&m_mutex);
  const auto found = m_sources.find(sourceKey);
  if (found == m_sources.end())
    return;
  found->consecutiveFailures = 0;
  found->suspendedUntilMs = 0;
  found->suspensions = 0;
  found->buckets.remove(bucketMs);
  if (found->buckets.isEmpty())
    m_sources.erase(found);
}

void PreviewFailureRegistry::forget(const QString &sourceKey) {
  QMutexLocker locker(&m_mutex);
  m_sources.remove(sourceKey);
}

void PreviewFailureRegistry::clear() {
  QMutexLocker locker(&m_mutex);
  m_sources.clear();
}

void PreviewFailureRegistry::trimLocked() {
  if (m_sources.size() <= kMaximumSources)
    return;
  // An unsuspended source with few remembered spans is the cheapest memory to
  // lose; a suspended one is the whole point of the registry, so it is evicted
  // only when every source is suspended, and then the one that recovers first.
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  auto victim = m_sources.end();
  for (auto it = m_sources.begin(); it != m_sources.end(); ++it) {
    if (victim == m_sources.end()) {
      victim = it;
      continue;
    }
    const bool candidateSuspended = it->suspendedUntilMs > now;
    const bool victimSuspended = victim->suspendedUntilMs > now;
    if (candidateSuspended != victimSuspended) {
      if (victimSuspended)
        victim = it;
      continue;
    }
    if (candidateSuspended) {
      if (it->suspendedUntilMs < victim->suspendedUntilMs)
        victim = it;
    } else if (it->buckets.size() < victim->buckets.size()) {
      victim = it;
    }
  }
  if (victim != m_sources.end())
    m_sources.erase(victim);
}

QVariantMap PreviewFailureRegistry::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("decodeFailures")] =
      qulonglong(m_failures.load(std::memory_order_relaxed));
  stats[QStringLiteral("decodeSuccesses")] =
      qulonglong(m_successes.load(std::memory_order_relaxed));
  stats[QStringLiteral("skippedCoolingSpans")] =
      qulonglong(m_skippedBuckets.load(std::memory_order_relaxed));
  stats[QStringLiteral("skippedSuspendedSources")] =
      qulonglong(m_skippedSources.load(std::memory_order_relaxed));
  stats[QStringLiteral("sourceSuspensions")] =
      qulonglong(m_suspensions.load(std::memory_order_relaxed));
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  int suspended = 0;
  int remembered = 0;
  QMutexLocker locker(&m_mutex);
  for (auto it = m_sources.constBegin(); it != m_sources.constEnd(); ++it) {
    remembered += it->buckets.size();
    if (it->suspendedUntilMs > now)
      ++suspended;
  }
  stats[QStringLiteral("failingSources")] = m_sources.size();
  stats[QStringLiteral("suspendedSources")] = suspended;
  stats[QStringLiteral("rememberedFailedSpans")] = remembered;
  return stats;
}
