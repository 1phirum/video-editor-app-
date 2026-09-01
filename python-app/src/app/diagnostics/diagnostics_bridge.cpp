#include "app/diagnostics/diagnostics_bridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QtGlobal>

#include "app/diagnostics/crash_channel.h"
#include "app/diagnostics/diagnostic_analyzer.h"
#include "app/diagnostics/crash_reporter_host.h"
#include "app/diagnostics/diagnostics_cli.h"
#include "app/diagnostics/item_tree_census.h"
#include "app/diagnostics/model_guard.h"
#include "app/diagnostics/playback_trace.h"
#include "app/preview/gui_thread_watchdog.h"

namespace {

void merge(QVariantMap &into, const QVariantMap &from) {
  for (auto it = from.cbegin(); it != from.cend(); ++it)
    into.insert(it.key(), it.value());
}

} // namespace

DiagnosticsBridge::DiagnosticsBridge(QObject *parent) : QObject(parent) {}

void DiagnosticsBridge::attach(QQmlApplicationEngine *engine,
                               int censusIntervalMs) {
  m_engine = engine;
  ItemTreeCensus::startSampling(engine, censusIntervalMs);
  if (censusIntervalMs > 0)
    return;

  // One baseline walk when periodic sampling is off, so a crash or hang report
  // still carries an item histogram instead of "census: nothing sampled". Late
  // enough to be after the panels have built and outside the launch stall, and
  // once only - the cost of the repeating version is what took it out.
  QTimer::singleShot(6000, engine, []() { ItemTreeCensus::sampleNow(); });
}

QVariantMap DiagnosticsBridge::statistics() const {
  QVariantMap map = GuiThreadWatchdog::instance().statistics();
  merge(map, ItemTreeCensus::statistics());
  merge(map, ModelGuard::instance().statistics());
  merge(map, CrashReporterHost::statistics());
  // Whether the playback trace is on, as a fact separate from whether it has
  // recorded anything. An empty history means "the env var was not set" or
  // "nothing has been played", and the two call for opposite advice.
  map.insert(QStringLiteral("playbackTraceEnabled"), PlaybackTrace::enabled());
  map.insert(QStringLiteral("playbackTraceEvents"),
             PlaybackTrace::instance().history(4096).size());
  return map;
}

QVariantMap DiagnosticsBridge::sampleCensus() {
  const ItemTreeCensus::Result result = ItemTreeCensus::sampleNow();
  QVariantMap map;
  map.insert(QStringLiteral("items"), result.items);
  map.insert(QStringLiteral("maxDepth"), result.maxDepth);
  map.insert(QStringLiteral("truncated"), result.truncated);
  map.insert(QStringLiteral("worstParentPath"), result.worstParentPath);
  map.insert(QStringLiteral("worstParentChildren"), result.worstParentChildren);
  map.insert(QStringLiteral("elapsedUs"),
             static_cast<qint64>(result.elapsedUs));
  QVariantList classes;
  for (const ItemTreeCensus::Entry &entry : result.byClass) {
    QVariantMap item;
    item.insert(QStringLiteral("className"), entry.className);
    item.insert(QStringLiteral("count"), entry.count);
    item.insert(QStringLiteral("samplePath"), entry.samplePath);
    classes.append(item);
  }
  map.insert(QStringLiteral("byClass"), classes);
  Q_EMIT statisticsChanged();
  return map;
}

QString DiagnosticsBridge::censusText() const {
  return ItemTreeCensus::last().text();
}

QString DiagnosticsBridge::modelReport() const {
  return ModelGuard::instance().report();
}

QString DiagnosticsBridge::reportDirectory() const {
  return CrashReporterHost::reportDirectory();
}

QStringList DiagnosticsBridge::reports(int limit) const {
  return CrashReporterHost::existingReports(limit);
}

QString DiagnosticsBridge::playbackTrace(int maxEntries) const {
  const QStringList lines = PlaybackTrace::instance().history(maxEntries);
  if (lines.isEmpty())
    return PlaybackTrace::enabled()
               ? QStringLiteral("no playback events yet")
               : QStringLiteral(
                     "playback trace off (set CUTPRO_PLAYBACK_TRACE=1)");
  return lines.join(QLatin1Char('\n'));
}

