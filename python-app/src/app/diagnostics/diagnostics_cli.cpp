#include "app/diagnostics/diagnostics_cli.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>
#include <QtGlobal>

#include <cstdio>

#include "app/diagnostics/crash_reporter_host.h"
#include "app/diagnostics/diagnostics_bridge.h"

namespace {

// stdout rather than qWarning() for the CLI half. A report that goes through the
// logging category picks up the timestamp pattern on every line, which makes the
// measurement table unreadable and unpasteable.
QTextStream &out() {
  static QTextStream stream(stdout);
  return stream;
}

QString newestReport() {
  const QStringList reports = CrashReporterHost::existingReports(1);
  return reports.isEmpty() ? QString() : reports.first();
}

QString usage() {
  return QStringLiteral(
      "Cut Pro diagnostics, command line\n"
      "\n"
      "  cutpro --diagnose          print the newest report or snapshot\n"
      "  cutpro --diagnose-list     list the reports on disk, newest first\n"
      "  cutpro --diagnose-help     this text\n"
      "\n"
      "In a running editor:\n"
      "  Ctrl+Shift+D               print the whole diagnosis to the console\n"
      "                             and write it next to the crash reports\n"
      "\n"
      "Environment:\n"
      "  CUTPRO_DIAGNOSE_MS=n       print the one-line verdict every n ms\n"
      "  CUTPRO_TURN_BUDGET_MS=n    report event-loop turns over n ms, the\n"
      "                             dropped-frame instrument (default 34, 0 off)\n"
      "  CUTPRO_PLAYBACK_TRACE=1    record every playback state change\n"
      "  CUTPRO_STALL_REPORT_MS=n   report GUI stalls from n ms up (default 400)\n"
      "  CUTPRO_STALL_TRACE_MS=n    capture a backtrace from n ms up\n"
      "  CUTPRO_CENSUS_MS=n         walk the item tree every n ms (default off)\n");
}

} // namespace

void DiagnosticsCli::print(const QString &text) {
  out() << text << '\n';
  out().flush();
}

bool DiagnosticsCli::handleCommandLine(const QStringList &arguments,
                                       int *exitCode) {
  const bool wantsReport = arguments.contains(QStringLiteral("--diagnose"));
  const bool wantsList = arguments.contains(QStringLiteral("--diagnose-list"));
  const bool wantsHelp = arguments.contains(QStringLiteral("--diagnose-help"));
  if (!wantsReport && !wantsList && !wantsHelp)
    return false;
  if (exitCode)
    *exitCode = 0;

  if (wantsHelp) {
    print(usage());
    return true;
  }

  const QString directory = CrashReporterHost::reportDirectory();
  if (wantsList) {
    const QStringList reports = CrashReporterHost::existingReports(40);
    print(QStringLiteral("reports in %1").arg(directory));
    if (reports.isEmpty()) {
      print(QStringLiteral(
          "  none - nothing has crashed, hung, or been snapshotted"));
      return true;
    }
    for (const QString &path : reports) {
      const QFileInfo info(path);
      print(QStringLiteral("  %1  %2 KB  %3")
                .arg(info.lastModified().toString(
                         QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                     QString::number((info.size() + 1023) / 1024),
                     info.fileName()));
    }
    return true;
  }

  const QString newest = newestReport();
  if (newest.isEmpty()) {
    // Not an error: it is the normal state of a healthy install, and saying so
    // beats an empty stdout that reads as a broken command.
    print(QStringLiteral("no reports in %1").arg(directory));
    print(QStringLiteral(
        "run the editor, press Ctrl+Shift+D, then repeat this command"));
    return true;
  }
  QFile file(newest);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    print(QStringLiteral("cannot read %1").arg(newest));
    if (exitCode)
      *exitCode = 1;
    return true;
  }
  print(QStringLiteral("--- %1 ---").arg(newest));
  QTextStream reader(&file);
  print(reader.readAll().trimmed());
  return true;
}

QString DiagnosticsCli::printReport(DiagnosticsBridge *bridge,
                                    const QString &note) {
  if (!bridge)
    return QString();
  // The file first. It samples the census, and printing the report before that
  // sample would put a staler scene on the console than in the file for the same
  // keypress - two different answers to one question.
  const QString path = bridge->writeSnapshot(note);
  print(QString());
  print(bridge->fullReport(note));
  if (!path.isEmpty())
    print(QStringLiteral("\nwrote %1").arg(path));
  print(QStringLiteral("read it later with: cutpro --diagnose"));
  return path;
}

void DiagnosticsCli::startTicker(DiagnosticsBridge *bridge, QObject *owner) {
  if (!bridge)
    return;
  bool ok = false;
  const int requested = qgetenv("CUTPRO_DIAGNOSE_MS").trimmed().toInt(&ok);
  if (!ok || requested <= 0)
    return;
  const int interval = qMax(kMinTickMs, requested);

  auto *timer = new QTimer(owner ? owner : nullptr);
  // Coarse on purpose: this timer must never be the reason a frame is late, and
  // a verdict printed 20 ms off schedule is the same verdict.
  timer->setTimerType(Qt::VeryCoarseTimer);
  QObject::connect(timer, &QTimer::timeout, owner ? owner : timer, [bridge]() {
    // Counters only - verdict() reads the published statistics and the playback
    // history, neither of which walks the scene. That is the difference between
    // this and the 2 s census that used to run here.
    DiagnosticsCli::print(QStringLiteral("[diagnose] %1").arg(bridge->verdict()));
  });
  timer->start(interval);
  print(QStringLiteral("[diagnose] verdict every %1 ms; Ctrl+Shift+D for the "
                       "full report")
            .arg(interval));
}
