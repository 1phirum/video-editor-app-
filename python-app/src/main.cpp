// Native C++/Qt entry point for Cut Pro (replaces the PySide6 main.py).
// Creates the QGuiApplication + QQmlApplicationEngine and loads the existing,
// untouched QML frontend embedded at qrc:/qml/main.qml.

#include <QGuiApplication>
#include <QIcon>
#include <QQuickImageProvider>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWindow>
#include <QString>
#include <QTimer>
#include <QRandomGenerator>
#include <QUrl>
#include <QtGlobal>
#include <QtQml>

#include <cstdlib>

#include "app/core_app/backend.h"
#include "app/core_app/pool_manager.h"
#include "app/diagnostics/crash_channel.h"
#include "app/diagnostics/crash_reporter_host.h"
#include "app/diagnostics/diagnostics_bridge.h"
#include "app/diagnostics/diagnostics_cli.h"
#include "app/diagnostics/item_tree_census.h"
#include "app/diagnostics/playback_trace.h"
#include "app/diagnostics/model_guard.h"
#include "app/guard/anti_debug.h"
#include "app/guard/binary_integrity.h"
#include "app/guard/tamper.h"
#include "app/preview/gui_dispatch.h"
#include "app/preview/gui_thread_watchdog.h"
#include "app/preview/gui_turn_monitor.h"
#include "app/preview/startup_warmup.h"
#include "app/preview/scope_renderer.h"
#include "app/preview/timeline_thumbnail_provider.h"
#include "app/preview/waveform_window_provider.h"
#include "app/settings/app_settings.h"
#include "app/mcp/mcp_server.h"
#include "app/ai/ai_assistant.h"
#include "app/ui/app_cursor.h"
#include "app/ui/process_exit.h"
#include "app/ui/localization.h"
#include "app/update/update_checker.h"
#include "core/version.h"

