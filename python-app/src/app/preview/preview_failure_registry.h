#pragma once

#include <QHash>
#include <QMutex>
#include <QString>
#include <QVariantMap>

#include <atomic>

// Memory of preview requests that a source could not answer, so the on-demand
// providers stop asking it the same question at full speed.
//
// A filmstrip cell, a scrub still and a waveform window are all "decode
// something at position P of file F". When F cannot answer - a damaged span, a
// codec whose keyframe-only path returns nothing, a container with no usable
// index - the request does not fail cheaply: it costs a seek, a read and the
// caller's whole time budget, and the QML Image then logs
// "No frame could be decoded." Nothing remembers the outcome, so the next
// repaint asks again, for every visible cell, on every provider thread. That is
// what turns one unreadable source into a pegged CPU, a thrashing disk and a
// window Windows marks "Not Responding".
//
// Two levels of memory, both time-bounded so a transient failure heals itself:
//
//  * per position bucket - a span that just failed is not retried until its
//    cool-off expires, and the cool-off grows each time it fails again;
//  * per source - after enough consecutive failures with no success in between,
//    the whole source is suspended briefly. This is the circuit breaker: an
//    eight hour AV1 file that answers nothing stops being asked hundreds of
//    times a second, and the timeline draws its placeholder instead.
//
// A single success clears both levels: the source is trusted again immediately.
class PreviewFailureRegistry final {
public:
  static PreviewFailureRegistry &instance();

  // False when this exact position is still cooling off, or the whole source is
  // suspended. Callers treat that as "cancelled", not as an error, so no
  // warning reaches the console.
  bool shouldAttempt(const QString &sourceKey, qint64 bucketMs);
  // True while the circuit breaker is open for this source.
  bool sourceSuspended(const QString &sourceKey) const;

  void noteFailure(const QString &sourceKey, qint64 bucketMs);
  void noteSuccess(const QString &sourceKey, qint64 bucketMs);

  void forget(const QString &sourceKey);
  void clear();
  QVariantMap statistics() const;

  // Cool-off for one position: doubles per repeat failure up to the ceiling.
  static constexpr qint64 kBucketCooloffMs = 4000;
  static constexpr qint64 kBucketCooloffCeilingMs = 60000;
  // Consecutive failures before the source itself is suspended.
  static constexpr int kSourceFailureThreshold = 6;
  static constexpr qint64 kSourceSuspendMs = 12000;
  static constexpr qint64 kSourceSuspendCeilingMs = 120000;
  // Remembered positions per source. A screenful of filmstrip cells is a few
  // dozen, so this holds several screens of history for the price of a few
  // kilobytes.
  static constexpr int kMaximumBucketsPerSource = 256;
  static constexpr int kMaximumSources = 24;

private:
  struct Bucket {
    qint64 retryAtMs = 0;
    int failures = 0;
  };

  struct Source {
    QHash<qint64, Bucket> buckets;
    int consecutiveFailures = 0;
    int suspensions = 0;
    qint64 suspendedUntilMs = 0;
  };

  PreviewFailureRegistry() = default;

  void trimLocked();

  mutable QMutex m_mutex;
  QHash<QString, Source> m_sources;

  std::atomic<quint64> m_failures{0};
  std::atomic<quint64> m_successes{0};
  std::atomic<quint64> m_skippedBuckets{0};
  std::atomic<quint64> m_skippedSources{0};
  std::atomic<quint64> m_suspensions{0};
};
