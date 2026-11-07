#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Starts and owns the out-of-process crash/hang reporter.
//
// Premiere ships AdobeCrashReport.exe beside Adobe Premiere Pro.exe for the same
// reason this exists: the process that is dying or wedged is the worst possible
// place to write a report about it. A hang cannot be described by the thread
// that is hung, and a crash filter running on a corrupted heap cannot be trusted
// to allocate a string, open a file and format a stack.
//
// So cutpro.exe publishes a small fixed block of shared memory (see
// diag::CrashChannel) and launches cutpro_crash_report.exe with the block's
// name and its own pid. From then on the two are independent: the reporter
// watches the heartbeat, and when it stops - or when the app signals its last
// chance filter - it writes a minidump of the app *from outside*, plus a text
// report carrying the scope chain, the item census and the guarded-model table
// that the app had already published.
//
// Failure to start the reporter is never fatal. A missing or blocked
// cutpro_crash_report.exe logs one line and the editor runs exactly as before.
class CrashReporterHost final {
public:
  // Creates the channel, installs the last-chance exception filter and spawns
  // the reporter. Call once from main(), on the GUI thread, before the window
  // exists. Returns false when the reporter could not be started; the channel
  // may still be live, which is why statistics() reports the two separately.
  static bool start();

  // Marks a clean exit so the reporter finishes silently, then lets it go. Does
  // not wait for the child: it exits on its own once it sees the flag or the
  // process handle signals.
  static void stop();

  // Where reports are written. CUTPRO_CRASH_DIR overrides; otherwise
  // <AppLocalData>/crash-reports.
  static QString reportDirectory();

  // Reports already on disk, newest first, at most `limit`.
  static QStringList existingReports(int limit = 20);

  static bool channelReady();
  static bool reporterRunning();
  static QVariantMap statistics();

  // Set CUTPRO_NO_CRASH_REPORT=1 to skip spawning the reporter. The channel is
  // still created - it costs one page and the debug overlay reads it.
  static bool disabledByEnvironment();

private:
  CrashReporterHost() = delete;
};
