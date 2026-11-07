#include <QtTest>
#include <QAudioDevice>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtQml>
#include "app/core_app/backend.h"
#include "app/preview/voice_over_recorder.h"
#include "app/preview/webcam_recorder.h"

// Mirror the launcher's qrc:/qml/assets alias when testing source QML.
class SourceAssets final : public QQmlAbstractUrlInterceptor {
  QUrl intercept(const QUrl &url, DataType) override {
    const QString prefix = QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/assets/");
    if (url.isLocalFile() && url.toLocalFile().startsWith(prefix))
      return QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/assets/") + url.toLocalFile().mid(prefix.size()));
    return url;
  }
};

class RecorderTests final : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  std::unique_ptr<Backend> backend;
  SourceAssets sourceAssets;
  std::unique_ptr<QQmlEngine> qmlEngine;
  QAudioFormat format() const {
    QAudioFormat f;
    f.setSampleRate(48000); f.setChannelCount(1); f.setSampleFormat(QAudioFormat::Int16);
    return f;
  }
  QByteArray pcm(int ms, qint16 amplitude) const {
    QByteArray bytes(48 * ms * 2, '\0');
    auto *samples = reinterpret_cast<qint16 *>(bytes.data());
    for (int i = 0; i < bytes.size() / 2; ++i)
      samples[i] = amplitude;
    return bytes;
  }
