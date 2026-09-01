#include "app/preview/gui_thread_watchdog.h"

#include <algorithm>

#include "app/diagnostics/crash_channel.h"
#include "app/preview/gui_stall_report.h"
#include "app/preview/gui_stall_tracer.h"
#include "app/preview/gui_turn_monitor.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <thread>

// Warning level on purpose. This category exists to be seen when the window
// freezes on a machine that has no debugger attached, so it must survive the
// default "warnings and above" filter.
Q_LOGGING_CATEGORY(lcGuiWatchdog, "cutpro.gui.watchdog", QtWarningMsg)

namespace {

qint64 monotonicMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

std::atomic<QThread *> g_guiThread{nullptr};
std::thread g_monitorThread;

} // namespace

qint64 GuiThreadWatchdog::reportThresholdMs() {
  // The floor on what this instrument can see, and the reason a stutter can be
  // invisible to it: at 400 ms a stall that drops six frames of a 30 fps preview
  // is under the threshold and never reaches report(), so the log is silent about
  // exactly the range users describe as "slow" rather than "frozen". Lowering it
  // costs log volume and nothing else, so it is a switch rather than a new
  // default.
  static const qint64 threshold = []() -> qint64 {
    const QByteArray raw = qgetenv("CUTPRO_STALL_REPORT_MS").trimmed();
    if (raw.isEmpty())
      return kReportThresholdMs;
    bool ok = false;
    const qint64 value = raw.toLongLong(&ok);
    if (!ok || value <= 0)
      return kReportThresholdMs;
    // Below ~120 ms the heartbeat's own resolution starts to dominate, and every
    // scheduler hiccup on the machine becomes a "stall".
    return qBound<qint64>(Q_INT64_C(120), value, Q_INT64_C(60000));
  }();
  return threshold;
}

int GuiThreadWatchdog::heartbeatIntervalMs() {
  // A stall cannot be measured more finely than the beat that detects it. With
  // the default 400 ms threshold a 100 ms beat is right; asking for 150 ms
  // reporting with a 100 ms beat would report every stall as "150 ms" whatever it
  // really was, so the beat follows the threshold down.
  const qint64 quarter = reportThresholdMs() / 4;
  return static_cast<int>(qBound<qint64>(
      Q_INT64_C(16), quarter, static_cast<qint64>(kHeartbeatIntervalMs)));
}

qint64 GuiThreadWatchdog::traceThresholdMs() {
  // Read once. The monitor thread asks for it on every report, and a stall run
  // is exactly when the process should not be doing environment lookups.
  static const qint64 threshold = []() -> qint64 {
    const QByteArray raw =
        qgetenv("CUTPRO_STALL_TRACE_MS").trimmed();
    if (raw.isEmpty())
      return kTraceThresholdMs;
    bool ok = false;
    const qint64 value = raw.toLongLong(&ok);
    if (!ok || value <= 0)
      return kTraceThresholdMs;
    // A stall below the report threshold never reaches report() at all, so
    // asking to trace at 100 ms would silently do nothing; clamping makes the
    // lowest useful value the lowest accepted one.
    return qMax(value, reportThresholdMs());
  }();
  return threshold;
}

GuiThreadWatchdog::Scope::Scope(const char *label) {
  if (!label || !GuiThreadWatchdog::onGuiThread())
    return;
  m_owned = true;
  m_depthOnEntry = GuiScopeStack::push(label);
}

GuiThreadWatchdog::Scope::~Scope() {
  if (!m_owned)
    return;
  GuiScopeStack::pop(m_depthOnEntry);
}

GuiThreadWatchdog &GuiThreadWatchdog::instance() {
  static GuiThreadWatchdog watchdog;
  return watchdog;
}

GuiThreadWatchdog::~GuiThreadWatchdog() { stop(); }

bool GuiThreadWatchdog::onGuiThread() {
  QThread *gui = g_guiThread.load(std::memory_order_acquire);
  return gui != nullptr && gui == QThread::currentThread();
}

