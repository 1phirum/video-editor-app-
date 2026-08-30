// Native C++/Qt entry point for Cut Pro (replaces the PySide6 main.py).
// Creates the QGuiApplication + QQmlApplicationEngine and loads the existing,
// untouched QML frontend embedded at qrc:/qml/main.qml.

#include <QGuiApplication>
#include <QQuickImageProvider>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QString>
#include <QUrl>
#include <QtGlobal>
#include <QtQml>

#include "app/core_app/backend.h"
#include "app/diagnostics/crash_channel.h"
#include "app/diagnostics/crash_reporter_host.h"
#include "app/diagnostics/diagnostics_bridge.h"
#include "app/diagnostics/item_tree_census.h"
#include "app/diagnostics/playback_trace.h"
#include "app/diagnostics/model_guard.h"
#include "app/preview/gui_dispatch.h"
#include "app/preview/gui_thread_watchdog.h"
#include "app/preview/startup_warmup.h"
#include "app/preview/timeline_thumbnail_provider.h"
#include "app/preview/waveform_window_provider.h"
#include "app/ui/app_cursor.h"
#include "core/version.h"

namespace {
class FfmpegPreviewImageProvider final : public QQuickImageProvider {
public:
    explicit FfmpegPreviewImageProvider(const Backend *backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

    QImage requestImage(const QString &, QSize *size,
                        const QSize &requestedSize) override {
        QImage image = m_backend ? m_backend->previewFrameImage() : QImage();
        if (size)
            *size = image.size();
        // The decoder already produces the frame at the monitor's own size, so a
        // rescale here would be a third full copy of the pixels on the GUI thread
        // for every displayed frame. Only downscale when QML genuinely asked for
        // something smaller - and smoothly, because this is the program monitor:
        // a nearest-neighbour reduction of a 4K frame is visibly aliased, which
        // is exactly the "preview looks worse than the file" complaint.
        if (image.isNull() || !requestedSize.isValid())
            return image;
        if (requestedSize.width() >= image.width() &&
            requestedSize.height() >= image.height())
            return image;
        return image.scaled(requestedSize, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation);
    }

private:
    const Backend *m_backend = nullptr;
};
} // namespace

int main(int argc, char* argv[]) {
    // Must be set before the QGuiApplication is constructed (as in main.py).
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QGuiApplication app(argc, argv);
    app.setOrganizationName("Antigravity");
    app.setOrganizationDomain("antigravity.ai");
    app.setApplicationName("Cut Pro");

    // Both have to be established on the GUI thread before anything can post to
    // it. install() fixes the dispatcher's queue, and start() records this thread
    // as the one whose responsiveness is being measured, so every later
    // GuiThreadWatchdog::onGuiThread() answer is correct.
    GuiDispatch::install();
    GuiThreadWatchdog::instance().start();

    // Before anything can stall, and before the window exists: the reporter has
    // to already be watching when the first freeze happens, not started in
    // response to one. This creates the shared block, installs the last-chance
    // exception filter and spawns cutpro_crash_report.exe. It never fails fatally
    // - a missing reporter logs one line and the editor runs as before.
    CrashReporterHost::start();

    // Started here and not later: everything between this line and engine.load()
    // is the head start it gets. The 2049 ms severe stall the tracer caught at
    // startup was the GUI thread loading the image-format plugins from inside a
    // QML icon's componentComplete; this asks for them first, on a thread nobody
    // is watching.
    StartupWarmup::begin();

    // The QML fully draws its own controls (custom background/contentItem), so
    // use the Basic style — the native Windows style ignores that customization
    // and would render native buttons instead of the dark UI.
    QQuickStyle::setStyle("Basic");

    // --- C++ backend registration -----------------------------------------
    // Registered and ready under "import CutPro 1.0"; the QML frontend is left
    // untouched in this step, so nothing imports it yet. This proves the
    // C++ -> QML pipeline compiles and registers cleanly at startup.
    Backend backend;
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Backend", &backend);

    // Caps any Repeater/ListView model width that is computed rather than
    // counted, and logs the key that asked for too much. A freeze already traced
    // to QQuickRepeater::clear() force-completing pending incubations is a model
    // that grew past what the window can draw; this makes the app say which
    // expression did it instead of surviving on a guess.
    ModelGuard &modelGuard = ModelGuard::instance();
    qmlRegisterSingletonInstance("CutPro", 1, 0, "ModelGuard", &modelGuard);

    // Census + guard + watchdog + crash reports behind one QML handle, plus the
    // two self-test calls that prove the reporter fires.
    DiagnosticsBridge diagnostics;
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Diagnostics", &diagnostics);

    // Attached-only type: `AppCursor.name: "Razor"` on any Item swaps in one of
    // the bitmap cursors under assets/cursors.
    qmlRegisterUncreatableType<AppCursor>(
        "CutPro", 1, 0, "AppCursor",
        QStringLiteral("AppCursor is only available as an attached property."));

    qInfo().noquote()
        << "Cut Pro: native C++/Qt backend, coreVersion ="
        << QString::fromLatin1(core::kVersion) << "| built =" << __DATE__
        << __TIME__ << "| executable =" << QCoreApplication::applicationFilePath();

    QQmlApplicationEngine engine;
    // Before anything can be loaded, so the very first playback-property write
    // already carries its QML caller. Attribution is the whole point of the
    // trace: Backend::playing has writers in three panels and the metaobject
    // write erases the caller from any C++ backtrace.
    PlaybackTrace::instance().attachEngine(&engine);
    engine.addImageProvider(QStringLiteral("ffmpeg-preview"),
                            new FfmpegPreviewImageProvider(&backend));
    // On-demand timeline thumbnails. Asynchronous, so a clip made of hundreds of
    // visible slots fills in without the GUI thread ever waiting on a decode.
    engine.addImageProvider(QStringLiteral("timeline-tile"),
                            new TimelineThumbnailProvider);
    // On-demand waveform windows. The whole-file sheet is a flat block once the
    // timeline is zoomed in past a few minutes per screen; this draws the span
    // actually on screen at the resolution it is drawn at.
    engine.addImageProvider(QStringLiteral("wave-window"),
                            new WaveformWindowProvider);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    // Named so the launch cost stops being reported as "no marked scope". The
    // tracer shows this blocking in QQmlTypeLoader::loadThread - the GUI thread
    // waiting while the type loader compiles main.qml and everything it imports.
    // It is a slow launch rather than a freeze: no window exists yet, so Windows
    // has nothing to paint a ghost over.
    {
        CUTPRO_GUI_SCOPE("QQmlApplicationEngine::load(qrc:/qml/main.qml)");
        engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    }
    if (engine.rootObjects().isEmpty())
        return -1;
    // Started once there is a tree to walk. Every sample is published into the
    // crash channel, so a hang report carries the item histogram as of a second
    // or two before the freeze - which is the only time it can be taken, since
    // the walk runs on the thread that gets wedged.
    diagnostics.attach(&engine, ItemTreeCensus::kDefaultIntervalMs);
    // From here a stall is a visibly frozen window rather than a slow launch, and
    // the watchdog's reports should say so.
    GuiThreadWatchdog::instance().markWindowShown();
    // Same distinction for the reporter: before this, a stalled heartbeat is a
    // slow launch and reporting it would bury the real freezes.
    diag::CrashChannel::markWindowShown();

    const int status = app.exec();
    // Joined before the engine and the backend go away: the monitor thread reads
    // the scope pointer the GUI thread publishes, and letting it outlive the
    // objects that publish it would turn a clean exit into a shutdown crash.
    ItemTreeCensus::stopSampling();
    GuiThreadWatchdog::instance().stop();
    // Tells the reporter this was a clean quit, so it exits without writing
    // anything. Without it every normal session would leave an "app vanished"
    // report behind.
    CrashReporterHost::stop();
    StartupWarmup::finish();
    return status;
}
