#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>

// The scope stack the GUI thread publishes, and the judgement made about it.
//
// Split out of GuiThreadWatchdog because they are two concerns that were sharing
// one file: the watchdog measures *whether* the GUI thread is answering, and this
// measures *what it was doing*. Keeping the second one here is what lets a report
// say something the watchdog could not.
//
// Three things the reports got wrong, all of them visible in captured logs:
//
//  * no staleness. report() printed the chain as it stood at report time, not as
//    it stood when the thread went silent, and it drew no distinction between a
//    frame that had been open the whole time and one entered a millisecond ago.
//    Printed identically, those read the same - which is how "Backend::
//    warmAudioOutput > FfmpegPreviewDecoder::warmAudioOutput" came to be printed
//    next to a backtrace sitting in QQuickWindowPrivate::polishItems and
//    QRasterPaintEngine::fill. Neither line was wrong; together they were
//    unreadable, because nothing said which frame was load-bearing.
//
//  * markTurn's clear is a queued event, so it cannot possibly run while the GUI
//    thread is blocked - the exact situation the label exists for. A QML label
//    posted before a freeze survived the whole freeze and was reprinted on every
//    escalating report of it. The entry timestamp was recorded and never read.
//
//  * re-entrancy was invisible. The 72-second drop freeze was one cascade
//    nested inside itself six or seven levels deep; a chain containing the same
//    label twice is the single most diagnostic fact available about a stall, and
//    it was being printed as nothing more than a long line.
class GuiScopeStack final {
public:
  // How deep the chain may get. Real nesting in this app is three or four; the
  // rest is headroom so a recursive path truncates instead of writing past the
  // array.
  static constexpr int kMaxDepth = 24;

  // Pushes a frame and returns the depth to restore on pop. Only string literals
  // may be passed: the monitor thread reads the pointer after the frame is gone,
  // so its target has to outlive the process.
  static int push(const char *label);
  static void pop(int depthOnEntry);

  // Labels the current trip through the event loop, for QML work that has no
  // destructor to pop with. Interned, so a QString is safe to pass. Returns
  // false when a C++ frame is already active and is the better attribution.
  static bool markTurn(const QString &label);
  // Retires a turn label if it is still the one `generation` installed.
  static void clearTurn(quint64 generation);
  static quint64 turnGeneration();

  // What the GUI thread is inside, judged against the moment it went quiet.
  // Every field is a best-effort snapshot read without a lock: a stall report
  // must never be able to block the thread it is describing, and a missing frame
  // is a better outcome than a mutex on the hot path.
  struct Snapshot {
    // Outermost first, in call order.
    QString chain;
    // Innermost frame still open, which is the one to blame. Empty when nothing
    // was marked.
    const char *blocking = nullptr;
    int depth = 0;
    // How long the innermost frame has been open. This is the number that
    // separates the two cases: a frame open for essentially the whole stall is
    // the thread wedged inside one call, while a young innermost frame means the
    // thread has been entering and leaving frames all along and the fix is to
    // stop doing it n times rather than to move it off the thread.
    qint64 innermostOpenMs = 0;
    // Frames entered after the thread went quiet. Informative, but deliberately
    // not the verdict: at startup the whole chain postdates the last heartbeat
    // and is still wedged.
    int enteredDuringStall = 0;
    // Highest number of times any single label appears in the chain. Two or more
    // means the work re-entered itself.
    int repeats = 0;
    const char *repeated = nullptr;

    // "wedged in", "churning through" or "stalled somewhere unmarked" - the
    // distinction that decides whether the fix is "move it off the thread" or
    // "stop doing it n times".
    QString verdict(qint64 ageMs) const;
    // A frame open for at least this share of the stall counts as the whole
    // stall. Not 1.0: the monitor samples every 50 ms and the frame may have been
    // entered a few milliseconds into the stall.
    bool wedged(qint64 ageMs) const {
      return depth > 0 && innermostOpenMs * 5 >= ageMs * 4;
    }
  };

  // `stallBeganMs` is the age-zero point: the last moment the GUI thread was
  // known to be answering. Frames entered at or before it were already running.
  static Snapshot snapshot(qint64 stallBeganMs);

  // Counters for the debug overlay: how many turn labels were interned, and how
  // many pushes were dropped for exceeding kMaxDepth.
  static QVariantMap statistics();
};
