#pragma once

#include <QString>
#include <QVariantMap>

// Measures how long the GUI thread spends in one trip through its event loop.
//
// Everything measuring responsiveness so far worked at freeze scale. The watchdog
// reports from 400 ms (150 ms with the env var lowered), the crash reporter from
// several seconds. A dropped frame is 17 ms at 60 Hz and 34 ms at 30 fps, so the
// whole of "the video is slow" - the complaint that outlasted every fix - happens
// two orders of magnitude below the smallest thing any of it could see. The logs
// were clean because the instruments were the wrong size, not because the frames
// were arriving.
//
// This is the right size. QAbstractEventDispatcher already emits awake() when the
// GUI thread starts processing and aboutToBlock() when it goes back to waiting;
// the gap between them is exactly one batch of work on the thread that also has
// to paint. A batch longer than a frame interval is a dropped frame, by
// definition, and this reports it with whatever scope was marked.
//
// Cost: two signal receptions per loop iteration and one steady_clock read each,
// on signals Qt emits whether or not anyone is listening. No timer, no thread, no
// allocation on the hot path. That is what makes it safe to leave on in a release
// build, which matters because the stutter shows up on a real machine with real
// 26-hour media and not under a profiler.
//
// Deliberately not a QObject: it holds a lambda connection to the dispatcher and
// nothing else, and staying a plain singleton keeps it usable from
// GuiThreadWatchdog::statistics() without a second registration.
class GuiTurnMonitor final {
public:
  // Connects to this thread's event dispatcher. Call once from main(), on the
  // GUI thread, after the application object exists. Calling twice is a no-op.
  static void install();

  // Merged into the watchdog's map, so everything the diagnostics report and
  // `cutpro --diagnose` already print picks these up with no extra wiring.
  static QVariantMap statistics();

  // One frame at 60 Hz is 16.7 ms. A turn that long has consumed the entire
  // budget for the frame it is inside, so the next one is late whatever the
  // renderer does. Reported from twice that, because a turn between 17 and 33 ms
  // costs a frame only when it lands badly and reporting those would drown the
  // ones that always cost one.
  static constexpr qint64 kBudgetMs = 34;
  // CUTPRO_TURN_BUDGET_MS overrides it; 0 turns the monitor off entirely.
  static qint64 budgetMs();
  // At most one line per this many ms, whatever the turn count. A stutter is
  // hundreds of over-budget turns in a row, and printing each one would itself
  // become the stall - the counters keep the total, the log keeps the shape.
  static constexpr qint64 kLogIntervalMs = 250;
  // Turns at least this long get a line of their own regardless of the rate
  // limit: at 250 ms the user is describing a hitch rather than a stutter, and
  // that is the one they will mention.
  static constexpr qint64 kAlwaysLogMs = 250;
};