void GuiThreadWatchdog::markTurn(const QString &label) {
  if (label.isEmpty() || !onGuiThread())
    return;
  if (!GuiScopeStack::markTurn(label))
    return;
  const quint64 generation = GuiScopeStack::turnGeneration();

  QObject *context = QCoreApplication::instance();
  if (!context) {
    // No event loop to clear it, so do not leave a label that will never be
    // retired and would mis-attribute every later stall.
    GuiScopeStack::clearTurn(generation);
    return;
  }
  // Queued, so it runs after the handler that marked - and after everything else
  // already queued, which is the point: the label covers the whole turn.
  //
  // A blocked GUI thread cannot run this, which is why GuiScopeStack::snapshot
  // discards a turn label older than the stall rather than trusting the clear.
  QMetaObject::invokeMethod(
      context, [generation]() { GuiScopeStack::clearTurn(generation); },
      Qt::QueuedConnection);
}

void GuiThreadWatchdog::start() {
  if (m_running.exchange(true, std::memory_order_acq_rel))
    return;
  g_guiThread.store(QThread::currentThread(), std::memory_order_release);
  m_heartbeatAtMs.store(monotonicMs(), std::memory_order_release);
  // Has to happen on the GUI thread: the handle it takes is this thread's.
  GuiStallTracer::rememberGuiThread();

  // Parented to the application so it dies with it, and deliberately not a
  // member: the watchdog is a plain singleton with no QObject of its own, which
  // keeps it usable from translation units that never see Qt's event loop.
  auto *beat = new QTimer(QCoreApplication::instance());
  beat->setTimerType(Qt::CoarseTimer);
  beat->setInterval(heartbeatIntervalMs());
  QObject::connect(beat, &QTimer::timeout, beat, [this]() {
    const quint64 tick =
        m_heartbeat.fetch_add(1, std::memory_order_relaxed) + 1;
    const qint64 now = monotonicMs();
    m_heartbeatAtMs.store(now, std::memory_order_release);
    // Published to the out-of-process reporter as well. It compares consecutive
    // samples of this counter rather than measuring against its own clock, which
    // is what lets it tell a real hang from a suspended machine or a debugger
    // that paused the app.
    diag::CrashChannel::noteHeartbeat(tick, now);
  });
  beat->start();

  g_monitorThread = std::thread([this]() { monitor(); });
}

void GuiThreadWatchdog::markWindowShown() {
  // Timestamped, not just flagged. The first stall of a run routinely straddles
  // this moment - the window is created part-way through the QML load and the
  // thread does not yield until the load finishes - and a plain flag read when
  // the monitor samples then says "a window existed", turning four seconds of
  // startup into a reported freeze. Comparing against when the stall began is the
  // only reading that survives that.
  m_windowShownAtMs.store(monotonicMs(), std::memory_order_release);
  m_windowShown.store(true, std::memory_order_relaxed);
}

void GuiThreadWatchdog::stop() {
  if (!m_running.exchange(false, std::memory_order_acq_rel))
    return;
  if (g_monitorThread.joinable())
    g_monitorThread.join();
}

void GuiThreadWatchdog::monitor() {
  quint64 lastSeen = m_heartbeat.load(std::memory_order_relaxed);
  // Escalating rather than once-per-threshold. A single line cannot distinguish a
  // thread wedged in one call from one churning through thousands of cheap ones,
  // and that distinction decides whether the fix is "move it off the thread" or
  // "stop doing it n times".
  qint64 nextReportAtMs = reportThresholdMs();
  bool countedStall = false;
  bool countedSevere = false;

  while (m_running.load(std::memory_order_acquire)) {
    // Half the heartbeat interval: fast enough that the reported duration is
    // close to the real one, slow enough to stay invisible in a profile.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(std::max(8, heartbeatIntervalMs() / 2)));

    const quint64 beat = m_heartbeat.load(std::memory_order_relaxed);
    if (beat != lastSeen) {
      lastSeen = beat;
      nextReportAtMs = reportThresholdMs();
      countedStall = false;
      countedSevere = false;
      continue;
    }

    const qint64 age =
        monotonicMs() - m_heartbeatAtMs.load(std::memory_order_acquire);
    if (age < nextReportAtMs)
      continue;

    if (!countedStall) {
      countedStall = true;
      m_stalls.fetch_add(1, std::memory_order_relaxed);
    }
    const bool severe = age >= kSevereThresholdMs;
    if (severe && !countedSevere) {
      countedSevere = true;
      m_severeStalls.fetch_add(1, std::memory_order_relaxed);
    }
    report(age, severe);

    // Doubling keeps a minute-long freeze down to a handful of lines. The clamp
    // guarantees one report lands close to the threshold Windows itself uses, so
    // the log always contains the "Not Responding" line for a freeze that reached
    // it rather than jumping from 1600 ms to 3200 ms.
    nextReportAtMs = age * 2;
    if (!countedSevere && nextReportAtMs > kSevereThresholdMs)
      nextReportAtMs = kSevereThresholdMs;
  }
}