private slots:
  void initTestCase() {
    QVERIFY(temp.isValid());
    qputenv("CUTPRO_SETTINGS_FILE", temp.filePath("settings.ini").toUtf8());
    qputenv("CUTPRO_CACHE_DIR", temp.filePath("cache").toUtf8());
    backend = std::make_unique<Backend>();
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Backend", backend.get());
    qmlEngine = std::make_unique<QQmlEngine>();
    qmlEngine->addUrlInterceptor(&sourceAssets);
  }
  void monitorDoesNotCreateFilesAndClearsPeaks() {
    VoiceOverWavWriter writer;
    VoiceOverCaptureConfig config; config.meterOnly = true;
    QSignalSpy takes(&writer, &VoiceOverWavWriter::segmentReady);
    const auto path = temp.filePath("monitor-%1.wav");
    QVERIFY(writer.beginSession(path, format(), config));
    QVERIFY(!writer.capturing());
    QCOMPARE(writer.write(pcm(40, 16384)), 3840);
    float left, right; writer.peakLevels(&left, &right);
    QCOMPARE(left, 0.5f); QCOMPARE(right, 0.5f);
    writer.write(pcm(40, 0)); writer.peakLevels(&left, &right);
    QCOMPARE(left, 0.0f); QCOMPARE(right, 0.0f);
    QCOMPARE(writer.sessionMs(), 80);
    QVERIFY(!writer.endSession());
    QCOMPARE(takes.size(), 0);
    QVERIFY(!QFile::exists(path.arg(1)));
  }
  void handsFreePreservesOffsetsAndTail() {
    VoiceOverWavWriter writer;
    VoiceOverCaptureConfig config;
    config.handsFree = true; config.silenceFloor = 200;
    config.hangoverMs = 400; config.lookbackMs = 100;
    config.tailPaddingMs = 100; config.webcam = true;
    QSignalSpy takes(&writer, &VoiceOverWavWriter::segmentReady);
    QSignalSpy audio(&writer, &VoiceOverWavWriter::audioBufferReady);
    QVERIFY(writer.beginSession(temp.filePath("line-%1.wav"), format(), config));
    for (int i = 0; i < 5; ++i) writer.write(pcm(40, 0));
    for (int i = 0; i < 15; ++i) writer.write(pcm(40, 8000));
    for (int i = 0; i < 10; ++i) writer.write(pcm(40, 0));
    QCOMPARE(takes.size(), 1);
    QCOMPARE(takes[0][1].toLongLong(), 800);
    QCOMPARE(takes[0][2].toLongLong(), 100);
    QVERIFY(!writer.capturing());
    QCOMPARE(writer.sessionMs(), 1200);
    QCOMPARE(audio.size(), 30);
    QCOMPARE(qvariant_cast<QAudioBuffer>(audio.first()[0]).startTime(), 0);
    QCOMPARE(qvariant_cast<QAudioBuffer>(audio.last()[0]).startTime(), 1160000);
    writer.endSession();
    QFile file(takes[0][0].toString()); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto wav = file.readAll(); QCOMPARE(wav.left(4), QByteArray("RIFF"));
    QCOMPARE(wav.size(), 44 + 800 * 48 * 2);
  }
  void liveMeterClearsImmediatelyAndRetainsSeparatePeak() {
    QQmlEngine &engine = *qmlEngine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/components/timeline/RecorderLevelMeter.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> meter(component.create());
    QVERIFY2(meter != nullptr, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(meter.get(), "push", Q_ARG(QVariant, 0.5), Q_ARG(QVariant, 0.5)));
    QVERIFY(meter->property("live").toBool());
    const double peak = meter->property("peakDb").toDouble();
    QVERIFY(QMetaObject::invokeMethod(meter.get(), "push", Q_ARG(QVariant, 0.0), Q_ARG(QVariant, 0.0)));
    QVERIFY(!meter->property("live").toBool());
    QCOMPARE(meter->property("shownDb").toDouble(), -60.0);
    QCOMPARE(meter->property("peakDb").toDouble(), peak);
  }
  void nativeWindowRendersAndResizes() {
    QQmlEngine &engine = *qmlEngine;
    QStringList warnings;
    const auto connection = connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
      for (const auto &error : errors) warnings << error.toString();
    });
    auto disconnectWarnings = qScopeGuard([&] { disconnect(connection); });
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/components/timeline/VoiceOverDialog.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    QVERIFY(!window->flags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(window->flags().testFlag(Qt::WindowTitleHint));
    QVERIFY(window->flags().testFlag(Qt::WindowSystemMenuHint));
    QVERIFY(window->flags().testFlag(Qt::WindowCloseButtonHint));
    QCOMPARE(window->title(), QStringLiteral("Voice-over & Webcam"));
    QVERIFY(!object->findChild<QObject *>("recorderWindowClose"));
    window->show(); QTest::qWait(150);
    QVERIFY(window->frameMargins().top() > 0);
    for (const QSize size : {QSize(1020, 760), QSize(760, 580)}) {
      window->resize(size); QTest::qWait(150);
      auto *microphone = object->findChild<QQuickItem *>("recorderMicrophoneCard"); QVERIFY(microphone);
      auto *voice = object->findChild<QQuickItem *>("recorderVoicePreset"); QVERIFY(voice);
      QVERIFY2(voice->mapToItem(microphone, QPointF(voice->width(), voice->height())).y() <= microphone->height(),
               "The voice selector must stay inside the microphone card.");
      auto *record = object->findChild<QQuickItem *>("recorderRecordButton"); QVERIFY(record);
      QVERIFY(record->mapToScene(QPointF(0,0)).y() >= 0);
      QVERIFY(record->mapToScene(QPointF(record->width(),record->height())).y() <= window->height());
      const auto image = window->grabWindow(); QVERIFY(!image.isNull());
      const auto dir = qEnvironmentVariable("CUTPRO_RECORDER_ARTIFACTS");
      if (!dir.isEmpty()) {
        QDir().mkpath(dir);
        QVERIFY(image.save(QDir(dir).filePath(QStringLiteral("recorder-%1.png").arg(size.width()))));
      }
      auto *scroll = object->findChild<QQuickItem *>("recorderSetupScroll"); QVERIFY(scroll);
      auto *content = scroll->property("contentItem").value<QObject *>(); QVERIFY(content);
      const double bottom = content->property("contentHeight").toDouble() - scroll->height();
      QVERIFY(content->setProperty("contentY", qMax(0.0, bottom)));
      QTest::qWait(100);
      auto *mute = object->findChild<QQuickItem *>("recorderMuteProgram"); QVERIFY(mute);
      QVERIFY(mute->mapToItem(scroll, QPointF(0, 0)).y() >= 0);
      QVERIFY2(mute->mapToItem(scroll, QPointF(mute->width(), mute->height())).y() <= scroll->height(),
               "Recording settings must remain reachable by scrolling.");
      if (!dir.isEmpty())
        QVERIFY(window->grabWindow().save(QDir(dir).filePath(QStringLiteral("recorder-%1-scrolled.png").arg(size.width()))));
      QVERIFY(content->setProperty("contentY", 0));
    }
    object->setProperty("webcamMode", true); QTest::qWait(100);
    object->setProperty("webcamMode", false); window->close();
    QVERIFY(!qobject_cast<WebcamRecorder *>(backend->voiceOverWebcam())->active());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
  }
  void syntheticWebcamRecordsBothStreamsAndCanRestart() {
    WebcamRecorder recorder;
    QSignalSpy done(&recorder, &WebcamRecorder::finished);
    QSignalSpy errors(&recorder, &WebcamRecorder::failed);
    for (int session = 0; session < 2; ++session) {
      const QString path = temp.filePath(QStringLiteral("camera-%1.mp4").arg(session));
      QVERIFY(recorder.prepare(path));
      recorder.startEncoding(format());
      QImage image(320, 180, QImage::Format_RGB32); image.fill(Qt::blue);
      for (int i = 0; i < 40; ++i) {
        recorder.enqueueAudio(QAudioBuffer(pcm(40, 8000), format(), i * 40000));
        recorder.enqueueVideo(QVideoFrame(image));
        QTest::qWait(40);
      }
      recorder.finish();
      QTRY_COMPARE_WITH_TIMEOUT(done.size(), session + 1, 15000);
      QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first()[0].toString()));
      QVERIFY(!done.last()[2].toBool());
      QCOMPARE(done.last()[1].toLongLong(), 1600);
      QVERIFY(QFileInfo(path).size() > 1000);
      QProcess probe;
      probe.start(QStringLiteral(CUTPRO_SOURCE_ROOT "/../ffmpeg-9.0.1-full_build-shared/bin/ffprobe.exe"),
                  {"-v", "error", "-show_streams", "-of", "json", path});
      QVERIFY(probe.waitForFinished(10000)); QCOMPARE(probe.exitCode(), 0);
      const auto streams = QJsonDocument::fromJson(probe.readAllStandardOutput()).object()["streams"].toArray();
      bool audio = false, video = false;
      double audioStart = -1, videoStart = -1, audioDuration = 0, videoDuration = 0;
      for (const auto &value : streams) {
        const auto stream = value.toObject();
        if (stream["codec_type"] == "audio") {
          audio = true; audioStart = stream["start_time"].toString().toDouble();
          audioDuration = stream["duration"].toString().toDouble();
        }
        if (stream["codec_type"] == "video") {
          video = true; videoStart = stream["start_time"].toString().toDouble();
          videoDuration = stream["duration"].toString().toDouble();
        }
      }
      QVERIFY(audio && video);
      QVERIFY(qAbs(audioStart - videoStart) < 0.1);
      QVERIFY(qAbs(audioDuration - 1.6) < 0.15);
      QVERIFY(qAbs(videoDuration - audioDuration) < 0.15);
      const auto artifactDir = qEnvironmentVariable("CUTPRO_RECORDER_ARTIFACTS");
      if (!artifactDir.isEmpty()) {
        QFile copy(QDir(artifactDir).filePath(QStringLiteral("synthetic-%1-probe.json").arg(session)));
        QVERIFY(copy.open(QIODevice::WriteOnly));
        copy.write(QJsonDocument(QJsonObject{{"streams", streams}}).toJson());
      }
    }
  }
  void cancelPreparedWebcamReturnsIdleWithoutAFile() {
    WebcamRecorder recorder;
    QSignalSpy done(&recorder, &WebcamRecorder::finished);
    const QString path = temp.filePath("pending.mp4");
    QVERIFY(recorder.prepare(path)); recorder.finish(true);
    QCOMPARE(done.size(), 1); QVERIFY(done.first()[2].toBool());
    QVERIFY(!recorder.busy());
  }
  void encoderBackpressureReturnsAnErrorInsteadOfDroppingAudio() {
    WebcamRecorder recorder;
    QSignalSpy errors(&recorder, &WebcamRecorder::failed);
    QSignalSpy done(&recorder, &WebcamRecorder::finished);
    QVERIFY(recorder.prepare(temp.filePath("overflow.mp4")));
    for (int i = 0; i < 51; ++i)
      recorder.enqueueAudio(QAudioBuffer(pcm(40, 8000), format(), i * 40000));
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(done.size(), 1);
    QVERIFY(done.first()[2].toBool());
    QVERIFY(!recorder.busy());
  }
  void hardwareCameraAndMicrophonePlaceLinkedTakes() {
    if (qEnvironmentVariableIntValue("CUTPRO_RECORDER_HARDWARE_TEST") != 1)
      QSKIP("Enable CUTPRO_RECORDER_HARDWARE_TEST to exercise connected devices.");
    auto *webcam = qobject_cast<WebcamRecorder *>(backend->voiceOverWebcam());
    webcam->setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(webcam->active(), 15000);
    auto settings = backend->appSettings();
    settings["voiceOverCountdownSec"] = 0;
    settings["voiceOverHandsFree"] = false;
    settings["voiceOverPlayWhileRecording"] = false;
    settings["voiceOverVoiceId"] = "none";
    backend->applyAppSettings(settings);
    QQmlComponent component(qmlEngine.get(), QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/components/timeline/VoiceOverDialog.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    object->setProperty("webcamMode", true);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "openFor", Q_ARG(QVariant, ""), Q_ARG(QVariant, 0)));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    QTRY_VERIFY_WITH_TIMEOUT(webcam->active(), 15000);
    auto saveWindow = [&](const QString &name) {
      const auto dir = qEnvironmentVariable("CUTPRO_RECORDER_ARTIFACTS");
      if (!dir.isEmpty()) {
        QTest::qWait(100);
        window->grabWindow().save(QDir(dir).filePath(name + ".png"));
      }
    };
    saveWindow("recorder-webcam-ready");
    const int before = backend->clips().size();
    QSignalSpy done(backend.get(), &Backend::voiceOverFinished);
    QVERIFY(backend->startVoiceOver({}, 0, true));
    QVERIFY(backend->voiceOverBusy());
    QVERIFY(!backend->startVoiceOver({}, 0, true));
    QTRY_VERIFY_WITH_TIMEOUT(backend->voiceOverRecording(), 10000);
    QTest::qWait(2200);
    saveWindow("recorder-webcam-recording");
    backend->stopVoiceOver();
    QTRY_VERIFY_WITH_TIMEOUT(!backend->voiceOverBusy(), 20000);
    QVERIFY(!done.isEmpty());
    QVERIFY2(done.last()[0].toBool(), qPrintable(done.last()[2].toString()));
    QCOMPARE(backend->clips().size(), before + 2);
    QVariantMap audio, video;
    for (const auto &value : backend->clips()) {
      const auto clip = value.toMap();
      if (clip["linkedRole"].toString() == "audio") audio = clip;
      if (clip["linkedRole"].toString() == "video") video = clip;
    }
    QVERIFY(!audio.isEmpty() && !video.isEmpty());
    QCOMPARE(audio["linkGroupId"], video["linkGroupId"]);
    QCOMPARE(audio["startMs"], video["startMs"]);
    QCOMPARE(audio["durationMs"].toLongLong(), video["durationMs"].toLongLong());
    QVERIFY(video["separateAudio"].toBool());
    QVERIFY(audio["durationMs"].toLongLong() >= 2000);
    saveWindow("recorder-webcam-stopped");

    // Cancel immediately while opening the input; a late start callback must
    // not resurrect the session or add another clip.
    QVERIFY(backend->startVoiceOver({}, 4000, true));
    backend->cancelVoiceOver();
    QTRY_VERIFY_WITH_TIMEOUT(!backend->voiceOverBusy(), 15000);
    QTest::qWait(500);
    QVERIFY(!backend->voiceOverRecording());
    QCOMPARE(backend->clips().size(), before + 2);

    // Closing an active capture must discard its open take and release the
    // devices after asynchronous microphone/encoder shutdown.
    QVERIFY(backend->startVoiceOver({}, 8000, true));
    QTRY_VERIFY_WITH_TIMEOUT(backend->voiceOverRecording(), 10000);
    QTest::qWait(300);
    window->close();
    QTRY_VERIFY_WITH_TIMEOUT(!backend->voiceOverBusy(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!webcam->active(), 5000);
    QVERIFY(!webcam->busy());
    QCOMPARE(backend->clips().size(), before + 2);
  }
  void rapidCameraPreviewToggleAndClose() {
    if (qEnvironmentVariableIntValue("CUTPRO_RECORDER_HARDWARE_TEST") != 1)
      QSKIP("Enable CUTPRO_RECORDER_HARDWARE_TEST to exercise connected devices.");
    auto *webcam = qobject_cast<WebcamRecorder *>(backend->voiceOverWebcam());
    for (int attempt = 0; attempt < 8; ++attempt) {
      QQmlComponent component(qmlEngine.get(), QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/components/timeline/VoiceOverDialog.qml")));
      QVERIFY2(component.isReady(), qPrintable(component.errorString()));
      std::unique_ptr<QObject> object(component.create());
      QVERIFY2(object != nullptr, qPrintable(component.errorString()));
      auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
      window->show();
      object->setProperty("webcamMode", true);
      QTest::qWait(20);
      object->setProperty("webcamMode", false);
      object->setProperty("webcamMode", true);
      window->close();
      QTRY_VERIFY_WITH_TIMEOUT(!webcam->active(), 5000);
      QVERIFY(!webcam->busy());
    }
  }
};

int main(int argc, char **argv) {
  QQuickStyle::setStyle("Basic");
  QGuiApplication app(argc, argv);
  RecorderTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "recorder_tests.moc"
