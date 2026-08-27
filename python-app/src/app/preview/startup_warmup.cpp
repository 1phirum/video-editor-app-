#include "app/preview/startup_warmup.h"

#include <QElapsedTimer>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QtGlobal>

namespace {

// Guards m_lines only. The thread object itself is only ever touched from the
// GUI thread (begin() and finish() are both documented as main()-only).
QMutex g_reportMutex;
QStringList g_report;
QThread *g_thread = nullptr;

void appendLine(const QString &line) {
  QMutexLocker locker(&g_reportMutex);
  g_report.append(line);
}

// One step, timed and recorded. Named separately from the lambda so a stall
// caught inside it resolves to something readable in the tracer's output.
void warmImageFormatPlugins() {
  QElapsedTimer timer;
  timer.start();
  // The call itself is the point - the returned list is not needed. Reading it
  // anyway keeps a release build from deciding the call has no effect.
  const int formats = QImageReader::supportedImageFormats().size();
  appendLine(QStringLiteral("imageFormats=%1 in %2 ms")
                 .arg(formats)
                 .arg(timer.elapsed()));
}

} // namespace

void StartupWarmup::begin() {
  if (g_thread)
    return;
  g_thread = QThread::create([]() {
    QElapsedTimer total;
    total.start();
    warmImageFormatPlugins();
    appendLine(QStringLiteral("total=%1 ms").arg(total.elapsed()));
  });
  g_thread->setObjectName(QStringLiteral("cutpro-startup-warmup"));
  // Below normal: this is work the GUI thread would otherwise do, but it must not
  // compete with the GUI thread for a core while the first frame is being built.
  g_thread->start(QThread::LowPriority);
}

void StartupWarmup::finish() {
  if (!g_thread)
    return;
  // No timeout. The only thing this thread does is load plugin DLLs, which
  // finishes on its own; killing it mid-LdrLoadDll would corrupt the loader state
  // for the rest of the shutdown.
  g_thread->wait();
  delete g_thread;
  g_thread = nullptr;
}

QStringList StartupWarmup::report() {
  QMutexLocker locker(&g_reportMutex);
  return g_report;
}
