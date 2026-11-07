#include "app/preview/startup_warmup.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfoList>
#include <QImageReader>
#include <QLibrary>
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

// The one freeze the out-of-process reporter kept catching at launch (median
// ~17 s, up to 40 s) is the GUI thread's first touch of QMediaDevices: Qt lazily
// loads the multimedia backend plugin through QFactoryLoader -> QLibrary::load ->
// LoadLibraryExW, and the hang minidump shows the thread wedged in
// ntdll!NtCreateSection mapping multimedia/ffmpegmediaplugin.dll and the whole
// ffmpeg/Media-Foundation dependency closure under it while the resident scanner
// walks every image. That cost is pure DLL file-mapping - process-wide, loader-
// level, with no COM and no device object - so it is exactly the kind of one-time
// plugin load this worker exists to pre-pay. We only map the DLLs here; the
// apartment-affine part (constructing QMediaDevices and enumerating endpoints)
// deliberately still happens on the GUI thread in FfmpegPreviewDecoder, so
// nothing created on this soon-to-exit thread is ever handed across. Once the
// images are mapped, the GUI thread's later LoadLibraryExW is a refcount bump.
void warmMultimediaBackendPlugins() {
  QElapsedTimer timer;
  timer.start();
  // Resolved relative to the executable so it is correct in both the build tree
  // and the deployed "Cut Pro 2026" layout, where the backend plugins live in a
  // multimedia/ subdirectory next to cutpro.exe.
  const QString appDir = QCoreApplication::applicationDirPath();
  QDir pluginDir(appDir + QStringLiteral("/multimedia"));
  if (!pluginDir.exists()) {
    appendLine(QStringLiteral("multimediaPlugins=skipped (no %1)")
                   .arg(pluginDir.absolutePath()));
    return;
  }
  int loaded = 0;
  const QFileInfoList dlls =
      pluginDir.entryInfoList(QStringList{QStringLiteral("*.dll")}, QDir::Files);
  for (const QFileInfo &dll : dlls) {
    // QLibrary::load() is LoadLibraryExW: it maps the image and resolves its
    // dependency closure, which is the whole cost, but does not instantiate the
    // Qt plugin (that waits for QFactoryLoader on the GUI thread) and so touches
    // no COM. A plugin that fails to load here is a no-op - the GUI thread simply
    // pays the cost later, exactly as it does today - so there is nothing to
    // recover from, only a speed-up to miss.
    QLibrary lib(dll.absoluteFilePath());
    if (lib.load()) {
      ++loaded;
      // Not unloaded: keeping the reference mapped is the entire point, and the
      // process would load it again within seconds regardless.
    }
  }
  appendLine(QStringLiteral("multimediaPlugins=%1/%2 in %3 ms")
                 .arg(loaded)
                 .arg(dlls.size())
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
    warmMultimediaBackendPlugins();
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
