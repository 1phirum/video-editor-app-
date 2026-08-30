#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <mutex>

// Names the thing that froze the window.
//
// Every round of preview fixes so far has been aimed at a suspect, not at
// evidence: the app is reported as "Not Responding", the likeliest blocking call
// is found by reading, and it gets moved off the GUI thread. That works only for
// as long as the guess is right, and there is no signal at all when it is wrong.
//
// This is the missing signal. The GUI thread publishes a heartbeat from a timer;
// a monitor thread watches the heartbeat's age. When the GUI thread stops
// answering, the monitor logs how long it has been gone and - crucially - what
// it was last seen entering. The next freeze then arrives already attributed
// instead of needing another round of reading.
//
// Cost when nothing is wrong: one 100 ms timer on the GUI thread, one sleeping
// thread, and two relaxed atomic stores per marked scope. That is cheap enough
// to leave compiled into release builds, which matters because the freeze
// reproduces on a user's machine with a user's media, not under a debugger.
class GuiThreadWatchdog final {
public:
  // Marks the GUI thread as being inside a named operation. Only string
  // literals may be passed: the pointer is read from the monitor thread after
  // the scope has already been left, so its target has to outlive everything.
  //
  // Constructing one off the GUI thread is allowed and does nothing, so callers
  // shared between the GUI thread and a worker need no branch of their own.
  //
  // Nested scopes are kept as a stack, not just as an innermost label. Reporting
  // only the innermost one cost several rounds of this investigation: a line
  // reading "inside: Backend::setSelectedClipId/selectionChanged" does not say
  // whether that selection came from a drop, from a click or from project
  // restore, and those have different fixes. The whole chain says it in one line.
  class Scope final {
  public:
    explicit Scope(const char *label);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    int m_depthOnEntry = 0;
    bool m_owned = false;
  };

  static GuiThreadWatchdog &instance();

  // Called once from main(), after the application object exists and before the
  // window is shown. Records the calling thread as the GUI thread and starts
  // both the heartbeat timer and the monitor thread.
  void start();
  void stop();

  // Called once the first window exists. Before it, a stall delays the launch;
  // after it, the same stall is a frozen window with a ghost frame and a
  // "(Not Responding)" title. The reports said the latter either way, which made
  // a slow launch read as the freeze under investigation.
  void markWindowShown();

  // True when called from the thread that called start(). Used by code that has
  // a blocking and a non-blocking path and must pick the safe one; see
  // DecodeWorkGovernor::admit, which refuses to wait on this thread.
  static bool onGuiThread();

  // Names the QML work running right now, for the stalls that report "no marked
  // scope" - which is most of them, because the expensive thing in this app is
  // usually a QML handler instantiating a panel rather than a C++ call.
  //
  // QML has no destructors, so this is not a scope: it labels the current trip
  // through the event loop and clears itself at the end of that trip, which is
  // exactly the lifetime a signal handler or a binding evaluation has. That makes
  // it impossible to leak a stale label, which a begin/end pair exposed to
  // JavaScript would do the first time a handler threw.
  //
  // Labels are interned, so passing a QString is safe even though the monitor
  // thread reads a raw pointer: the interned copy is never freed and never moves.
  // An active C++ Scope wins - it is the more specific attribution.
  static void markTurn(const QString &label);

  // Longest stall seen, how many crossed the reporting threshold, and what the
  // worst one was inside. Merged into Backend::previewDecodeStatistics so the
  // debug overlay can show it without a log file.
  QVariantMap statistics() const;

  // A frame at 60 Hz is 16 ms. A stall under a tenth of a second is not what
  // makes a window look dead, and reporting at that level would bury the real
  // ones in noise from ordinary layout passes.
  static constexpr qint64 kReportThresholdMs = 400;
  // Windows paints the ghost window and appends "(Not Responding)" at about
  // five seconds. Anything past this is already visible to the user.
  static constexpr qint64 kSevereThresholdMs = 2000;
  static constexpr int kHeartbeatIntervalMs = 100;
  // From here on a stall is worth suspending the GUI thread for a moment to find
  // out where it actually is. Sits below the second escalating report at ~800 ms
  // so the first repeat of any real stall carries a backtrace; below it, the
  // marked scopes are enough and the report stays free of side effects.
  static constexpr qint64 kTraceThresholdMs = 600;
  // The threshold actually in force. CUTPRO_STALL_TRACE_MS lowers it for a
  // diagnostic session: a run of 400 ms hitches never reaches the default, so
  // the log names no scope for exactly the stalls a user describes as "slow but
  // not frozen". Clamped to the report threshold below, since a stall that is
  // never reported cannot be traced either.
  static qint64 traceThresholdMs();

private:
  GuiThreadWatchdog() = default;
  ~GuiThreadWatchdog();

  void monitor();
  // One report line for a stall that is still going, plus a backtrace once it is
  // long enough to be worth one. Called repeatedly for the same stall.
  void report(qint64 ageMs, bool severe);

  std::atomic_bool m_running{false};
  std::atomic_bool m_windowShown{false};
  std::atomic<quint64> m_heartbeat{0};
  std::atomic<qint64> m_heartbeatAtMs{0};

  std::atomic<quint64> m_stalls{0};
  std::atomic<quint64> m_severeStalls{0};
  std::atomic<qint64> m_worstStallMs{0};
  std::atomic<const char *> m_worstScope{nullptr};
  std::atomic<quint64> m_traceCaptures{0};

  // Read by statistics() on the GUI thread, written by the monitor. Small enough
  // that a plain mutex beats anything cleverer.
  mutable std::mutex m_worstMutex;
  QString m_worstChain;
  QString m_worstVerdict;
  QStringList m_worstTrace;
};

// Wraps a statement in a named scope. The label is the function it guards, so
// the log line reads as a call site rather than as a description. Two levels of
// indirection because `##` suppresses expansion of its operands: pasting
// __LINE__ directly would produce a variable literally called guiScope__LINE__
// and the second use in a function would not compile.
#define CUTPRO_GUI_SCOPE_PASTE(a, b) a##b
#define CUTPRO_GUI_SCOPE_NAME(line) CUTPRO_GUI_SCOPE_PASTE(cutproGuiScope_, line)
#define CUTPRO_GUI_SCOPE(label)                                                \
  GuiThreadWatchdog::Scope CUTPRO_GUI_SCOPE_NAME(__LINE__)(label)
