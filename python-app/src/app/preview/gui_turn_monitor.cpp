#include "app/preview/gui_turn_monitor.h"

#include "app/preview/gui_stall_report.h"

#include <QAbstractEventDispatcher>
#include <QByteArray>
#include <QLoggingCategory>
#include <QThread>
#include <QtGlobal>

#include <atomic>
#include <chrono>

// Same category and same level as the watchdog: this is the small end of the same
// measurement, and a user reproducing a stutter should not have to know that two
// different switches turn on the two halves of one instrument.
Q_LOGGING_CATEGORY(lcGuiTurns, "cutpro.gui.turns", QtWarningMsg)

namespace {

qint64 monotonicMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

struct TurnState {
  bool installed = false;
  qint64 turnStartedAtMs = 0;
  // Not atomic: written and read on the GUI thread only. statistics() is called
  // from QML, which is that thread, and the monitor thread does not touch these.
  quint64 turns = 0;
  quint64 overBudget = 0;
  qint64 worstMs = 0;
  qint64 totalOverBudgetMs = 0;
  QString worstChain;
  qint64 lastLogAtMs = 0;
  // Suppressed since the last line, so the log can say what it did not print
  // rather than quietly dropping it.
  quint64 suppressed = 0;
};

TurnState &state() {
  static TurnState s;
  return s;
}

} // namespace

qint64 GuiTurnMonitor::budgetMs() {
  static const qint64 budget = []() -> qint64 {
    const QByteArray raw = qgetenv("CUTPRO_TURN_BUDGET_MS").trimmed();
    if (raw.isEmpty())
      return kBudgetMs;
    bool ok = false;
    const qint64 value = raw.toLongLong(&ok);
    if (!ok || value < 0)
      return kBudgetMs;
    if (value == 0)
      return 0; // off
    // Below 8 ms every ordinary layout pass is "over budget" and the log stops
    // distinguishing anything.
    return qBound<qint64>(Q_INT64_C(8), value, Q_INT64_C(10000));
  }();
  return budget;
}

void GuiTurnMonitor::install() {
  TurnState &s = state();
  if (s.installed)
    return;
  if (budgetMs() == 0)
    return;
  QAbstractEventDispatcher *dispatcher = QAbstractEventDispatcher::instance();
  if (!dispatcher)
    return;
  s.installed = true;
  s.turnStartedAtMs = monotonicMs();

  // awake() is "the thread has stopped waiting and is about to process";
  // aboutToBlock() is "it has run out of work and is going back to waiting". The
  // gap is the work. Both are emitted by the dispatcher this thread owns, so both
  // handlers run on the GUI thread and can read the scope stack directly - no
  // lock, no cross-thread staleness.
  QObject::connect(dispatcher, &QAbstractEventDispatcher::awake, dispatcher,
                   []() { state().turnStartedAtMs = monotonicMs(); });

  QObject::connect(
      dispatcher, &QAbstractEventDispatcher::aboutToBlock, dispatcher, []() {
        TurnState &s = state();
        const qint64 now = monotonicMs();
        const qint64 elapsed = now - s.turnStartedAtMs;
        ++s.turns;
        // Guard against a clock that went backwards and against the first
        // aboutToBlock, which has no matching awake.
        if (elapsed <= 0 || elapsed < GuiTurnMonitor::budgetMs())
          return;

        ++s.overBudget;
        s.totalOverBudgetMs += elapsed;
        // Judged from the start of the turn: a scope entered before it was
        // already running, which is the same rule the stall reports use.
        const GuiScopeStack::Snapshot scope =
            GuiScopeStack::snapshot(s.turnStartedAtMs);
        const QString chain =
            scope.chain.isEmpty() ? QStringLiteral("unmarked") : scope.chain;
        if (elapsed > s.worstMs) {
          s.worstMs = elapsed;
          s.worstChain = chain;
        }

        const bool always = elapsed >= GuiTurnMonitor::kAlwaysLogMs;
        if (!always &&
            now - s.lastLogAtMs < GuiTurnMonitor::kLogIntervalMs) {
          ++s.suppressed;
          return;
        }
        // Frames, not milliseconds, is the unit the complaint is in: "the video
        // is slow" is dropped frames, and a turn of 84 ms means the next two
        // 30 fps frames could not be presented no matter how fast the decoder
        // was.
        const qint64 frames30 = elapsed / 33;
        qCWarning(lcGuiTurns).nospace()
            << "GUI turn " << elapsed << " ms (budget "
            << GuiTurnMonitor::budgetMs() << " ms, ~" << frames30
            << " frames of 30 fps lost): " << chain
            << (s.suppressed
                    ? QStringLiteral(" [+%1 more since the last line]")
                          .arg(s.suppressed)
                    : QString());
        s.suppressed = 0;
        s.lastLogAtMs = now;
      });

  qCWarning(lcGuiTurns).nospace()
      << "GUI turn monitor on, budget " << budgetMs()
      << " ms - every event-loop turn longer than this is reported with its "
         "marked scope (CUTPRO_TURN_BUDGET_MS=0 turns it off)";
}

QVariantMap GuiTurnMonitor::statistics() {
  const TurnState &s = state();
  QVariantMap map;
  map.insert(QStringLiteral("guiTurnMonitorOn"), s.installed);
  map.insert(QStringLiteral("guiTurnBudgetMs"), budgetMs());
  map.insert(QStringLiteral("guiTurns"), static_cast<qulonglong>(s.turns));
  map.insert(QStringLiteral("guiTurnsOverBudget"),
             static_cast<qulonglong>(s.overBudget));
  map.insert(QStringLiteral("guiWorstTurnMs"), s.worstMs);
  map.insert(QStringLiteral("guiWorstTurnScope"), s.worstChain);
  // The number that says whether the stutter is the GUI thread at all: total time
  // the thread spent in turns it should have finished sooner. Small next to the
  // session length means the frames are being lost somewhere else.
  map.insert(QStringLiteral("guiTurnOverBudgetTotalMs"), s.totalOverBudgetMs);
  return map;
}
