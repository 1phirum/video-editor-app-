#pragma once

// Rate-limited progress reporting for a run with thousands of items.
//
// TextToSpeechEngine publishes progress like this:
//
//     void TextToSpeechEngine::setState(double progress, const QString &status) {
//       m_progress = progress;
//       m_status = status;
//       emit stateChanged();
//     }
//
// and handleWorkerLine() calls it for every PROGRESS line the worker writes -
// one per synthesised segment - building the status string with two .arg() calls
// each time. updateTimelineImport() calls it once per imported clip as well,
// while the timeline is being mutated underneath. For a 19 831-cue transcript
// that is around forty thousand notifications and forty thousand formatted
// strings, each one making every binding on the engine re-evaluate, and there is
// no change test: the same progress with the same status still emits. A progress
// bar can show about sixty distinct positions per second, so nearly all of that
// work is invisible by construction.
//
// This paces it. Items report completion as they finish, the publish callback
// runs at most once per interval plus once more when the stream goes quiet, and
// it is skipped when the snapshot has not actually changed. Nothing is lost: the
// callback is handed the current totals, so a collapsed run of steps is reported
// by the next publish rather than dropped.
//
// Weighted totals are here because a bar that counts items jumps - twelve
// seconds of speech is not the same amount of work as two, which is why the
// dashboard already weights its batch figures by duration. Hand the weights over
// at begin() and report indices as they land.
//
// Out-of-order and repeated reports are expected and handled: the TTS worker
// overlaps requests, so completions do not arrive in index order, and a double
// report would otherwise push the bar past the end.
//
// Single threaded and not a QObject. It is shaped for a caller like the TTS
// engine, whose worker is a separate process and whose progress lines therefore
// arrive on the GUI thread inside a readyRead handler.

#include <QBitArray>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>

#include <functional>
#include <memory>

#include "app/performance/gesture_pacer.h"

class QObject;

namespace cutpro::perf {

class BatchProgress {
public:
  // Everything the UI needs in one value, so a publish is one call and a
  // comparison against the last one is cheap.
  struct Snapshot {
    int done = 0;
    int total = 0;
    int failed = 0;
    // 0..1, weighted when weights were given.
    double fraction = 0.0;
    QString label;
    bool active = false;

    friend bool operator==(const Snapshot &a, const Snapshot &b) {
      return a.done == b.done && a.total == b.total && a.failed == b.failed &&
             a.active == b.active && qFuzzyCompare(a.fraction + 1.0,
                                                   b.fraction + 1.0) &&
             a.label == b.label;
    }
    friend bool operator!=(const Snapshot &a, const Snapshot &b) {
      return !(a == b);
    }
  };

  using Publish = std::function<void(const Snapshot &)>;

  // `owner` scopes the internal timer. `publish` is the single place the UI is
  // told anything. 50 ms is twenty updates a second, which is past the point
  // where a bar reads as continuous.
  BatchProgress(QObject *owner, Publish publish, int intervalMs = 50);

  // Starts a run of `total` equally weighted items and publishes immediately, so
  // the bar appears at once rather than at the end of the first interval.
  void begin(int total, const QString &label = QString());
  // Starts a run whose items are not equal. Weights below zero are treated as
  // zero and simply never move the bar.
  void beginWeighted(const QVector<qint64> &weights,
                     const QString &label = QString());

  // One item finished. `index` is into the weight list; -1 means the caller
  // cannot say which, in which case a weighted run advances by the mean weight
  // so the bar stays honest in aggregate. Repeated and out-of-range indices are
  // ignored.
  void step(int index = -1, bool ok = true);
  void setLabel(const QString &label);

  // Publishes now, whatever the interval says, and restarts the interval clock.
  void flush();
  // Final state: fills the bar, publishes once, and stops pacing.
  void finish(const QString &label = QString());
  // Ends the run where it stands, without filling the bar.
  void cancel(const QString &label = QString());

  Snapshot snapshot() const;
  double fraction() const;
  bool isActive() const { return m_active; }
  int done() const { return m_done; }
  int total() const { return m_total; }
  int failed() const { return m_failed; }
  const QString &label() const { return m_label; }

  // steps, publishes, and the difference: reports that did not have to reach the
  // UI. During a large run the ratio should be in the hundreds.
  QVariantMap statistics() const;

private:
  void publishNow(bool force);
  void reset(int total, const QString &label);

  Publish m_publish;
  std::unique_ptr<GesturePacer> m_pacer;
  QVector<qint64> m_weights;
  QBitArray m_reported;
  Snapshot m_lastPublished;
  QString m_label;
  qint64 m_weightTotal = 0;
  qint64 m_weightDone = 0;
  int m_total = 0;
  int m_done = 0;
  int m_failed = 0;
  bool m_active = false;
  quint64 m_steps = 0;
  quint64 m_publishes = 0;
};

} // namespace cutpro::perf
