#pragma once

// Rate limiting for work driven by a continuous input stream, and the test for
// whether that work needs doing at all.
//
// SignalCoalescer in app/core_app already collapses notifications to one per
// trip through the event loop, which is the right shape for an edit that touches
// twelve clips inside one turn. It is the wrong shape for a scroll: a scroll
// delivers exactly one content-x change per turn, so "at most one per turn"
// collapses nothing at all and the expensive recomputation still happens sixty
// times a second. What a gesture needs is a rate, and something that notices the
// gesture has stopped.
//
// GesturePacer is that rate. Calls collapse into at most one apply per interval,
// the first call in a burst applies immediately so nothing feels laggy, and a
// settle callback fires once the stream goes quiet - which is where the work
// that must not run mid-gesture belongs: thumbnail requests, waveform tiles,
// decoder seeks. The pattern is "cheap while moving, correct when stopped", and
// it is the difference between a scroll that tears along and one that fights the
// pointer.
//
// ViewportGate is the other half. Even paced to once per frame, recomputing a
// projection for a window that is still inside the one already computed is pure
// waste, and while scrolling that is the common case: the model pads its window
// by thirty seconds, so anything inside that padding was answered last time.
// The gate holds the last accepted window and says whether the new one has left
// its margin.
//
// Neither is a QObject. The pacer takes the work as a closure and owns a QTimer
// parented to whatever object scopes it, so destroying the owner stops the
// pacing with it and nothing about the caller's QML-visible interface changes.

#include <QElapsedTimer>
#include <QPointer>
#include <QVariantMap>
#include <QtGlobal>

#include <functional>

class QObject;
class QTimer;

namespace cutpro::perf {

class GesturePacer {
public:
  // `owner` scopes the timers; passing null falls back to the application
  // object, and with no event loop at all every request applies inline. 16 ms is
  // one frame at 60 Hz. `settleMs` is how long the stream has to be quiet before
  // the gesture counts as over.
  GesturePacer(QObject *owner, std::function<void()> apply,
               int intervalMs = 16, int settleMs = 120);
  ~GesturePacer();

  GesturePacer(const GesturePacer &) = delete;
  GesturePacer &operator=(const GesturePacer &) = delete;

  // Asks for the work to run. The first call in a burst runs it now; the rest
  // collapse into one run at the end of the current interval.
  void request();
  // Runs it now, whatever the interval says. For the end of a gesture, a
  // keyboard edit, or a caller about to read the result back.
  void requestNow();
  // Drops anything pending without running it, and ends the gesture without
  // firing the settle callback.
  void cancel();

  bool pending() const { return m_pending; }
  // True from the first request until settleMs after the last one. Read this to
  // degrade quality mid-gesture.
  bool isActive() const { return m_active; }

  // Runs once, settleMs after the last request. Replaces any previous callback.
  void setOnSettled(std::function<void()> settled);
  void setIntervalMs(int intervalMs);
  void setSettleMs(int settleMs);
  int intervalMs() const { return m_intervalMs; }
  int settleMs() const { return m_settleMs; }

  // requests, applies, and the difference: work asked for that did not have to
  // happen. A ratio near 1:1 during a drag means the interval is too short to be
  // buying anything.
  QVariantMap statistics() const;

private:
  void fire();

  std::function<void()> m_apply;
  std::function<void()> m_settled;
  QPointer<QTimer> m_timer;
  QPointer<QTimer> m_settleTimer;
  QElapsedTimer m_sinceApply;
  int m_intervalMs = 16;
  int m_settleMs = 120;
  bool m_pending = false;
  bool m_active = false;
  quint64 m_requests = 0;
  quint64 m_applies = 0;
};

// "Has the visible window moved far enough to be worth recomputing?"
class ViewportGate {
public:
  // The margin is a fraction of the window's own width, so it scales with zoom
  // instead of covering hours at the zoomed-out end of a 26-hour source.
  // `minMarginMs` is a floor for the case where the window is tiny.
  explicit ViewportGate(double marginFraction = 0.25, qint64 minMarginMs = 0);

  // True when [startMs, endMs] has left the margin around the last accepted
  // window, or the scale changed - and records the new window. False means the
  // last computation still covers this one and the caller can return.
  bool moved(qint64 startMs, qint64 endMs, double msPerPixel);
  // The same test without recording anything.
  bool covers(qint64 startMs, qint64 endMs, double msPerPixel) const;
  void reset();

  qint64 startMs() const { return m_startMs; }
  qint64 endMs() const { return m_endMs; }
  double msPerPixel() const { return m_msPerPixel; }
  // Margin the last accepted window earned. Callers usually want to pad their
  // own query by at least this much, or the gate will skip a window whose edges
  // were never computed.
  qint64 marginMs() const;

  QVariantMap statistics() const;

private:
  double m_marginFraction = 0.25;
  qint64 m_minMarginMs = 0;
  qint64 m_startMs = 0;
  qint64 m_endMs = 0;
  double m_msPerPixel = 0.0;
  bool m_valid = false;
  quint64 m_accepted = 0;
  quint64 m_rejected = 0;
};

} // namespace cutpro::perf
