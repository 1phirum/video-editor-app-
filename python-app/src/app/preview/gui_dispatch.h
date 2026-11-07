#pragma once

#include <QVariantMap>

#include <functional>

// Collapses worker-thread notifications so a burst costs one trip through the
// GUI thread instead of one per item.
//
// The preview workers all publish the same way: decode something, then
// QMetaObject::invokeMethod(..., Qt::QueuedConnection) to let the GUI thread
// pick it up. Each of those is an event appended to the GUI thread's queue, and
// the queue is unbounded. A drop on the timeline produces dozens of tile
// results, a scrub produces one per pointer move, and playback produces one per
// frame. When the GUI thread is already busy laying out the timeline it falls
// behind, and every event it fell behind on still has to be delivered - so it
// cannot catch up by dropping work it no longer needs. That is the shape of a
// freeze that has no single slow function in it: nothing blocks, the thread is
// simply never idle again.
//
// Two rules fix that, and both are about the queue rather than the work:
//
//  * posts sharing a key replace each other. Only the newest survives, because
//    for "a frame arrived" or "tiles changed" the newest is the only one whose
//    effect is still wanted;
//  * a drain is time-budgeted. Whatever does not fit in one frame's worth of
//    work is left for the next trip, so a burst can never hold the thread past
//    a repaint.
//
// Keys are string literals compared by pointer, not by content. That keeps the
// hot path free of hashing and allocation, and the compiler's literal pooling
// makes the same key in two translation units still collapse.
class GuiDispatch final {
public:
  // Posts to the GUI thread, discarding any earlier post with this key that has
  // not run yet. Safe from any thread. Called on the GUI thread it still defers:
  // callers rely on the work happening after the current handler returns.
  static void postCoalesced(const char *key, std::function<void()> work);

  // Posts without coalescing, for work where every instance matters.
  static void post(std::function<void()> work);

  // Drops a pending post with this key that has not run yet, and reports whether
  // there was one. An object that captured itself into a deferred notification
  // has to be able to retract it before it goes away, otherwise a queued closure
  // outlives its captures and the drain runs into freed memory. Safe from any
  // thread, but only the thread that will destroy the captures can use it
  // meaningfully: a drain already in flight may be running the very entry being
  // cancelled, so call it from the GUI thread.
  static bool cancel(const char *key);

  // Called once from main() on the GUI thread, before the window is shown.
  static void install();

  // Superseded posts, drains run, and how often a drain hit its budget and had
  // to continue on the next trip. A rising supersededPosts count is the
  // dispatcher doing its job; a rising deferredDrains count means the GUI
  // thread is saturated.
  static QVariantMap statistics();

  // One 60 Hz frame is 16.7 ms. Half of it leaves room for the repaint the
  // drained work exists to cause.
  static constexpr int kDrainBudgetMs = 8;
};
