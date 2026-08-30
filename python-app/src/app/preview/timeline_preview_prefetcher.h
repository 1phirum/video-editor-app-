#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include <QWaitCondition>

#include <atomic>

#include "core/module_api.h"

// Ordered, bounded decoding for everything the timeline draws behind a clip.
//
// The timeline used to work the other way round: a QML slot pointed an Image at
// "image://timeline-tile/<token>/<ms>" and the provider decoded whatever was
// asked for. That reads well and behaves badly. A screenful is around thirty
// slots, a drop or a zoom rewrites all of them in one frame, and on a multi-hour
// source every one of those is a real seek plus decode. Thirty decodes are then
// in flight at once, the provider's two threads and the governor's two slots
// serve them in an order nobody chose, and the requests that lose are completed
// with a transparent pixel - which the strip cannot tell apart from a frame, so
// those cells stay blank until something unrelated invalidates them. That is
// both halves of the complaint: the window stops painting on a drop, and the
// strip ends up as a few thumbnails with gaps that reappear after every zoom.
//
// So the decision of *what to decode next* moves here, out of the binding
// layer:
//
//  * QML declares the positions it wants, as one wish per clip layer, and
//    replaces that wish whenever the visible slice changes. It never asks for a
//    picture that is not already in memory, so no provider thread ever blocks
//    and a refused request cannot leave a hole.
//  * one worker decodes them, one at a time, in the order given - left to right
//    across the visible slice - round-robining between clips so a long source
//    cannot starve the others.
//  * each landed item bumps a revision that QML watches. The strip and the
//    waveform therefore *grow*, at whatever rate the machine can actually
//    decode, instead of appearing all at once or not at all.
//
// A re-issued wish that is identical keeps its cursor, so the periodic re-issue
// QML uses as a heartbeat does not restart the sweep from the left; once a sweep
// is spent, the next re-issue starts a fresh one, which is how an item some gate
// refused gets another chance without any per-item bookkeeping.
class CUTPRO_PREVIEW_API TimelinePreviewPrefetcher final : public QObject {
  Q_OBJECT
public:
  enum class Kind {
    // Timeline thumbnails, by keyframe bucket.
    Tile,
    // Waveform detail, by window start.
    WaveWindow,
  };

  // A screenful plus margin. Past this the wish is truncated: positions off the
  // end of the viewport are not worth a decode that a pan will invalidate.
  static constexpr int kMaximumPositions = 96;
  // Wishes are dropped by age past this. Every clip layer on screen owns one, so
  // this is a very large timeline; the cap only exists so a leaked requester id
  // cannot grow the table without bound.
  static constexpr int kMaximumWishes = 256;
  // How often the GUI thread checks whether anything landed. Coalesces a burst
  // of completions into one round of QML re-evaluation.
  static constexpr int kNotifyIntervalMs = 70;
  // Notify ticks with nothing new before the timer stops. Re-armed by the next
  // wish.
  static constexpr int kQuietTicksBeforeIdle = 12;
  // How long the worker parks while a gesture is in progress, and how long it
  // parks when every wish is spent.
  static constexpr int kInteractionPollMs = 120;
  static constexpr int kIdlePollMs = 400;

  static TimelinePreviewPrefetcher &instance();

  ~TimelinePreviewPrefetcher() override;

  // Replaces whatever `requesterId` asked for last. `bucketsMs` are the exact
  // values QML builds its tile URLs from, so the position the worker decodes and
  // the position the strip later looks up are the same number - no snapping
  // logic is duplicated between C++ and QML.
  void requestTiles(const QString &requesterId, const QString &path,
                    const QVector<qint64> &bucketsMs);
  void requestWindows(const QString &requesterId, const QString &path,
                      const QVector<qint64> &startsMs, qint64 spanMs,
                      int columns);
  void cancel(const QString &requesterId);
  void clear();

  // Bumped once per item that lands. QML compares it, so it only has to be
  // monotonic.
  int revision() const {
    return m_revision.load(std::memory_order_acquire);
  }

  QVariantMap statistics() const;

signals:
  // Coalesced, on the GUI thread. Something new is in memory.
  void progressed();

private:
  class Worker;
  friend class Worker;

  struct Wish {
    Kind kind = Kind::Tile;
    QString path;
    QVector<qint64> positions;
    qint64 spanMs = 0;
    int columns = 0;
    int next = 0;
  };

  struct Item {
    bool valid = false;
    Kind kind = Kind::Tile;
    QString path;
    qint64 positionMs = 0;
    qint64 spanMs = 0;
    int columns = 0;
  };

  TimelinePreviewPrefetcher();

  void submit(const QString &requesterId, Wish wish);
  // Blocks until there is something to do, or until the worker is stopping - in
  // which case the returned item is invalid.
  Item takeNext();
  void decode(const Item &item);
  void noteLanded();

  static bool sameShape(const Wish &a, const Wish &b);

  mutable QMutex m_mutex;
  QWaitCondition m_work;
  QHash<QString, Wish> m_wishes;
  // Requester ids in arrival order, for round-robin. May name a wish that has
  // since been cancelled; those are pruned when they are next walked over.
  QStringList m_order;
  int m_cursor = 0;
  bool m_stopping = false;

  Worker *m_worker = nullptr;
  QTimer m_notify;
  int m_notifiedRevision = 0;
  int m_quietTicks = 0;

  std::atomic<int> m_revision{0};
  std::atomic<quint64> m_decoded{0};
  std::atomic<quint64> m_alreadyHeld{0};
  std::atomic<quint64> m_refused{0};
  std::atomic<quint64> m_failed{0};
};
