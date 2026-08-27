#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QWaitCondition>

#include <atomic>
#include <thread>

// Newest-wins frame service for scrubbing.
//
// A drag on the timeline emits a position every few milliseconds. Decoding all
// of them is pointless work: only the last one is ever seen. Before this, every
// request tore down and restarted a decoder, so a fast drag on a long source
// queued dozens of container opens and the picture lagged seconds behind the
// playhead.
//
// The rules here are the ones a smooth scrub needs:
//  - one worker thread, one pending request. A request arriving while another is
//    being decoded replaces it and cancels the decode in flight, because the
//    frame it would produce is already stale;
//  - a cache hit is served on the calling thread, so revisiting a position that
//    was already decoded is immediate rather than a round trip through a queue;
//  - during a fast drag a neighbouring cached frame is shown at once and the
//    exact one replaces it when it arrives;
//  - the container stays open between requests (DecodeSessionCache), so the
//    second frame of a source costs a seek instead of a header parse.
class ScrubFrameService final : public QObject {
  Q_OBJECT

public:
  explicit ScrubFrameService(QObject *parent = nullptr);
  ~ScrubFrameService() override;

  static bool available();

  // Queues `positionMs`, replacing anything still waiting. Returns true when a
  // frame was published synchronously from the cache.
  bool request(const QString &path, qint64 positionMs,
               const QSize &maximumSize, bool exact = false);
  // Opens the container and builds its keyframe index ahead of the first scrub,
  // so the first frame is not the slow one.
  void prewarm(const QString &path, const QSize &maximumSize);
  // Drops the queued request and aborts the decode in flight. Sessions and
  // cached frames are kept: this is a "stop showing new frames", not a reset.
  void cancel();
  // Forgets everything held for one source, for when a file is removed or
  // replaced.
  void forget(const QString &path);

  QImage frame() const;
  quint64 revision() const { return m_publishedRevision.load(std::memory_order_acquire); }
  QString error() const;
  bool busy() const { return m_busy.load(std::memory_order_acquire); }

  // Counters for the debug overlay: served, cacheHits, decodes, coalesced,
  // sessions, cacheBytes.
  QVariantMap statistics() const;

signals:
  void frameReady(quint64 revision);
  void busyChanged();
  void errorChanged();

private:
  struct Task {
    QString path;
    QString sourceKey;
    qint64 positionMs = 0;
    QSize maximumSize;
    bool exact = false;
    bool prewarmOnly = false;
    quint64 generation = 0;
  };

  void run();
  void process(const Task &task);
  void publish(const QImage &image, const QString &sourceKey, qint64 bucketMs);
  void setError(const QString &message);
  void setBusy(bool busy);
  bool superseded(quint64 generation) const {
    return generation != m_generation.load(std::memory_order_acquire);
  }
  // Half a GOP for a source whose index has already been read, and a
  // conservative default for one that has not.
  qint64 toleranceFor(const QString &sourceKey) const;
  void rememberTolerance(const QString &sourceKey, qint64 gopMs);
  QString sourceKeyFor(const QString &path);

  mutable QMutex m_mutex; // guards the pending task and the tolerance table
  QWaitCondition m_wake;
  Task m_pending;
  bool m_hasPending = false;
  bool m_shutdown = false;
  QHash<QString, qint64> m_gopBySource;
  QHash<QString, QString> m_sourceKeyByPath;

  mutable QMutex m_frameMutex;
  QImage m_frame;
  QString m_publishedSourceKey;
  qint64 m_publishedBucketMs = -1;

  mutable QMutex m_errorMutex;
  QString m_error;

  std::thread m_worker;
  std::atomic_bool m_cancelCurrent{false};
  std::atomic_bool m_busy{false};
  std::atomic<quint64> m_generation{0};
  std::atomic<quint64> m_publishedRevision{0};
  std::atomic<quint64> m_served{0};
  std::atomic<quint64> m_cacheHits{0};
  std::atomic<quint64> m_decodes{0};
  std::atomic<quint64> m_coalesced{0};
};
