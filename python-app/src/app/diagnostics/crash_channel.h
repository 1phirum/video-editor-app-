#pragma once

#include <QString>
#include <QStringList>

#include <cstddef>

// The contract between cutpro.exe and cutpro_crash_report.exe.
//
// Every freeze investigated so far has been reconstructed after the fact, from
// a screenshot of "(Not Responding)" plus whatever a debugger could still be
// attached to by hand. That works once. It does not work on a user's machine,
// it does not work when the process has already been killed, and - as this
// project has now proved several times over - reading the code afterwards
// produces a plausible suspect rather than the actual one.
//
// So the app publishes what it knows into a block of shared memory, and a
// second process watches it. The split matters:
//
//  * a hang cannot be reported by the hung process. Anything the app itself
//    would do at the moment of the freeze has to run on the thread that is
//    wedged, or on a thread that then has to symbolise a suspended stack
//    in-process - which is how a stack sampler deadlocks on the loader lock.
//    An out-of-process watcher has neither problem: it calls MiniDumpWriteDump
//    on a *foreign* process handle and gets every thread's stack, with no
//    cooperation from the target at all;
//  * a crash cannot be reported from inside the crashing process either. By
//    the time an unhandled exception filter runs, the heap may be the thing
//    that is broken. The filter here writes only fixed-size fields into an
//    already-mapped view and signals an event; the reporter does the writing.
//
// The layout is fixed, POD, and versioned. Both sides are built by the same
// compiler in the same build, so no packing pragma is needed - but a stale
// reporter left in a dist folder next to a newer app would otherwise read
// garbage, hence kVersion.
namespace diag {

// "CUTP". Distinguishes our view from any other mapping that happens to be
// openable under a colliding name.
constexpr unsigned int kChannelMagic = 0x43555450u;
// Bump on any field move. The reporter refuses a block it does not recognise.
constexpr unsigned int kChannelVersion = 1u;

constexpr int kTextBytes = 1024;
// Room for ~60 lines of "ClassName count" plus the worst-parent chain. The
// census is the field that names the offending item tree, so it gets the space.
constexpr int kCensusBytes = 8192;

// Written by the app, read by the reporter. Field order is 8-byte members
// first so no member straddles a cache line by accident; the trailing char
// arrays are the only variable-cost part.
struct ChannelData {
  // --- identity ---
  unsigned int magic;
  unsigned int version;
  unsigned int pid;
  unsigned int guiThreadId;

  // --- liveness ---
  // Bumped by the GUI thread's heartbeat timer. The reporter never compares it
  // to its own clock: it compares consecutive samples, so a machine that
  // suspends or a debugger that pauses the app does not read as a hang.
  unsigned long long heartbeat;
  // Milliseconds since the app started, as of the last beat.
  long long heartbeatAtMs;
  long long startedAtMs;
  // Set to 1 by the app just before it returns from main(). The reporter exits
  // silently when it sees this, so a normal quit produces no report.
  unsigned int cleanExit;
  unsigned int windowShown;

  // --- what the watchdog already knows ---
  unsigned int stallReports;
  unsigned int severeStalls;
  long long worstStallMs;

  // --- unhandled exception, filled by the app's last-chance filter ---
  unsigned int exceptionCode;
  unsigned int exceptionThreadId;
  unsigned long long exceptionAddress;
  // EXCEPTION_POINTERS in the *app's* address space. MiniDumpWriteDump reads
  // it out of the target process, which is why passing a pointer across is
  // correct here and would not be anywhere else.
  unsigned long long exceptionPointers;

  // --- text ---
  char appVersion[64];
  // GuiScopeStack::snapshot().chain at the last report.
  char scopeChain[kTextBytes];
  // ...and its verdict ("wedged in" / "churning through").
  char verdict[kTextBytes];
  // ItemTreeCensus, refreshed on a timer. Latin-1, newline separated.
  char census[kCensusBytes];
  long long censusAtMs;
  unsigned int censusItems;
  unsigned int censusTruncated;
};

// Maps the block. One instance per process; both sides use the same class so
// the layout can only ever be described once.
class CrashChannel final {
public:
  // Creates the mapping and the two events, and returns the names to hand to
  // the reporter on its command line. Called once from the app, before
  // anything can stall. False when the platform has no implementation.
  static bool createHost();
  // Opens a mapping the app created. Called by the reporter.
  static bool openClient(const QString &mappingName);
  // The reporter learns the event names from its command line rather than
  // deriving them, so a future change to how they are built cannot leave the
  // two sides waiting on different objects.
  static void setEventNames(const QString &crashEvent,
                            const QString &dumpDoneEvent);

  static bool valid();
  static ChannelData *data();

  // Names passed to the reporter process. Empty until createHost() succeeded.
  static QString mappingName();
  // Set by the app's exception filter: "I am crashing, dump me now".
  static QString crashEventName();
  // Set by the reporter: "the dump is written, you may finish dying". Without
  // it the crashing process races the reporter and usually wins, so the dump
  // is truncated or the target is gone before MiniDumpWriteDump opens it.
  static QString dumpDoneEventName();

  // --- app side ---------------------------------------------------------
  // Copies `text` into a fixed field, truncating rather than allocating: some
  // of these run from an exception filter where the heap is suspect.
  static void setScopeChain(const QString &text);
  static void setVerdict(const QString &text);
  static void setCensus(const QString &text, int items, bool truncated);
  static void noteHeartbeat(unsigned long long beat, long long atMs);
  static void noteStall(unsigned int reports, unsigned int severe,
                        long long worstMs);
  static void markWindowShown();
  static void markCleanExit();
  // Installs the last-chance exception filter that fills the exception fields
  // and signals the crash event. Safe to call more than once.
  static void installExceptionFilter();

  // --- reporter side ----------------------------------------------------
  // Blocks until the app's exception filter signals a crash or `timeoutMs`
  // elapses. Returns true only for a signalled crash.
  static bool waitForCrash(int timeoutMs);
  // Releases the crashing app once its dump has been written.
  static void signalDumpDone();

  static QStringList describe();

private:
  CrashChannel() = delete;
};

} // namespace diag
