#pragma once

#include <QVariantMap>

#include <atomic>
#include <functional>

// Turns "notify QML that this changed" into at most one notification per trip
// through the event loop.
//
// A Qt notify signal is not free the way it looks. `emit somethingChanged()`
// re-evaluates every binding attached to every property that declares it as
// NOTIFY, synchronously, before the emit returns - and if those bindings are
// what makes a panel exist, the emit instantiates the panel. The drop path was
// measured doing exactly that: Backend::setSelectedClipId's body is a guard, an
// assignment and two emits, and it held the GUI thread for 413-462 ms across
// three runs, because the second emit rebuilt the colour panel's control tree
// inside the drop handler.
//
// Two separate problems live in that sentence, and this class is for both:
//
//  * the work happened at the worst possible moment. Nothing about a colour
//    panel needs to be correct before the drop returns and the frame is
//    presented; it needs to be correct before the user looks at it. Scheduling
//    the emit for the next event-loop turn moves several hundred milliseconds
//    out of the gesture without changing what the user eventually sees, because
//    the property getters are still the same getters - only the moment QML
//    re-reads them moves;
//  * the work happened once per change. An edit that touches twelve clips emits
//    twelve times and rebuilds the same panel twelve times, and eleven of those
//    rebuilds are discarded by the twelfth. Collapsing them costs the caller
//    nothing and is invisible downstream, because a notify signal carries no
//    payload: two of them and one of them mean the same thing.
//
// It is deliberately not a QObject. The emit is supplied as a closure, so the
// signal being deferred stays a normal member signal of a normal class and
// nothing about the QML-visible interface changes. Ordering against other
// deferred work is preserved: GuiDispatch is a queue, not a set of timers.
//
// The key must be a string literal - GuiDispatch compares keys by pointer.
class SignalCoalescer final {
public:
  // `emitter` is run on the GUI thread and must outlive this object's pending
  // state; in practice it captures the owner, which is why the destructor
  // retracts anything still queued.
  SignalCoalescer(const char *key, std::function<void()> emitter);
  ~SignalCoalescer();

  SignalCoalescer(const SignalCoalescer &) = delete;
  SignalCoalescer &operator=(const SignalCoalescer &) = delete;

  // Asks for one notification on a later turn. Repeated calls before that turn
  // collapse into the single pending one. Safe from any thread.
  void schedule();

  // Emits now if something is pending, and clears it. For the cases that cannot
  // tolerate a stale turn - saving a project, or a QML call that reads the
  // property back immediately after writing it.
  void flush();

  // Drops a pending notification without emitting. For when the reason to notify
  // has been superseded by something that will notify anyway.
  void discard();

  bool pending() const;

  // requests: schedule() calls. emissions: notifications actually delivered.
  // collapsed: the difference - bindings that were asked to re-run and did not
  // have to. A high collapsed:emissions ratio is this class earning its keep.
  QVariantMap statistics() const;

private:
  void deliver();

  const char *m_key = nullptr;
  std::function<void()> m_emitter;
  std::atomic_bool m_pending{false};
  std::atomic<quint64> m_requests{0};
  std::atomic<quint64> m_emissions{0};
};
