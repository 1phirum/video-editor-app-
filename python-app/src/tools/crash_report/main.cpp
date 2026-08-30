// cutpro_crash_report.exe - the out-of-process hang and crash reporter.
//
// Launched by cutpro.exe at startup (see CrashReporterHost) with the name of a
// shared memory block, the pid to watch and a directory to write into. It then
// does three things, none of which the app can do for itself:
//
//  * watches the GUI thread's heartbeat. When it stops for longer than
//    --hang-ms, it writes a text report - carrying the scope chain, the verdict
//    and the item-tree census the app had already published - and a minidump of
//    every thread's stack, taken from outside the frozen process;
//  * waits on the crash event the app's last-chance exception filter sets. That
//    filter blocks until this process says the dump is written, which is the
//    only way to get a dump of a process that is in the middle of dying;
//  * notices the app vanishing without either. That is what a task-kill or a
//    Windows Error Reporting takedown looks like, and saying so is better than
//    the silence this used to produce.
//
// A clean quit sets a flag in the block; this process sees it, writes nothing,
// and exits. So a normal session leaves no reports behind.
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QThread>

#include "app/diagnostics/crash_channel.h"
#include "tools/crash_report/minidump_writer.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

struct Options {
  qint64 pid = 0;
  QString channel;
  QString directory;
  // Windows paints "(Not Responding)" at about five seconds. Twelve is past
  // every legitimate stall this app has (project load, a long export starting)
  // and still early enough to catch a runaway item tree while it is growing.
  int hangMs = 12000;
  int pollMs = 250;
  bool fullMemory = false;
};

QString stamp() {
  return QDateTime::currentDateTime().toString(
      QStringLiteral("yyyyMMdd-HHmmss"));
}

void logLine(const Options &options, const QString &text) {
  QFile file(QDir(options.directory).filePath(QStringLiteral("reporter.log")));
  if (!file.open(QIODevice::Append | QIODevice::Text))
    return;
  QTextStream stream(&file);
  stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "  "
         << text << '\n';
}

// Keeps the directory from growing without bound. Reports are small text files
// plus dumps that are tens of megabytes each, so the dumps are what this is
// really for.
void pruneOldReports(const Options &options, int keepFiles) {
  QDir directory(options.directory);
  const QFileInfoList entries = directory.entryInfoList(
      {QStringLiteral("cutpro-*.txt"), QStringLiteral("cutpro-*.dmp")},
      QDir::Files, QDir::Time);
  for (int i = keepFiles; i < entries.size(); ++i)
    QFile::remove(entries.at(i).absoluteFilePath());
}

QStringList channelReport() {
  QStringList lines = diag::CrashChannel::describe();
  const diag::ChannelData *block = diag::CrashChannel::data();
  if (!block)
    return lines;
  lines << QString();
  lines << QStringLiteral("--- item tree census (age %1 ms at last sample) ---")
               .arg(block->censusAtMs);
  if (block->census[0])
    lines << QString::fromLatin1(block->census);
  else
    lines << QStringLiteral("(no sample was published)");
  return lines;
}

QString writeTextReport(const Options &options, const QString &kind,
                        const QStringList &body) {
  const QString path = QDir(options.directory)
                           .filePath(QStringLiteral("cutpro-%1-%2.txt")
                                         .arg(kind, stamp()));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return QString();
  QTextStream stream(&file);
  stream << "Cut Pro " << kind << " report\n";
  stream << "when           "
         << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
  stream << "watched pid    " << options.pid << '\n';
  stream << "reporter pid   " << QCoreApplication::applicationPid() << '\n';
  for (const QString &line : body)
    stream << line << '\n';
  stream << '\n';
  for (const QString &line : channelReport())
    stream << line << '\n';
  return path;
}

#ifdef Q_OS_WIN

