#include "app/diagnostics/diagnostics_bridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QVariantList>
#include <QtGlobal>

#include "app/diagnostics/crash_channel.h"
#include "app/diagnostics/crash_reporter_host.h"
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
}

QVariantMap DiagnosticsBridge::statistics() const {
  QVariantMap map = GuiThreadWatchdog::instance().statistics();
  merge(map, ItemTreeCensus::statistics());
  merge(map, ModelGuard::instance().statistics());
  merge(map, CrashReporterHost::statistics());
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
  // than up to one interval before it.
  const ItemTreeCensus::Result census = ItemTreeCensus::sampleNow();
  QTextStream stream(&file);
  stream << "Cut Pro snapshot\n";
  stream << "when           "
         << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
  stream << "pid            " << QCoreApplication::applicationPid() << '\n';
  if (!note.isEmpty())
    stream << "note           " << note << '\n';
  stream << '\n';
  const QVariantMap stats = statistics();
  for (auto it = stats.cbegin(); it != stats.cend(); ++it)
    stream << it.key() << "  " << it.value().toString() << '\n';
  stream << "\n--- item tree census ---\n" << census.text() << '\n';
  stream << "\n--- guarded models ---\n"
         << ModelGuard::instance().report() << '\n';
  stream << "\n--- playback trace ---\n" << playbackTrace(256) << '\n';
  stream << "\n--- crash channel ---\n";
  for (const QString &line : diag::CrashChannel::describe())
    stream << line << '\n';
  Q_EMIT statisticsChanged();
  return path;
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
