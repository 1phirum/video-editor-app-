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
#include "app/preview/gui_dispatch.h"
#include "app/preview/gui_thread_watchdog.h"
#include "app/preview/startup_warmup.h"
#include "app/preview/timeline_thumbnail_provider.h"
#include "app/preview/waveform_window_provider.h"
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

    qInfo().noquote()
        << "Cut Pro: native C++/Qt backend, coreVersion ="
        << QString::fromLatin1(core::kVersion) << "| built =" << __DATE__
        << __TIME__ << "| executable =" << QCoreApplication::applicationFilePath();

    QQmlApplicationEngine engine;
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
    // From here a stall is a visibly frozen window rather than a slow launch, and
    // the watchdog's reports should say so.
    GuiThreadWatchdog::instance().markWindowShown();

    const int status = app.exec();
    // Joined before the engine and the backend go away: the monitor thread reads
    // the scope pointer the GUI thread publishes, and letting it outlive the
    // objects that publish it would turn a clean exit into a shutdown crash.
    GuiThreadWatchdog::instance().stop();
    StartupWarmup::finish();
    return status;
}