void showCrashDialog(const QString &reportPath) {
  if (qEnvironmentVariableIntValue("CUTPRO_CRASH_SILENT") != 0)
    return;
  const QString text =
      QStringLiteral("Cut Pro closed unexpectedly.\n\nA report was saved to:\n"
                     "%1\n\nSend this file with a description of what you were "
                     "doing.")
          .arg(reportPath.isEmpty() ? QStringLiteral("(could not be written)")
                                    : reportPath);
  MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(text.utf16()),
              L"Cut Pro", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

QString writeDump(const Options &options, HANDLE process, const QString &kind) {
  const diag::ChannelData *block = diag::CrashChannel::data();
  crashreport::DumpRequest request;
  request.processHandle = process;
  request.processId = static_cast<unsigned int>(options.pid);
  request.fullMemory = options.fullMemory;
  request.path = QDir(options.directory)
                     .filePath(QStringLiteral("cutpro-%1-%2.dmp")
                                   .arg(kind, stamp()));
  if (block && kind == QLatin1String("crash")) {
    request.exceptionThreadId = block->exceptionThreadId;
    request.exceptionPointers = block->exceptionPointers;
  }
  const crashreport::DumpResult result = crashreport::writeDump(request);
  if (!result.ok) {
    logLine(options, QStringLiteral("dump failed: %1").arg(result.error));
    return QString();
  }
  logLine(options, QStringLiteral("dump written: %1 (%2 bytes)")
                       .arg(request.path)
                       .arg(result.bytes));
  return request.path;
}

#endif

Options parse(const QCoreApplication &app) {
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Cut Pro hang and crash reporter"));
  QCommandLineOption pid(QStringLiteral("pid"),
                         QStringLiteral("Process to watch."),
                         QStringLiteral("pid"));
  QCommandLineOption channel(QStringLiteral("channel"),
                             QStringLiteral("Shared memory name."),
                             QStringLiteral("name"));
  QCommandLineOption crashEvent(QStringLiteral("crash-event"),
                                QStringLiteral("Event the app sets to ask for "
                                               "a dump."),
                                QStringLiteral("name"));
  QCommandLineOption doneEvent(QStringLiteral("done-event"),
                               QStringLiteral("Event set once the dump is "
                                              "written."),
                               QStringLiteral("name"));
  QCommandLineOption directory(QStringLiteral("dir"),
                               QStringLiteral("Where reports are written."),
                               QStringLiteral("path"));
  QCommandLineOption hang(QStringLiteral("hang-ms"),
                          QStringLiteral("Heartbeat age that counts as a "
                                         "hang."),
                          QStringLiteral("ms"));
  QCommandLineOption full(QStringLiteral("full-memory"),
                          QStringLiteral("Dump the whole address space."));
  parser.addOptions({pid, channel, crashEvent, doneEvent, directory, hang,
                     full});
  parser.addHelpOption();
  parser.process(app);

  Options options;
  options.pid = parser.value(pid).toLongLong();
  options.channel = parser.value(channel);
  options.directory = parser.value(directory);
  if (options.directory.isEmpty())
    options.directory = QDir::tempPath() + QStringLiteral("/CutPro");
  if (parser.isSet(hang)) {
    const int value = parser.value(hang).toInt();
    if (value >= 1000)
      options.hangMs = value;
  }
  options.fullMemory = parser.isSet(full);
  // The channel class holds the event names for both sides, so they only have
  // to be parsed into it once.
  diag::CrashChannel::setEventNames(parser.value(crashEvent),
                                    parser.value(doneEvent));
  return options;
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("Cut Pro Crash Report"));
  const Options options = parse(app);
  QDir().mkpath(options.directory);

  if (options.pid <= 0 || options.channel.isEmpty()) {
    logLine(options, QStringLiteral("no target given; exiting"));
    return 2;
  }
  if (!diag::CrashChannel::openClient(options.channel)) {
    logLine(options, QStringLiteral("cannot open channel %1 (version "
                                    "mismatch or app already gone)")
                         .arg(options.channel));
    return 3;
  }

#ifndef Q_OS_WIN
  logLine(options, QStringLiteral("no platform support; exiting"));
  return 0;
#else
  HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                                   SYNCHRONIZE,
                               FALSE, static_cast<DWORD>(options.pid));
  if (!process) {
    logLine(options, QStringLiteral("cannot open pid %1 (error %2)")
                         .arg(options.pid)
                         .arg(GetLastError()));
    return 4;
  }
  logLine(options, QStringLiteral("watching pid %1, hang threshold %2 ms")
                       .arg(options.pid)
                       .arg(options.hangMs));

  const diag::ChannelData *block = diag::CrashChannel::data();
  QElapsedTimer clock;
  clock.start();

  unsigned long long lastBeat = block->heartbeat;
  qint64 lastBeatAt = clock.elapsed();
  bool hangReported = false;
  int hangs = 0;

  for (;;) {
    // Doubles as the poll interval: a crash wakes this immediately, and
    // nothing else in the loop needs finer resolution than one tick.
    if (diag::CrashChannel::waitForCrash(options.pollMs)) {
      QStringList body;
      body << QStringLiteral("reason         unhandled exception in the app");
      if (hangs > 0)
        body << QStringLiteral("note           %1 hang(s) were reported first")
                    .arg(hangs);
      const QString dump = writeDump(options, process, QStringLiteral("crash"));
      if (!dump.isEmpty())
        body << QStringLiteral("minidump       %1").arg(dump);
      const QString report =
          writeTextReport(options, QStringLiteral("crash"), body);
      // Only now may the app finish dying.
      diag::CrashChannel::signalDumpDone();
      pruneOldReports(options, 40);
      showCrashDialog(report);
      break;
    }

    if (block->cleanExit) {
      logLine(options, QStringLiteral("app exited cleanly; nothing to report"));
      break;
    }

    if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
      DWORD code = 0;
      GetExitCodeProcess(process, &code);
      if (code == 0 && hangs == 0) {
        logLine(options, QStringLiteral("app exited with code 0"));
        break;
      }
      QStringList body;
      body << QStringLiteral("reason         app vanished without reporting");
      body << QStringLiteral("exit code      0x%1 (%2)")
                  .arg(code, 8, 16, QLatin1Char('0'))
                  .arg(static_cast<int>(code));
      body << QStringLiteral("note           a task-kill of a frozen window, a "
                             "Windows Error Reporting takedown, or a crash "
                             "before the filter was installed all look like "
                             "this");
      if (hangs > 0)
        body << QStringLiteral("note           %1 hang(s) were reported first")
                    .arg(hangs);
      writeTextReport(options, QStringLiteral("exit"), body);
      pruneOldReports(options, 40);
      break;
    }

    const unsigned long long beat = block->heartbeat;
    if (beat != lastBeat) {
      if (hangReported) {
        logLine(options,
                QStringLiteral("GUI thread answered again after %1 ms")
                    .arg(clock.elapsed() - lastBeatAt));
      }
      lastBeat = beat;
      lastBeatAt = clock.elapsed();
      hangReported = false;
      continue;
    }

    // Before the first window exists a stall is a slow launch, not a freeze,
    // and reporting it would bury the real ones.
    if (!block->windowShown || hangReported)
      continue;
    const qint64 age = clock.elapsed() - lastBeatAt;
    if (age < options.hangMs)
      continue;

    hangReported = true;
    ++hangs;
    QStringList body;
    body << QStringLiteral("reason         GUI thread stopped answering for %1 "
                           "ms")
                .arg(age);
    const QString dump = writeDump(options, process, QStringLiteral("hang"));
    if (!dump.isEmpty())
      body << QStringLiteral("minidump       %1").arg(dump);
    const QString report =
        writeTextReport(options, QStringLiteral("hang"), body);
    logLine(options, QStringLiteral("hang report: %1").arg(report));
    pruneOldReports(options, 40);
  }

  CloseHandle(process);
  return 0;
#endif
}
