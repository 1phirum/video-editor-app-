#include <QtTest>
#include <QGuiApplication>
#include <QDir>
#include <QProcess>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QtQml>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include "app/core_app/backend.h"
#include "app/export/export_encoder_policy.h"
#include "app/diagnostics/playback_trace.h"
#include "app/diagnostics/model_guard.h"
#include "app/ui/app_cursor.h"
#include "app/media/ffmpeg_runtime.h"
#include "app/preview/decode_session.h"

class SourceAssets final : public QQmlAbstractUrlInterceptor {
  QUrl intercept(const QUrl &url, DataType) override {
    const QString prefix = QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/assets/");
    if (url.isLocalFile() && url.toLocalFile().startsWith(prefix))
      return QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/assets/") +
                                url.toLocalFile().mid(prefix.size()));
    return url;
  }
};

class PreviewImages final : public QQuickImageProvider {
public:
  explicit PreviewImages(Backend *backend)
      : QQuickImageProvider(Image), m_backend(backend) {}
  QImage requestImage(const QString &, QSize *size, const QSize &) override {
    const QImage image = m_backend->gradedPreviewFrameImage();
    if (size)
      *size = image.size();
    return image;
  }
private:
  Backend *m_backend;
};

class ExportPreviewTests final : public QObject {
  Q_OBJECT
  QTemporaryDir m_temp;
  QString m_source;
  QString m_audioSource;
  std::unique_ptr<Backend> m_backend;
  SourceAssets m_assets;
  std::unique_ptr<QQmlEngine> m_engine;
  QQuickWindow m_window;
  std::unique_ptr<QQuickItem> m_preview;