void GuiThreadWatchdog::report(qint64 ageMs, bool severe) {
  // Judged against the last heartbeat, not against "now": that is the moment the
  // GUI thread was last known to be answering, so a frame entered at or before
  // it was already running when the thread went quiet and a frame entered after
  // it is the thread making progress. Without that split the chain named
  // whatever happened to be on the stack when the monitor looked.
  const qint64 stallBeganMs = m_heartbeatAtMs.load(std::memory_order_acquire);
  const GuiScopeStack::Snapshot scope = GuiScopeStack::snapshot(stallBeganMs);
  const QString verdict = scope.verdict(ageMs);
  const bool worst = ageMs > m_worstStallMs.load(std::memory_order_relaxed);
  if (worst) {
    m_worstStallMs.store(ageMs, std::memory_order_relaxed);
    m_worstScope.store(scope.blocking, std::memory_order_relaxed);
    // Recorded with the stall rather than read back later: by the time anything
    // asks, the window exists, and a launch stall would then be indistinguishable
    // from a freeze a user sat through. This is the only fact that separates
    // "startup took two seconds" from "the app hung".
    //
    // Judged from when the stall began, not from now: a stall that started before
    // the first window and was still running after it is startup cost, however
    // long it went on.
    const qint64 shownAt = m_windowShownAtMs.load(std::memory_order_acquire);
    m_worstBeforeWindow.store(shownAt == 0 || stallBeganMs < shownAt,
                              std::memory_order_relaxed);
  }

  // Published before anything is logged. If this stall never ends - which is the
  // case the reporter exists for - these are the fields its hang report carries,
  // and they have to be in the shared block by the time the reporter looks. The
  // calls are fixed-size copies into an already-mapped page, so doing them from
  // the monitor thread costs nothing and cannot allocate.
  diag::CrashChannel::setScopeChain(scope.chain);
  diag::CrashChannel::setVerdict(verdict);
  diag::CrashChannel::noteStall(
      static_cast<unsigned int>(m_stalls.load(std::memory_order_relaxed)),
      static_cast<unsigned int>(m_severeStalls.load(std::memory_order_relaxed)),
      m_worstStallMs.load(std::memory_order_relaxed));

  if (severe)
    qCWarning(lcGuiWatchdog).nospace()
        << "GUI thread STILL blocked after " << ageMs << " ms, " << verdict
        << ": " << scope.chain
        << (m_windowShown.load(std::memory_order_relaxed)
                ? " - this is the state Windows shows as \"Not Responding\"."
                : " - during launch, before any window exists: this delays "
                  "startup but cannot show as \"Not Responding\".");
  else
    qCWarning(lcGuiWatchdog).nospace()
        << "GUI thread unresponsive for " << ageMs << " ms, " << verdict << ": "
        << scope.chain;

  // The single most diagnostic line available about a stall, and it used to be
  // buried in the chain: work that appears more than once in its own call stack
  // is a notification cascade re-entering itself, which no amount of moving it
  // off the thread will fix.
  if (scope.repeats > 1 && scope.repeated)
    qCWarning(lcGuiWatchdog).nospace()
        << "  re-entrancy: \"" << scope.repeated << "\" appears "
        << scope.repeats
        << " times in its own chain - a notification cascade, not slow work.";
  else if (scope.depth > 0 && !scope.wedged(ageMs))
    qCWarning(lcGuiWatchdog).nospace()
        << "  the innermost frame has only been open " << scope.innermostOpenMs
        << " ms of the " << ageMs
        << " ms stall, so the thread is entering and leaving frames rather than "
           "stuck in one: the fix is to do it fewer times, not to move it off "
           "the thread.";

  if (ageMs < traceThresholdMs())
    return;

  const QStringList frames = GuiStallTracer::captureGuiBacktrace();
  if (frames.isEmpty()) {
    // Only worth a line when the whole mechanism is off, which is a fact about
    // the build rather than about this stall.
    if (!GuiStallTracer::available())
      qCWarning(lcGuiWatchdog)
          << "  (no backtrace available - stall tracing is disabled or "
             "unsupported on this platform)";
    return;
  }

  m_traceCaptures.fetch_add(1, std::memory_order_relaxed);
  qCWarning(lcGuiWatchdog).nospace()
      << "  GUI thread backtrace, innermost first (" << frames.size()
      << " frames):";
  for (int i = 0; i < frames.size(); ++i)
    qCWarning(lcGuiWatchdog).nospace() << "    #" << i << "  " << frames.at(i);

  if (!worst)
    return;
  const std::lock_guard<std::mutex> guard(m_worstMutex);
  m_worstChain = scope.chain;
  m_worstVerdict = verdict;
  m_worstTrace = frames;
}

