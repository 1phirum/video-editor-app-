#include "app/diagnostics/crash_reporter_host.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "app/diagnostics/crash_channel.h"

namespace {

struct HostState {
  bool channelReady = false;
  bool reporterRunning = false;
  qint64 reporterPid = 0;
  QString reporterPath;
  QString failure;
};

HostState &host() {
  static HostState s;
  return s;
}

QString reporterExecutable() {
  const QString directory = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
  const QString name = QStringLiteral("cutpro_crash_report.exe");
#else
  const QString name = QStringLiteral("cutpro_crash_report");
#endif
  return QDir(directory).filePath(name);
}

} // namespace

bool CrashReporterHost::disabledByEnvironment() {
  return qEnvironmentVariableIntValue("CUTPRO_NO_CRASH_REPORT") != 0;
}

QString CrashReporterHost::reportDirectory() {
  QString base = qEnvironmentVariable("CUTPRO_CRASH_DIR");
  if (base.isEmpty()) {
    base = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
      base = QDir::tempPath() + QStringLiteral("/CutPro");
    base += QStringLiteral("/crash-reports");
  }
  return QDir::cleanPath(base);
}

bool CrashReporterHost::start() {
  HostState &s = host();
  if (s.reporterRunning)
    return true;

  s.channelReady = diag::CrashChannel::createHost();
  if (!s.channelReady) {
    s.failure = QStringLiteral("shared channel unavailable");
    qWarning().noquote() << "CrashReporterHost:" << s.failure
                         << "- hangs and crashes will not be reported";
    return false;
  }
  // Installed even when the reporter itself cannot be spawned: the filter also
  // makes the process die deterministically instead of being handed to Windows
  // Error Reporting, and it costs nothing.
  diag::CrashChannel::installExceptionFilter();

  const QString directory = reportDirectory();
  QDir().mkpath(directory);

  if (disabledByEnvironment()) {
    s.failure = QStringLiteral("disabled by CUTPRO_NO_CRASH_REPORT");
    return false;
  }

  s.reporterPath = reporterExecutable();
  if (!QFileInfo::exists(s.reporterPath)) {
    s.failure = QStringLiteral("not found: %1").arg(s.reporterPath);
    qWarning().noquote() << "CrashReporterHost:" << s.failure;
    return false;
  }

  const QStringList arguments{
      QStringLiteral("--pid"),
      QString::number(QCoreApplication::applicationPid()),
      QStringLiteral("--channel"),
      diag::CrashChannel::mappingName(),
      QStringLiteral("--crash-event"),
      diag::CrashChannel::crashEventName(),
      QStringLiteral("--done-event"),
      diag::CrashChannel::dumpDoneEventName(),
      QStringLiteral("--dir"),
      directory,
  };

  // Detached on purpose. A QProcess child is killed by its destructor and dies
  // with an abnormal parent exit, which is exactly the case the reporter has to
  // survive in order to describe.
  qint64 pid = 0;
  const bool started = QProcess::startDetached(
      s.reporterPath, arguments, QCoreApplication::applicationDirPath(), &pid);
  if (!started) {
    s.failure = QStringLiteral("could not launch %1").arg(s.reporterPath);
    qWarning().noquote() << "CrashReporterHost:" << s.failure;
    return false;
  }
  s.reporterRunning = true;
  s.reporterPid = pid;
  s.failure.clear();
  qInfo().noquote() << "CrashReporterHost: watching pid"
                    << QCoreApplication::applicationPid() << "from pid" << pid
                    << "| reports ->" << directory;
  return true;
}

void CrashReporterHost::stop() {
  diag::CrashChannel::markCleanExit();
  host().reporterRunning = false;
}

QStringList CrashReporterHost::existingReports(int limit) {
  QDir directory(reportDirectory());
  if (!directory.exists())
    return {};
  const QFileInfoList entries = directory.entryInfoList(
      {QStringLiteral("*.txt"), QStringLiteral("*.dmp")}, QDir::Files,
      QDir::Time);
  QStringList out;
  for (const QFileInfo &info : entries) {
    if (out.size() >= limit)
      break;
    out << info.absoluteFilePath();
  }
  return out;
}

bool CrashReporterHost::channelReady() { return host().channelReady; }
bool CrashReporterHost::reporterRunning() { return host().reporterRunning; }

QVariantMap CrashReporterHost::statistics() {
  const HostState &s = host();
  QVariantMap map;
  map.insert(QStringLiteral("crashChannelReady"), s.channelReady);
  map.insert(QStringLiteral("crashReporterRunning"), s.reporterRunning);
  map.insert(QStringLiteral("crashReporterPid"), s.reporterPid);
  map.insert(QStringLiteral("crashReportDir"), reportDirectory());
  if (!s.failure.isEmpty())
    map.insert(QStringLiteral("crashReporterProblem"), s.failure);
  return map;
}