  void invoke(const char *method) {
    QVERIFY(QMetaObject::invokeMethod(m_preview.get(), method));
  }
  void scrub(const char *method, double x) {
    QVERIFY(QMetaObject::invokeMethod(m_preview.get(), method,
                                      Q_ARG(QVariant, x), Q_ARG(QVariant, 1000.0)));
  }
private slots:
  void initTestCase() {
    QVERIFY(m_temp.isValid());
    qputenv("CUTPRO_SETTINGS_FILE", m_temp.filePath("settings.ini").toUtf8());
    qputenv("CUTPRO_CACHE_DIR", m_temp.filePath("cache").toUtf8());
    m_source = m_temp.filePath("numbered.mp4");
    const QDir fonts(QStandardPaths::writableLocation(QStandardPaths::FontsLocation));
    const QStringList fontFiles = fonts.entryList({"*.ttf"}, QDir::Files);
    QVERIFY(!fontFiles.isEmpty());
    QString font = fonts.filePath(fontFiles.first());
    font.replace(":", "\\:");
    QProcess generator;
    generator.start(FfmpegRuntime::executable(), {"-y", "-f", "lavfi", "-i",
        "testsrc2=size=320x180:rate=25:duration=10", "-vf",
        QStringLiteral("drawtext=fontfile='%1':text='%{n}':fontsize=28:fontcolor=white:x=10:y=10")
            .arg(font),
        "-c:v", "libx264", "-g", "250", "-pix_fmt", "yuv420p", m_source});
    QVERIFY(generator.waitForFinished(15000));
    QVERIFY2(generator.exitCode() == 0, generator.readAllStandardError().constData());
    m_audioSource = m_temp.filePath("numbered with audio.mp4");
    generator.start(FfmpegRuntime::executable(), {"-y", "-i", m_source,
        "-f", "lavfi", "-i", "sine=sample_rate=48000:duration=10", "-c:v", "copy",
        "-c:a", "aac", "-shortest", m_audioSource});
    QVERIFY(generator.waitForFinished(15000));
    QVERIFY2(generator.exitCode() == 0, generator.readAllStandardError().constData());
    m_backend = std::make_unique<Backend>();
    m_backend->setGpuGrading(false);
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Backend", m_backend.get());
    qmlRegisterSingletonInstance("CutPro", 1, 0, "ModelGuard", &ModelGuard::instance());
    qmlRegisterUncreatableType<AppCursor>("CutPro", 1, 0, "AppCursor", "Attached cursor properties");
    m_engine = std::make_unique<QQmlEngine>();
    m_engine->addUrlInterceptor(&m_assets);
    m_engine->addImageProvider("ffmpeg-preview", new PreviewImages(m_backend.get()));
    PlaybackTrace::instance().attachEngine(m_engine.get());
    // Physical desktop input must not change an automated transport test.
    m_window.setFlags(m_window.flags() | Qt::WindowTransparentForInput |
                      Qt::WindowDoesNotAcceptFocus);
    m_window.resize(640, 420);
    m_window.show();
  }
  void init() {
    QVERIFY(m_backend->newProject("Preview regression", m_temp.path(), true));
    QCOMPARE(m_backend->importMedia({m_source}), 1);
    const auto clips = m_backend->addMediaToTimeline(
        m_backend->media().first().toMap().value("id").toString(), 0, "V1");
    QVERIFY(!clips.isEmpty());
    QVERIFY(m_backend->splitClip(clips.first(), 5000));
    m_backend->setPlayheadMs(1200);
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(
        QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/components/export/ExportPreviewPlayer.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    m_preview.reset(qobject_cast<QQuickItem *>(component.createWithInitialProperties(
        {{"visible", false}, {"audioEnabled", false}})));
    QVERIFY2(m_preview != nullptr, qPrintable(component.errorString()));
    m_preview->setParentItem(m_window.contentItem());
    m_preview->setSize(QSizeF(640, 420));
    m_preview->setVisible(true);
    QSignalSpy frames(m_backend.get(), &Backend::previewFrameChanged);
    QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 5000);
    QTest::qWait(150);
  }
  void cleanup() {
    m_preview.reset();
    m_backend->setPlaying(false);
    m_backend->stopPreviewDecode();
  }
  void cleanupTestCase() {
    PlaybackTrace::instance().attachEngine(nullptr);
    m_engine.reset();
    m_backend.reset();
    // Close pooled codec contexts while their worker threads are still alive,
    // before Windows tears down DLLs at process exit.
    DecodeSessionCache::instance().clear();
  }
  void pauseRetainsAcceptedFrame() {
    QSignalSpy frames(m_backend.get(), &Backend::previewFrameChanged);
    // Cover steady playback, a cut, and Pause immediately after Play.
    for (int i = 0; i < 100; ++i) {
      m_backend->setPlayheadMs(i % 3 == 1 ? 4880 : 1200);
      QTest::qWait(150);
      frames.clear();
      invoke("togglePlayback");
      QVERIFY(m_backend->playing());
      if (i % 3 != 2)
        QTRY_VERIFY_WITH_TIMEOUT(frames.size() >= 4, 5000);
      invoke("togglePlayback");
      QVERIFY(!m_backend->playing());
      const QImage held = m_backend->previewFrameImage();
      const QString revision = m_backend->previewFrameUrl();
      const qint64 pts = m_backend->previewPresentedSourceMs();
      const auto served = m_backend->previewDecodeStatistics().value("served");
      const auto decodes = m_backend->previewDecodeStatistics().value("decodes");
      frames.clear();
      QTest::qWait(2000);
      QCOMPARE(frames.size(), 0);
      QCOMPARE(m_backend->previewFrameUrl(), revision);
      QCOMPARE(m_backend->previewFrameImage(), held);
      QCOMPARE(m_backend->previewPresentedSourceMs(), pts);
      QCOMPARE(m_backend->previewDecodeStatistics().value("served"), served);
      QCOMPARE(m_backend->previewDecodeStatistics().value("decodes"), decodes);
    }
  }
  void timelinePlayheadDisplayIsSmoothAndSeeksStayExact() {
    m_preview->setVisible(false);
    QVERIFY(m_backend->newProject("Playhead motion", m_temp.path(), true));
    const QString still = m_temp.filePath("playhead.png");
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(still));
    QCOMPARE(m_backend->importMedia({still}), 1);
    QVERIFY(!m_backend->addMediaToTimeline(
        m_backend->media().first().toMap().value("id").toString(), 0, "V1").isEmpty());
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(
        QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/panels/TimelinePanel.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QQuickItem> timeline(qobject_cast<QQuickItem *>(
        component.createWithInitialProperties({{"visible", false}})));
    QVERIFY2(timeline != nullptr, qPrintable(component.errorString()));
    timeline->setParentItem(m_window.contentItem());
    timeline->setSize(QSizeF(640, 420));
    timeline->setVisible(true);
    timeline->setProperty("fitMode", false);
    timeline->setProperty("pps", 126.0);
    auto *line = timeline->findChild<QQuickItem *>("timelinePlayheadLine");
    auto *flag = timeline->findChild<QQuickItem *>("timelinePlayheadFlag");
    auto *animation = timeline->findChild<QObject *>("timelinePlayheadFrames");
    QVERIFY(line && flag && animation);

    // Compare both presentations against the same transport samples and frames.
    QQmlComponent baselineComponent(m_engine.get());
    baselineComponent.setData(R"QML(import QtQuick
import CutPro 1.0
Item {
    property real displayX: Backend.playheadMs / 1000 * 126
    Behavior on displayX {
        enabled: Backend.playing
        NumberAnimation { duration: Backend.videoPreviewHelper.uiTickInterval; easing.type: Easing.Linear }
    }
})QML", QUrl());
    QVERIFY2(baselineComponent.isReady(), qPrintable(baselineComponent.errorString()));
    std::unique_ptr<QQuickItem> baseline(qobject_cast<QQuickItem *>(baselineComponent.create()));
    QVERIFY(baseline);
    baseline->setParentItem(m_window.contentItem());
    struct MotionSample { double time, smooth, old; };
    QList<MotionSample> samples;
    QElapsedTimer clock;
    clock.start();
    const auto capture = connect(&m_window, &QQuickWindow::afterAnimating, this, [&] {
      samples.append({clock.nsecsElapsed() / 1000000000.0, line->x(),
                      double(qRound(baseline->property("displayX").toDouble()))});
    });
    const auto disconnectCapture = qScopeGuard([&] { disconnect(capture); });
    QSignalSpy ticks(m_backend.get(), &Backend::playheadChanged);
    m_backend->setPlaying(true);
    QTest::qWait(2500);
    disconnect(capture);
    QVERIFY(samples.size() > 20);
    QVERIFY(ticks.size() > 20 && ticks.size() < 80);
    int stationaryOld = 0, stationarySmooth = 0, intervals = 0;
    double oldTotal = 0, smoothTotal = 0, oldSquares = 0, smoothSquares = 0;
    bool fractional = false;
    for (int i = 1; i < samples.size(); ++i) {
      const auto &a = samples.at(i - 1), &b = samples.at(i);
      if (a.time < 0.3 || b.time <= a.time)
        continue;
      QVERIFY2(b.smooth >= a.smooth, "Rendered playhead moved backward during playback");
      fractional |= qAbs(b.smooth - qRound(b.smooth)) > 0.01;
      const double oldSpeed = (b.old - a.old) / (b.time - a.time);
      const double smoothSpeed = (b.smooth - a.smooth) / (b.time - a.time);
      stationaryOld += qAbs(oldSpeed) < 0.001;
      stationarySmooth += qAbs(smoothSpeed) < 0.001;
      oldTotal += oldSpeed; smoothTotal += smoothSpeed;
      oldSquares += oldSpeed * oldSpeed; smoothSquares += smoothSpeed * smoothSpeed;
      ++intervals;
    }
    QVERIFY(fractional);
    QVERIFY(intervals > 10);
    qInfo() << "Playhead motion frames" << intervals << "transport ticks" << ticks.size()
            << "stationary old/new" << stationaryOld << stationarySmooth
            << "speed stddev old/new"
            << std::sqrt(qMax(0.0, oldSquares / intervals - qPow(oldTotal / intervals, 2)))
            << std::sqrt(qMax(0.0, smoothSquares / intervals - qPow(smoothTotal / intervals, 2)));
    for (qint64 position : {3400, 500}) {
      m_backend->setPlayheadMs(position);
      QCOMPARE(m_backend->previewDisplayPlayheadMs(), double(position));
      QTest::qWait(20);
    }
    m_backend->setPlaying(false);
    QVERIFY(!animation->property("running").toBool());
    QCOMPARE(line->x(), double(qRound(line->property("viewportX").toDouble())));
    const double pausedX = line->x();
    QCOMPARE(flag->x() + flag->width() / 2, pausedX);
    QTest::qWait(100);
    QCOMPARE(line->x(), pausedX);
    const double origin = line->property("viewportX").toDouble()
                          - m_backend->playheadMs() / 1000.0 * 126;
    timeline->setProperty("pps", 252.0);
    QCOMPARE(line->x(), double(qRound(origin + m_backend->playheadMs() / 1000.0 * 252)));
    timeline->setProperty("pps", 126.0);
    for (int i = 0; i < 5; ++i) {
      m_backend->setPlaying(true);
      QTest::qWait(20);
      m_backend->setPlaying(false);
      QCOMPARE(flag->x() + flag->width() / 2, line->x());
    }
    m_backend->setPlaying(true);
    QVERIFY(QMetaObject::invokeMethod(timeline.get(), "beginPlayheadScrub", Q_ARG(QVariant, 350.0)));
    QVERIFY(timeline->property("scrubbingPlayhead").toBool());
    QVERIFY(!m_backend->playing());
    QVERIFY(!animation->property("running").toBool());
    const double scrubX = line->x();
    QTest::qWait(50);
    QCOMPARE(line->x(), scrubX);
    QVERIFY(QMetaObject::invokeMethod(timeline.get(), "finishPlayheadScrub",
                                      Q_ARG(QVariant, 350.0), Q_ARG(QVariant, true)));
    QTRY_VERIFY(m_backend->playing());
    QVERIFY(animation->property("running").toBool());
    m_backend->setPlayheadMs(m_backend->durationMs() - 10);
    QTRY_VERIFY_WITH_TIMEOUT(!m_backend->playing(), 1000);
    QCOMPARE(m_backend->playheadMs(), m_backend->durationMs());
    QCOMPARE(m_backend->previewDisplayPlayheadMs(), double(m_backend->durationMs()));
    timeline->setWidth(720);
    m_backend->setPlaying(true);
    timeline->setVisible(false);
    QVERIFY(!animation->property("running").toBool());
    timeline.reset();
    m_backend->setPlaying(false);
    QVERIFY(m_backend->newProject("Empty playhead", m_temp.path(), true));
    QCOMPARE(m_backend->previewDisplayPlayheadMs(), 0.0);
  }
  void discreteAndSettledSeeksAreExact() {
    QSignalSpy frames(m_backend.get(), &Backend::previewFrameChanged);
    ScrubFrameService reference;
    QSignalSpy referenceFrames(&reference, &ScrubFrameService::frameReady);
    m_backend->setPlayheadMs(2400);
    QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 5000);
    const QImage discrete = m_backend->previewFrameImage();
    reference.request(m_source, 2400, QSize(320, 180), true);
    QTRY_VERIFY_WITH_TIMEOUT(!referenceFrames.isEmpty(), 5000);
    QCOMPARE(discrete, reference.frame());
    qint64 sourceMs = -1;
    reference.frame(&sourceMs);
    QCOMPARE(m_backend->previewPresentedSourceMs(), sourceMs);
    scrub("beginScrub", 360);
    QTest::qWait(150);
    frames.clear();
    scrub("finishScrub", 360);
    QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 5000);
    const QImage settled = m_backend->previewFrameImage();
    referenceFrames.clear();
    reference.request(m_source, 3600, QSize(320, 180), true);
    QTRY_VERIFY_WITH_TIMEOUT(!referenceFrames.isEmpty(), 5000);
    QCOMPARE(settled, reference.frame());
  }
  void pendingSeekCannotStopResumedOrHiddenPlayback() {
    m_backend->setPlayheadMs(2600);
    invoke("togglePlayback");
    QTest::qWait(150);
    QVERIFY(m_backend->previewDecodeStatistics().value("playbackDecoding").toBool());
    invoke("togglePlayback");
    m_backend->setPlayheadMs(3400);
    m_preview->setVisible(false);
    m_backend->setPlaying(true);
    QVERIFY(m_backend->startPreviewDecode(m_source, "video", 3400, 5000,
                                          320, 180, 25, false, 0));
    QTest::qWait(150);
    QVERIFY(m_backend->playing());
    QVERIFY(m_backend->previewDecodeStatistics().value("playbackDecoding").toBool());
    m_preview.reset();
    QVERIFY(m_backend->playing());
  }
  void retimedPlaybackMapsAcceptedPictureToTimeline() {
    const QString clipId = m_backend->videoPreviewHelper()->activeClipId();
    for (double speed : {50.0, 200.0}) {
      QVERIFY(m_backend->setClipSpeed(clipId, speed));
      m_backend->setPlayheadMs(1000);
      QTest::qWait(250);
      QSignalSpy frames(m_backend.get(), &Backend::previewFrameChanged);
      invoke("togglePlayback");
      QTRY_VERIFY_WITH_TIMEOUT(frames.size() >= 4, 5000);
      QTest::qWait(400);
      const qint64 pts = m_backend->previewPresentedSourceMs();
      invoke("togglePlayback");
      QCOMPARE(m_backend->playheadMs(), qRound64(pts * 100.0 / speed));
      QVERIFY(pts > qRound64(1000 * speed / 100.0));
      const QString revision = m_backend->previewFrameUrl();
      QTest::qWait(150);
      QCOMPARE(m_backend->previewFrameUrl(), revision);
    }
  }
  void retimedMutedAudioUsesSameClockAsPicture() {
    QVERIFY(m_backend->newProject("Audio clock regression", m_temp.path(), true));
    QCOMPARE(m_backend->importMedia({m_audioSource}), 1);
    const auto clips = m_backend->addMediaToTimeline(
        m_backend->media().first().toMap().value("id").toString(), 0, "V1");
    QVERIFY(!clips.isEmpty());
    for (double speed : {50.0, 200.0}) {
      QVERIFY(m_backend->setClipSpeed(clips.first(), speed));
      m_backend->setPlayheadMs(1000);
      QTest::qWait(250);
      QSignalSpy frames(m_backend.get(), &Backend::previewFrameChanged);
      invoke("togglePlayback");
      QTRY_VERIFY_WITH_TIMEOUT(frames.size() >= 4, 5000);
      // Endpoint initialization is asynchronous and can outlive the first
      // picture. Check the audio-master phase after the device actually starts.
      QTRY_VERIFY_WITH_TIMEOUT(m_backend->previewAudioClockSourceMs() >= 0, 10000);
      QTest::qWait(600);
      QTRY_VERIFY_WITH_TIMEOUT(qAbs(m_backend->playheadMs() -
          qRound64(m_backend->previewAudioClockSourceMs() * 100.0 / speed)) < 150, 5000);
      QTRY_VERIFY_WITH_TIMEOUT(qAbs(m_backend->previewPresentedSourceMs() -
          m_backend->previewAudioClockSourceMs()) < 150, 5000);
      const qint64 pts = m_backend->previewPresentedSourceMs();
      invoke("togglePlayback");
      QCOMPARE(m_backend->playheadMs(), qRound64(pts * 100.0 / speed));
    }
  }
  void retainedWorkspacesShareFrameAndSession() {
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(
        QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/panels/MonitorPanel.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QQuickItem> monitor(qobject_cast<QQuickItem *>(
        component.createWithInitialProperties({{"visible", false}})));
    QVERIFY2(monitor != nullptr, qPrintable(component.errorString()));
    monitor->setParentItem(m_window.contentItem());
    monitor->setSize(QSizeF(640, 420));
    auto sessions = [] {
      int count = 0;
      for (const QString &line : PlaybackTrace::instance().history())
        if (line.contains("session.start")) ++count;
      return count;
    };
    QSignalSpy frames(m_backend.get(), &Backend::previewFrameChanged);
    const QString held = m_backend->previewFrameUrl();
    const auto served = m_backend->previewDecodeStatistics().value("served");
    for (int i = 0; i < 10; ++i) {
      m_preview->setVisible(false);
      monitor->setVisible(true);
      QTest::qWait(110);
      monitor->setVisible(false);
      m_preview->setVisible(true);
      QTest::qWait(110);
    }
    QCOMPARE(m_backend->previewFrameUrl(), held);
    QCOMPARE(m_backend->previewDecodeStatistics().value("served"), served);
    invoke("togglePlayback");
    frames.clear();
    QTRY_VERIFY_WITH_TIMEOUT(frames.size() >= 4, 5000);
    const int before = sessions();
    qint64 position = m_backend->playheadMs();
    for (int i = 0; i < 4; ++i) {
      m_preview->setVisible(false);
      monitor->setVisible(true);
      QTest::qWait(110);
      QVERIFY(m_backend->playheadMs() >= position);
      position = m_backend->playheadMs();
      monitor->setVisible(false);
      m_preview->setVisible(true);
      QTest::qWait(110);
      QVERIFY(m_backend->playheadMs() >= position);
      position = m_backend->playheadMs();
    }
    QCOMPARE(sessions(), before);
    QVERIFY(qAbs(m_backend->playheadMs() - m_backend->previewPresentedSourceMs()) < 150);
    QVERIFY(m_backend->playing());
    invoke("togglePlayback");
    // A pending seek belongs to its visible workspace. Switching must still
    // settle at the requested frame and must not restart resumed playback.
    m_backend->setPlayheadMs(3600);
    m_preview->setVisible(false);
    monitor->setVisible(true);
    QTest::qWait(250);
    ScrubFrameService reference;
    QSignalSpy referenceFrames(&reference, &ScrubFrameService::frameReady);
    reference.request(m_source, 3600, QSize(320, 180), true);
    QTRY_VERIFY_WITH_TIMEOUT(!referenceFrames.isEmpty(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(m_backend->previewFrameImage(), reference.frame(), 5000);
    monitor->setVisible(false);
    m_preview->setVisible(true);
  }
  void delayedWarmupKeepsActiveAudioClock() {
    FfmpegPreviewDecoder decoder;
    QVERIFY(decoder.start(m_audioSource, "video", 0, 10000, 320, 180, 25, true, 0));
    QTRY_VERIFY_WITH_TIMEOUT(decoder.audioClockSourceMs() >= 0, 10000);
    const qint64 before = decoder.audioClockSourceMs();
    decoder.warmAudioOutput();
    QTRY_VERIFY_WITH_TIMEOUT(decoder.audioClockSourceMs() >= before + 300, 3000);
    decoder.stop();
  }
  void matchedRealExportBenchmark() {
    const QString snapshot = qEnvironmentVariable("CUTPRO_EXPORT_BENCHMARK_PROJECT");
    const QString folder = qEnvironmentVariable("CUTPRO_EXPORT_BENCHMARK_DIR");
    if (snapshot.isEmpty() || folder.isEmpty()) QSKIP("Matched real-media benchmark is opt-in");
    const QString project = m_temp.filePath("benchmark.cutpro.json");
    QVERIFY(QFile::copy(snapshot, project));
    QVERIFY(m_backend->loadProject(project));
    QFile command(QDir(folder).filePath("window-arguments.json"));
    QVERIFY(command.open(QIODevice::ReadOnly));
    const auto state = QJsonDocument::fromJson(command.readAll()).object().toVariantMap();
    QStringList original = state.value("arguments").toStringList();
    const int graphIndex = original.indexOf("-filter_complex");
    QVERIFY(graphIndex >= 0);
    QFile graph(QDir(folder).filePath("matched-filter.txt"));
    QVERIFY(graph.open(QIODevice::WriteOnly));
    graph.write(original.at(graphIndex + 1).toUtf8());
    graph.close();
    original[graphIndex] = "-/filter_complex";
    original[graphIndex + 1] = graph.fileName();
    const int codec = original.indexOf("-c:v"), audio = original.indexOf("-c:a");
    QVERIFY(codec >= 0 && audio > codec);
    QVariantList results;
    for (const bool playing : {false, true}) {
      m_backend->setPlayheadMs(0);
      m_backend->setPlaying(playing);
      QTest::qWait(2000);
      QCOMPARE(m_backend->playing(), playing);
      QSignalSpy transportChanges(m_backend.get(), &Backend::playingChanged);
      for (int run = 0; run < 4; ++run) {
        for (const bool hardware : {false, true}) {
          const QString output = QDir(folder).filePath(QString("%1-%2-%3.mov")
              .arg(playing ? "playing" : "paused").arg(hardware ? "qsv" : "software").arg(run));
          QVERIFY(!QFileInfo::exists(output));
          QStringList args = original;
          if (hardware) args = original.mid(0, codec) +
              ExportEncoderPolicy::hardwareArguments(state.value("settings").toMap()) + original.mid(audio);
          args.last() = output;
          args.prepend("-benchmark");
          args.prepend("-nostdin");
          QProcess process;
          QSignalSpy finished(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished));
          QElapsedTimer timer;
          timer.start();
          process.start(FfmpegRuntime::executable(), args);
          QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 120000);
          const qint64 elapsed = timer.elapsed();
          QCOMPARE(m_backend->playing(), playing);
          QVERIFY2(transportChanges.isEmpty(), "Playback changed during the matched benchmark");
          QFile log(output + ".log");
          QVERIFY(log.open(QIODevice::WriteOnly));
          log.write(process.readAllStandardError());
          log.close();
          QCOMPARE(process.exitCode(), 0);
          results.append(QVariantMap{{"playing", playing}, {"encoder", hardware ? "qsv" : "software"},
              {"run", run}, {"warmup", run == 0}, {"elapsedMs", elapsed}, {"bytes", QFileInfo(output).size()}});
          QFile report(QDir(folder).filePath("matched-results.json"));
          QVERIFY(report.open(QIODevice::WriteOnly));
          report.write(QJsonDocument::fromVariant(results).toJson());
          qInfo() << "Benchmark" << (playing ? "playing" : "paused")
                  << (hardware ? "qsv" : "software") << run << elapsed << "ms";
        }
      }
      m_backend->setPlaying(false);
    }
  }
  void workspacesDrawSameTransformedPicture() {
    auto grab = [](QQuickItem *item) {
      auto result = item->grabToImage(QSize(320, 180));
      if (!result) return QImage();
      QSignalSpy ready(result.get(), &QQuickItemGrabResult::ready);
      QElapsedTimer deadline;
      deadline.start();
      while (ready.isEmpty() && deadline.elapsed() < 3000) QTest::qWait(10);
      return result->image().convertToFormat(QImage::Format_RGB32);
    };
    auto difference = [](const QImage &a, const QImage &b) {
      if (a.isNull() || a.size() != b.size()) return 255.0;
      double total = 0;
      for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
          const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
          total += qAbs(qRed(p) - qRed(q)) + qAbs(qGreen(p) - qGreen(q)) + qAbs(qBlue(p) - qBlue(q));
        }
      return total / (a.width() * a.height() * 3.0);
    };
    auto exportPicture = m_preview->findChild<QQuickItem *>("sequenceBasePicture");
    QVERIFY(exportPicture);
    const QImage original = grab(exportPicture);
    QVERIFY(!original.isNull());
    const QString clipId = m_backend->videoPreviewHelper()->activeClipId();
    QVERIFY(m_backend->setClipEffectSetting(clipId, "horizontalFlip", true));
    QVERIFY(m_backend->setClipEffectSetting(clipId, "scale", 65));
    QVERIFY(m_backend->setClipEffectSetting(clipId, "rotation", 12));
    QVERIFY(m_backend->setClipEffectSetting(clipId, "positionX", 11));
    QTest::qWait(250);
    const QImage exported = grab(exportPicture);
    QVERIFY(difference(original, exported) > 10);
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(
        QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/panels/MonitorPanel.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QQuickItem> monitor(qobject_cast<QQuickItem *>(
        component.createWithInitialProperties({{"visible", false}})));
    QVERIFY2(monitor != nullptr, qPrintable(component.errorString()));
    monitor->setParentItem(m_window.contentItem());
    monitor->setSize(QSizeF(640, 420));
    m_preview->setVisible(false);
    monitor->setVisible(true);
    QTest::qWait(200);
    auto monitorPicture = monitor->findChild<QQuickItem *>("sequenceBasePicture");
    QVERIFY(monitorPicture);
    const QImage edited = grab(monitorPicture);
    const double meanDifference = difference(exported, edited);
    QVERIFY2(meanDifference < 4, qPrintable(QString::number(meanDifference)));
    monitor->setVisible(false);
    m_preview->setVisible(true);
  }
};

int main(int argc, char **argv) {
  qputenv("CUTPRO_PLAYBACK_TRACE", "1");
  qputenv("QT_QUICK_BACKEND", "software");
  QQuickStyle::setStyle("Basic");
  QGuiApplication app(argc, argv);
  ExportPreviewTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "export_preview_tests.moc"