QVariantMap GuiThreadWatchdog::statistics() const {
  const char *worst = m_worstScope.load(std::memory_order_relaxed);
  QVariantMap stats;
  stats[QStringLiteral("guiWatchdogRunning")] =
      m_running.load(std::memory_order_acquire);
  stats[QStringLiteral("guiStalls")] =
      static_cast<qulonglong>(m_stalls.load(std::memory_order_relaxed));
  stats[QStringLiteral("guiSevereStalls")] =
      static_cast<qulonglong>(m_severeStalls.load(std::memory_order_relaxed));
  stats[QStringLiteral("guiWorstStallMs")] =
      m_worstStallMs.load(std::memory_order_relaxed);
  stats[QStringLiteral("guiWorstStallScope")] =
      worst ? QString::fromLatin1(worst) : QString();
  stats[QStringLiteral("guiWorstStallBeforeWindow")] =
      m_worstBeforeWindow.load(std::memory_order_relaxed);
  stats[QStringLiteral("guiWindowShown")] =
      m_windowShown.load(std::memory_order_relaxed);
  stats[QStringLiteral("guiStallTraces")] =
      static_cast<qulonglong>(m_traceCaptures.load(std::memory_order_relaxed));
  stats[QStringLiteral("guiStallTracerReady")] = GuiStallTracer::available();
  // Which stalls in this run were eligible for a backtrace. Without it, a log
  // full of unattributed 400 ms hitches reads as a tracer failure rather than as
  // the threshold being above them.
  stats[QStringLiteral("guiStallTraceThresholdMs")] = traceThresholdMs();
  // Published so a quiet log can be read correctly: no stalls at a 400 ms
  // threshold and no stalls at a 150 ms one are very different claims.
  stats[QStringLiteral("guiStallReportThresholdMs")] = reportThresholdMs();
  stats[QStringLiteral("guiHeartbeatIntervalMs")] = heartbeatIntervalMs();
  // The chain and the frames of the worst stall, so the debug overlay shows what
  // the log would have said without needing the log.
  {
    const std::lock_guard<std::mutex> guard(m_worstMutex);
    stats[QStringLiteral("guiWorstStallChain")] = m_worstChain;
    stats[QStringLiteral("guiWorstStallVerdict")] = m_worstVerdict;
    stats[QStringLiteral("guiWorstStallTrace")] = m_worstTrace;
  }
  const QVariantMap scopeStats = GuiScopeStack::statistics();
  for (auto it = scopeStats.cbegin(); it != scopeStats.cend(); ++it)
    stats[it.key()] = it.value();
  // The frame-scale half of the same measurement. Merged here rather than wired
  // into the bridge separately: every consumer of this map - the snapshot file,
  // `cutpro --diagnose`, the analyzer - then sees dropped frames without a second
  // registration, and the two halves can never disagree about which run they
  // describe.
  const QVariantMap turnStats = GuiTurnMonitor::statistics();
  for (auto it = turnStats.cbegin(); it != turnStats.cend(); ++it)
    stats[it.key()] = it.value();
  return stats;
}