namespace {
#ifdef Q_OS_WIN
BOOL WINAPI consoleCloseHandler(DWORD event) {
    if (event != CTRL_CLOSE_EVENT && event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
        return FALSE;
    // Runs on Windows' console-control thread. The channel is already initialized;
    // marking its POD flag needs no GUI event loop, allocation, or worker join.
    diag::CrashChannel::markCleanExit();
    app::exitProcess(EXIT_SUCCESS);
}
#endif

class FfmpegPreviewImageProvider final : public QQuickImageProvider {
public:
    explicit FfmpegPreviewImageProvider(const Backend *backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

    QImage requestImage(const QString &, QSize *size,
                        const QSize &requestedSize) override {
        QImage image =
            m_backend ? m_backend->gradedPreviewFrameImage() : QImage();
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

// The two Lumetri lookup tables, as textures for the grade shader. They are
// tiny (a 256x1 strip, and 1024x32 for a 32-point .cube) and only rebuilt when
// the grade generation changes, so handing them over as images costs far less
// than the 4096 floats a uniform array would need.
class LumetriLutImageProvider final : public QQuickImageProvider {
public:
    explicit LumetriLutImageProvider(const Backend *backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        // "curves?gen=N" / "cube?gen=N" - the generation is only there to defeat
        // the QML pixmap cache, so the prefix is all that selects the table.
        QImage image;
        if (m_backend) {
            image = id.startsWith(QLatin1String("cube"))
                        ? m_backend->lumetriCubeTexture()
                        : m_backend->lumetriCurveTexture();
        }
        if (size)
            *size = image.size();
        // Never rescaled: every texel is a table entry, and a filtered resize
        // would quietly rewrite the grade.
        return image;
    }

private:
    const Backend *m_backend = nullptr;
};

// Lumetri Scopes. Each request grades a small copy of the on-screen frame on the
// CPU (scopeSourceImage) and draws the chosen instrument. Synchronous like the
// frame provider: the source is capped to a few hundred pixels, so a scope is a
// sub-millisecond pass, and QML re-requests only when the frame revision changes.
class ScopeImageProvider final : public QQuickImageProvider {
public:
    explicit ScopeImageProvider(const Backend *backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override {
        // "waveform-rgb?rev=N" etc. - the query only defeats the pixmap cache.
        const scope::Type type = scope::typeFromId(id);
        QSize out = requestedSize.isValid() && requestedSize.width() > 0 &&
                            requestedSize.height() > 0
                        ? requestedSize
                        : QSize(512, 300);
        // A hard ceiling: a scope reads a downscaled sample, so there is no gain
        // in a graph larger than the panel could ever show, and it bounds the
        // per-frame allocation on the GUI thread.
        out = out.boundedTo(QSize(1024, 1024));
        // A denser sample makes a fuller trace: more source rows land more
        // points in every column. 420 px is enough to feed the blur into smooth
        // ribbons while keeping the per-frame grade+render well within budget so
        // it never competes with playback.
        const QImage src =
            m_backend ? m_backend->scopeSourceImage(420) : QImage();
        QImage graph = scope::render(src, type, out);
        if (size)
            *size = graph.size();
        return graph;
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
    // The logo every window this process opens is titled with, and the picture
    // Windows draws in the taskbar button while the app is running. It is read out
    // of the compiled resources rather than off disk, so it cannot go missing in a
    // deployed copy, and it is set here - before the engine exists - so the first
    // window is created already wearing it instead of being repainted afterwards.
    // The .exe's own icon (Explorer, Start menu, a pinned shortcut) is a separate
    // thing and comes from src/cutpro.rc.
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/image.png")));

    // `cutpro --diagnose` prints the newest report and exits: no window, no
    // engine, no crash reporter. Handled here because it needs the application
    // name (the report directory is derived from it) and nothing else - starting
    // the editor to read a text file would change the state being asked about.
    {
        int cliExit = 0;
        if (DiagnosticsCli::handleCommandLine(app.arguments(), &cliExit))
            return cliExit;
    }

    // Both have to be established on the GUI thread before anything can post to
    // it. install() fixes the dispatcher's queue, and start() records this thread
    // as the one whose responsiveness is being measured, so every later
    // GuiThreadWatchdog::onGuiThread() answer is correct.
    GuiDispatch::install();
    GuiThreadWatchdog::instance().start();
    // The frame-scale instrument, installed in the same breath as the freeze-scale
    // one: a dropped frame is 17-34 ms, two orders of magnitude below the smallest
    // thing the watchdog can see, which is why every "the video is slow" report so
    // far arrived with a clean log.
    GuiTurnMonitor::install();

    // Before anything can stall, and before the window exists: the reporter has
    // to already be watching when the first freeze happens, not started in
    // response to one. This creates the shared block, installs the last-chance
    // exception filter and spawns cutpro_crash_report.exe. It never fails fatally
    // - a missing reporter logs one line and the editor runs as before.
    CrashReporterHost::start();
#ifdef Q_OS_WIN
    if (GetConsoleWindow() && !SetConsoleCtrlHandler(consoleCloseHandler, TRUE))
        qWarning() << "Could not install console-close handler:" << GetLastError();
#endif

    // --- Anti-tamper floor ------------------------------------------------
    // Sweep for a debugger and verify the signed integrity manifest before the
    // UI loads, then keep re-checking on a jittered timer: attaching a debugger
    // or swapping a DLL mid-session is a routine cracking move. Detection never
    // fails on these lines (see app/guard/tamper.h) - it arms a poison flag the
    // licence gate, the exporter and maybeAbortIfTripped() read back elsewhere,
    // far from the detector, so a crack cannot map cause to effect. Runs after
    // CrashReporterHost so a delayed abort is captured like any other fault.
    guard::sweep();
    guard::verifyAll();
    {
        auto *tamperTimer = new QTimer(&app);
        QObject::connect(tamperTimer, &QTimer::timeout, &app, [] {
            guard::sweep();
            guard::verifyManifest();
            guard::maybeAbortIfTripped();
        });
        // Jittered, non-round interval so there is no fixed heartbeat to find.
        tamperTimer->start(16000 + int(QRandomGenerator::global()->bounded(11000)));
    }

    // Started here and not later: everything between this line and engine.load()
    // is the head start it gets. The 2049 ms severe stall the tracer caught at
    // startup was the GUI thread loading the image-format plugins from inside a
    // QML icon's componentComplete; this asks for them first, on a thread nobody
    // is watching.
    StartupWarmup::begin();

    // Override only tooltips to give every attached hover label rounded corners.
    // All other controls keep their existing Basic style and QML customization.
    QQuickStyle::setStyle("CutProStyle");
    QQuickStyle::setFallbackStyle("Basic");

    // --- C++ backend registration -----------------------------------------
    // Registered and ready under "import CutPro 1.0"; the QML frontend is left
    // untouched in this step, so nothing imports it yet. This proves the
    // C++ -> QML pipeline compiles and registers cleanly at startup.
    Backend backend;
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Backend", &backend);

    // Opt-in local agent control. Backend owns all edits and background jobs;
    // the launcher owns the transport and closes it before Backend is destroyed.
    McpServer mcp(&backend, backend.keyframeEngine(), backend.textToSpeechEngine());
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Mcp", &mcp);
    mcp.applySettings(backend.appSettings());
    QObject::connect(&backend, &Backend::appSettingsChanged, &mcp,
                     [&backend, &mcp] { mcp.applySettings(backend.appSettings()); });

    // UI language controller. The saved language is installed here, before the
    // engine loads, so the very first paint is already localized; the engine
    // pointer is handed over after the engine exists (below) so a later switch
    // can retranslate the live scene without a restart.
    Localization localization;
    localization.setLanguage(
        AppSettings::load()
            .value(QStringLiteral("language"), QStringLiteral("en"))
            .toString());
    qmlRegisterSingletonInstance("CutPro", 1, 0, "I18n", &localization);
    // Settings commit / reset / cancel all run through applyAppSettings and emit
    // this, so it is the one place the language has to be re-read - the same way
    // accentColor and appearanceBrightness apply live.
    QObject::connect(&backend, &Backend::appSettingsChanged, &localization,
                     [&backend, &localization]() {
                       localization.setLanguage(
                           backend.appSettings()
                               .value(QStringLiteral("language"),
                                      QStringLiteral("en"))
                               .toString());
                     });

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

    // Self-updater. Polls GitHub Releases for a newer build and drives the
    // top-bar update button. The first check runs a few seconds after launch so
    // it never competes with the initial paint and warmup.
    UpdateChecker updater;
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Updater", &updater);
    QTimer::singleShot(4000, &updater, &UpdateChecker::checkForUpdate);

    // The Google account pool: signs accounts in, rotates them for the Gemini
    // workers, and owns the loopback rotation server (tools/pool_tool.py).
    auto &pool = PoolManager::instance();
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Pool", &pool);
    AiAssistant assistant(&backend, &mcp, &pool);
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Ai", &assistant);
    QTimer::singleShot(0, &pool, &PoolManager::refresh);

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
    engine.addImportPath(QStringLiteral("qrc:/qml/styles"));
    // Now that the engine exists, a language switch can retranslate its scene.
    localization.setEngine(&engine);
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
    // The grade's lookup tables. Registered next to the frame provider because
    // the monitor asks for both in the same frame.
    engine.addImageProvider(QStringLiteral("lumetri-lut"),
                            new LumetriLutImageProvider(&backend));
    // The Lumetri Scopes graphs, drawn from the graded frame on demand.
    engine.addImageProvider(QStringLiteral("scope"),
                            new ScopeImageProvider(&backend));
    // ShaderEffect draws nothing under the software backend, so the monitor has
    // to fall back to the CPU chain there. Decided before main.qml loads: the
    // choice picks which of two mutually exclusive item trees is instantiated,
    // and flipping it afterwards would build both.
    backend.setGpuGrading(QQuickWindow::graphicsApi() !=
                          QSGRendererInterface::Software);
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

    // Failsafe against orphaned, headless cutpro.exe processes. Instances were
    // found alive in the wild with a running event loop but no visible window -
    // the main window had gone (user's X, or any code path that drops its
    // visibility) yet the process never quit, so it kept the install folder
    // locked (the "Folder In Use" / "installer conflicts with the old version"
    // reports) and piled up as zombies. quitOnLastWindowClosed on its own did
    // not end these, so we make it explicit AND quit the instant the primary
    // window stops being visible after startup. A hidden main window is never a
    // valid running state for this app (no tray icon, no background mode);
    // going fullscreen or minimizing keeps visible == true, so this only fires
    // on a real close/hide.
    app.setQuitOnLastWindowClosed(true);
    if (auto *rootWindow =
            qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        QObject::connect(rootWindow, &QWindow::visibleChanged, &app,
                         [](bool visible) {
                             if (!visible)
                                 QCoreApplication::quit();
                         });
    }

    // Registered for on-demand sampling, with the repeating walk off by default.
    //
    // It used to run every 2 s. The walk visits every QObject and QQuickItem in
    // the scene, on the GUI thread, and this window measured 9801 items at 38 ms
    // per walk - two dropped frames of a 30 fps preview, twice a second, for the
    // whole session. That is diagnostics cost charged to every frame budget, and
    // it was turning up in the same stutter it was installed to explain.
    //
    // Ctrl+Shift+D still walks on demand, one baseline walk still lands a few
    // seconds in so a hang report has a histogram, and CUTPRO_CENSUS_MS brings
    // the timer back for a session that is specifically hunting scene growth.
    int censusIntervalMs = 0;
    {
        bool ok = false;
        const int requested = qgetenv("CUTPRO_CENSUS_MS").trimmed().toInt(&ok);
        // Below ~250 ms the sampler spends more of the frame budget than the app.
        if (ok && requested > 0)
            censusIntervalMs = qMax(250, requested);
    }
    diagnostics.attach(&engine, censusIntervalMs);
    // The console replacement for the F12 overlay: off unless asked for, one line
    // per tick, no scene walk.
    DiagnosticsCli::startTicker(&diagnostics, &engine);
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

    // Force the process to terminate now instead of returning. The event loop has
    // already ended (the window closed), and everything that must happen on a
    // clean quit is done above: the crash reporter has been told this was a clean
    // exit, the watchdog and census sampler are stopped. Returning would hand off
    // to C runtime static destruction - the QApplication/engine/Backend teardown
    // that joins the preview-decode, timeline-prefetch, startup-warmup and audio
    // worker threads and tears down the embedded Python runtime. Any one of those
    // blocking on a ->wait() that never returns left cutpro.exe alive as a
    // headless zombie: it held the install folder locked and, when launched from a
    // terminal, never returned the prompt. The native Windows exit also bypasses
    // DLL detach handlers, which the CRT's _Exit still runs with stopped workers.
    // The async project save is SQLite/WAL and crash-safe.
    app::exitProcess(status);
}
