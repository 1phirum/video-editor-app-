#pragma once

#include <QString>
#include <QStringList>

// Captures what the GUI thread is executing while it is blocked.
//
// The watchdog can only name scopes that were marked by hand, so every freeze so
// far has been narrowed by adding another CUTPRO_GUI_SCOPE and rebuilding - and
// when the stall turns out to be inside QML rather than inside a call of ours,
// there is nothing to mark and the report says "no marked scope". That is the
// point at which the evidence runs out and the guessing starts again.
//
// This ends that. When the GUI thread has been unresponsive long enough to be a
// real freeze, the monitor thread suspends it, walks its stack, and resumes it.
// The result names the actual frames - QQmlBinding::evaluate,
// QQmlObjectCreator::create, an ffmpeg call, a mutex wait - which is the
// difference between "somewhere in QML" and a call site.
//
// Two properties make this safe enough to leave in a shipping build:
//
//  * nothing is allocated and no symbol API is called while the thread is
//    suspended. The suspended window is a bounded RtlVirtualUnwind loop over at
//    most kMaxFrames frames, reading the target's stack through
//    ReadProcessMemory so a bad frame pointer returns false instead of faulting.
//    Symbolisation happens after the thread is running again. Calling into
//    dbghelp - which allocates, and takes the loader lock - with the GUI thread
//    frozen is the classic way to deadlock a stack sampler, and it is exactly
//    what this avoids;
//  * it only runs at the severe threshold, once per stall. By then the window is
//    already painted over by Windows as "Not Responding", so the trade of a
//    millisecond of suspension for an actual answer is not close.
//
// Set CUTPRO_NO_STALL_TRACE=1 to disable it at run time.
class GuiStallTracer final {
public:
  // Called from the GUI thread, once, before the monitor thread starts. Takes a
  // real thread handle: the pseudo-handle GetCurrentThread() returns is only
  // meaningful to the calling thread, so the monitor could not use it.
  static void rememberGuiThread();

  // False on platforms with no implementation, when the handle was never taken,
  // or when disabled by environment.
  static bool available();

  // One entry per frame, outermost last, formatted for a log line. Frames that
  // cannot be resolved to a symbol are reported as module+0xoffset, which is
  // still enough to locate them with addr2line against the build tree.
  //
  // Must be called from a thread other than the GUI thread. Returns empty if the
  // capture failed, which is not an error worth propagating - the stall report is
  // still printed without it.
  static QStringList captureGuiBacktrace();

  // Deep enough to cross QML's binding and component-creation machinery, which is
  // a dozen frames on its own, and reach the call that actually blocks.
  static constexpr int kMaxFrames = 48;
};