QVariantList DiagnosticsBridge::findings() const {
  return DiagnosticAnalyzer::asVariantList(DiagnosticAnalyzer::analyze(
      statistics(), PlaybackTrace::instance().history(512)));
}

QString DiagnosticsBridge::verdict() const {
  return DiagnosticAnalyzer::verdict(DiagnosticAnalyzer::analyze(
      statistics(), PlaybackTrace::instance().history(512)));
}

QString DiagnosticsBridge::diagnosisText() const {
  return DiagnosticAnalyzer::text(DiagnosticAnalyzer::analyze(
      statistics(), PlaybackTrace::instance().history(512)));
}

QString DiagnosticsBridge::crashChannelText() const {
  return diag::CrashChannel::describe().join(QLatin1Char('\n'));
}

QString DiagnosticsBridge::fullReport(const QString &note) const {
  QStringList out;
  out << QStringLiteral("Cut Pro diagnostic report");
  out << QStringLiteral("when           %1")
             .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
  out << QStringLiteral("pid            %1")
             .arg(QCoreApplication::applicationPid());
  if (!note.isEmpty())
    out << QStringLiteral("note           %1").arg(note);

  out << QString() << QStringLiteral("--- diagnosis ---") << diagnosisText();

  out << QString() << QStringLiteral("--- measurements ---");
  const QVariantMap stats = statistics();
  for (auto it = stats.cbegin(); it != stats.cend(); ++it) {
    // A stall trace is a list; printing it as "QVariantList" would lose the one
    // thing in the map that names a function.
    const QVariant value = it.value();
    if (value.typeId() == QMetaType::QStringList)
      out << QStringLiteral("%1  %2").arg(it.key(),
                                          value.toStringList().join(
                                              QStringLiteral(" <- ")));
    else
      out << QStringLiteral("%1  %2").arg(it.key(), value.toString());
  }

  out << QString() << QStringLiteral("--- item tree census ---") << censusText();
  out << QString() << QStringLiteral("--- guarded models ---") << modelReport();
  out << QString() << QStringLiteral("--- playback trace ---")
      << playbackTrace(512);
  out << QString() << QStringLiteral("--- crash channel ---")
      << crashChannelText();
  return out.join(QLatin1Char('\n'));
}

QString DiagnosticsBridge::writeSnapshot(const QString &note) {
  const QString directory = CrashReporterHost::reportDirectory();
  QDir().mkpath(directory);
  const QString path = QDir(directory).filePath(
      QStringLiteral("cutpro-snapshot-%1.txt")
          .arg(QDateTime::currentDateTime().toString(
              QStringLiteral("yyyyMMdd-HHmmss"))));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate |
                 QIODevice::Text)) {
    qWarning() << "DiagnosticsBridge: cannot write" << path;
    return QString();
  }
  // Sampled first, so the file describes the moment it was asked for rather
  // than up to one interval before it. fullReport() reads the census through
  // ItemTreeCensus::last(), which this call has just refreshed.
  ItemTreeCensus::sampleNow();
  QTextStream stream(&file);
  // One text builder for the file, the report window and the clipboard: three
  // formats of the same evidence is how they drift apart, and the file is the
  // copy that gets attached to a bug.
  stream << fullReport(note) << '\n';
  Q_EMIT statisticsChanged();
  return path;
}

QString DiagnosticsBridge::printReport(const QString &note) {
  return DiagnosticsCli::printReport(this, note);
}

void DiagnosticsBridge::forceHang(int ms) {
  const int clamped = qBound(1000, ms, 120000);
  qWarning() << "DiagnosticsBridge: blocking the GUI thread for" << clamped
             << "ms on purpose - the reporter should leave a hang report";
  CUTPRO_GUI_SCOPE("DiagnosticsBridge::forceHang");
  QThread::msleep(static_cast<unsigned long>(clamped));
}

void DiagnosticsBridge::forceCrash() {
  qWarning() << "DiagnosticsBridge: raising a deliberate access violation";
  // Through a volatile pointer: a plain null write is undefined behaviour the
  // optimiser is entitled to delete outright, which would make the self-test
  // silently pass by doing nothing.
  volatile int *pointer = nullptr;
  *pointer = 1;
}
