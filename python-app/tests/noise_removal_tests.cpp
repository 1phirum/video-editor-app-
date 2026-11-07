#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QSettings>
#include "app/guard/license_session.h"
#include <cmath>
#include "app/core_app/backend.h"
#include "app/export/sequence_export_builder.h"
#include "app/media/ffmpeg_runtime.h"
#include "app/media/noise_removal.h"

class NoiseRemovalTests final : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  std::unique_ptr<Backend> backend;
  std::unique_ptr<QQmlEngine> engine;
  QString source;
  QString fixtures;
  QByteArray run(const QString &exe, const QStringList &args) {
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; });
#endif
    process.start(exe, args);
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
      qWarning().noquote() << process.readAllStandardError();
      return {};
    }
    return process.readAllStandardOutput();
  }
  QByteArray pcm(const QString &path, int channels = 1) {
    return run(FfmpegRuntime::executable(), {"-v", "error", "-i", path,
        "-f", "f32le", "-ar", "48000", "-ac", QString::number(channels), "-"});
  }
  QVariantMap clip(const QString &id, const QString &kind, const QString &track) {
    return {{"id", id}, {"name", "Noisy speech"}, {"kind", kind}, {"track", track},
        {"mediaId", "source"}, {"startMs", 0}, {"sourceInMs", 1000},
        {"durationMs", 2000}, {"sourceDurationMs", 12000}, {"enabled", true}};
  }
  bool load(const QVariantList &clips) {
    const QVariantMap state{{"schemaVersion", 4}, {"projectId", "noise-tests"},
        {"sequenceId", "noise-sequence"}, {"videoTrackCount", 4}, {"audioTrackCount", 2},
        {"trackStates", QVariantMap{{"V3", QVariantMap{{"locked", true}}}}},
        {"media", QVariantList{QVariantMap{{"id", "source"}, {"kind", "video"},
            {"path", source}, {"channels", 1}, {"durationMs", 12000},
            {"width", 64}, {"height", 64}, {"frameRate", 10}}}}, {"clips", clips}};
    QFile file(temp.filePath("project.cutpro.json"));
    if (!file.open(QIODevice::WriteOnly))
      return false;
    file.write(QJsonDocument(QJsonObject::fromVariantMap(state)).toJson());
    file.close();
    return backend->loadProject(file.fileName());
  }
  QString clean(const QVariantMap &clip) {
    return clip.value("effects").toMap().value("noiseRemovalPath").toString();
  }
private slots:
  void initTestCase() {
    QVERIFY(temp.isValid());
    QStandardPaths::setTestModeEnabled(true);
    qputenv("CUTPRO_SETTINGS_FILE", temp.filePath("settings.ini").toUtf8());
    qputenv("CUTPRO_CACHE_DIR", temp.filePath("cache").toUtf8());
    if (qEnvironmentVariableIntValue("CUTPRO_TEST_STORED_LICENSE") == 1) {
      QSettings installed("Antigravity", "Cut Pro");
      QSettings isolated(temp.filePath("settings.ini"), QSettings::IniFormat);
      isolated.setValue("License/key", installed.value("License/key"));
      isolated.sync();
    }
    fixtures = qEnvironmentVariable("CUTPRO_NOISE_FIXTURES");
    QVERIFY(QFileInfo::exists(NoiseRemoval::modelPath()));
    source = temp.filePath(QString::fromUtf8("speech with spaces-ភាសា.mkv"));
    QStringList args{"-v", "error", "-y", "-f", "lavfi", "-i", "color=c=red:s=64x64:r=10:d=12"};
    if (!fixtures.isEmpty())
      args << "-i" << QDir(fixtures).filePath("libri1-hiss-input.wav");
    else
      args << "-f" << "lavfi" << "-i" << "sine=frequency=440:sample_rate=48000:duration=12";
    args << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-c:a" << "pcm_s24le" << "-shortest" << source;
    run(FfmpegRuntime::executable(), args);
    QVERIFY(QFileInfo(source).size() > 44);
    backend = std::make_unique<Backend>();
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Backend", backend.get());
    engine = std::make_unique<QQmlEngine>();
  }
  void durationAndChannels_data() {
    QTest::addColumn<int>("sampleRate"); QTest::addColumn<int>("channels");
    for (int rate : {44100, 48000})
      for (int channels : {1, 2})
        QTest::newRow(qPrintable(QString("%1-%2").arg(rate).arg(channels))) << rate << channels;
  }
  void durationAndChannels() {
    QFETCH(int, sampleRate); QFETCH(int, channels);
    const QString path = temp.filePath(QString("rate-%1-%2.wav").arg(sampleRate).arg(channels));
    run(FfmpegRuntime::executable(), {"-v", "error", "-y", "-f", "lavfi", "-i",
        QString("sine=frequency=440:sample_rate=%1:duration=1.237").arg(sampleRate),
        "-ac", QString::number(channels), "-c:a", "pcm_s24le", path});
    const QByteArray original = pcm(path, channels); QVERIFY(!original.isEmpty());
    NoiseRemoval processor;
    QSignalSpy done(&processor, &NoiseRemoval::finished);
    QVERIFY(processor.start(path, 0));
    QVERIFY(processor.busy());
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
    QCOMPARE(done.last().at(1).toString(), QString());
    const QByteArray bypass = pcm(done.last().at(0).toString(), channels);
    QCOMPARE(bypass.size(), original.size());
    double error = 0;
    for (qsizetype i = 0; i < bypass.size(); i += sizeof(float)) {
      float a, b; memcpy(&a, original.constData() + i, sizeof(float)); memcpy(&b, bypass.constData() + i, sizeof(float));
      error += (a - b) * (a - b);
    }
    QVERIFY(std::sqrt(error / (bypass.size() / sizeof(float))) < 0.00001);
    done.clear();
    QVERIFY(processor.start(path, 100));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
    QCOMPARE(done.last().at(1).toString(), QString());
    QCOMPARE(pcm(done.last().at(0).toString(), channels).size(), original.size());
  }
  void cancelAndRapidRestart() {
    NoiseRemoval processor;
    QSignalSpy done(&processor, &NoiseRemoval::finished);
    for (int i = 0; i < 8; ++i) {
      done.clear();
      QVERIFY(processor.start(source, 100));
      QVERIFY(!processor.start(source, 100));
      processor.cancel();
      QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
      QVERIFY(!done.last().at(1).toString().isEmpty());
      QVERIFY(done.last().at(0).toString().isEmpty());
      QVERIFY(!processor.busy());
    }
    const QString invalid = temp.filePath("corrupt.wav");
    QFile file(invalid); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not audio"); file.close();
    done.clear(); QVERIFY(processor.start(invalid, 100));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
    QVERIFY(!done.last().at(1).toString().isEmpty());
    QVERIFY(!processor.start(temp.filePath("missing.wav"), 100));
  }
  void strengthAndPreviewLength() {
    NoiseRemoval processor;
    QSignalSpy done(&processor, &NoiseRemoval::finished);
    QList<QByteArray> outputs;
    for (int strength : {0, 25, 100}) {
      done.clear();
      QVERIFY(processor.start(source, strength, 1333, 1237));
      QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
      QCOMPARE(done.last().at(1).toString(), QString());
      outputs.append(pcm(done.last().at(0).toString()));
      QCOMPARE(outputs.last().size(), qsizetype(59376 * sizeof(float)));
    }
    double mild = 0, strong = 0;
    for (qsizetype i = 0; i < outputs.first().size(); i += sizeof(float)) {
      float original, a, b;
      memcpy(&original, outputs.at(0).constData() + i, sizeof(float));
      memcpy(&a, outputs.at(1).constData() + i, sizeof(float));
      memcpy(&b, outputs.at(2).constData() + i, sizeof(float));
      mild += (a-original)*(a-original); strong += (b-original)*(b-original);
    }
    QVERIFY2(mild < strong * 0.2, "Lower strength must blend toward the original voice.");
  }
  void atomicApplyUndoSaveAndRestore() {
    QVERIFY(load({clip("v", "video", "V1"), clip("a", "audio", "A1"), clip("locked", "video", "V3")}));
    const QVariantMap before = backend->clipById("v");
    QSignalSpy done(backend.get(), &Backend::noiseRemovalFinished);
    QTimer timer; timer.setInterval(10); int ticks = 0;
    connect(&timer, &QTimer::timeout, this, [&]() { ++ticks; }); timer.start();
    QVERIFY(backend->applyClipsNoiseRemoval({"v", "a", "locked"}, 100));
    QVERIFY(!backend->applyClipsNoiseRemoval({"v"}, 100));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
    QVERIFY2(done.last().first().toBool(), qPrintable(done.last().at(1).toString()));
    QVERIFY(ticks > 0); timer.stop();
    const QVariantMap applied = backend->clipById("v");
    QVERIFY(!clean(applied).isEmpty());
    QCOMPARE(clean(applied), clean(backend->clipById("a")));
    QVERIFY(clean(backend->clipById("locked")).isEmpty());
    for (const QString &key : {QString("mediaId"), QString("sourceInMs"), QString("startMs"), QString("durationMs")})
      QCOMPARE(applied.value(key), before.value(key));
    QCOMPARE(backend->clipReplacementAudioPath(applied), clean(applied));
    backend->undo();
    QVERIFY(clean(backend->clipById("v")).isEmpty());
    QVERIFY(clean(backend->clipById("a")).isEmpty());
    backend->redo();
    QCOMPARE(clean(backend->clipById("v")), clean(applied));
    const QString saved = temp.filePath("saved.cutpro.json");
    QVERIFY(backend->saveProject(saved)); QVERIFY(backend->loadProject(saved));
    QCOMPARE(backend->clipReplacementAudioPath(backend->clipById("v")), clean(applied));
    QVERIFY(backend->restoreClipsNoiseRemoval({"v", "a"}));
    QVERIFY(backend->clipReplacementAudioPath(backend->clipById("v")).isEmpty());
    backend->undo();
    QCOMPARE(clean(backend->clipById("v")), clean(applied));
    QCOMPARE(clean(backend->clipById("a")), clean(applied));
    QVERIFY(backend->resetClipEffectSettings("v"));
    QVERIFY(backend->clipReplacementAudioPath(backend->clipById("v")).isEmpty());
  }
  void changedClipRejectsWholeBatch() {
    QVERIFY(load({clip("v", "video", "V1"), clip("a", "audio", "A1")}));
    QSignalSpy done(backend.get(), &Backend::noiseRemovalFinished);
    QVERIFY(backend->applyClipsNoiseRemoval({"v", "a"}, 100));
    QVERIFY(backend->setClipEffectSetting("a", "volumeDb", -3));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
    QVERIFY(!done.last().first().toBool());
    QVERIFY(clean(backend->clipById("v")).isEmpty());
    QVERIFY(clean(backend->clipById("a")).isEmpty());
  }
  void extractedAudioAndFallback() {
    QVERIFY(load({clip("v", "video", "V1")}));
    const QStringList extracted = backend->extractClipAudio({"v"}); QCOMPARE(extracted.size(), 1);
    QSignalSpy done(backend.get(), &Backend::noiseRemovalFinished);
    QVERIFY(backend->applyClipsNoiseRemoval({"v", extracted.first()}, 100));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
    QVERIFY2(done.last().first().toBool(), qPrintable(done.last().at(1).toString()));
    const QString path = clean(backend->clipById(extracted.first())); QVERIFY(!path.isEmpty());
    QVERIFY(clean(backend->clipById("v")).isEmpty());
    QVERIFY(backend->restoreClipAudio({"v"}));
    QCOMPARE(clean(backend->clipById("v")), path);
    const QStringList again = backend->extractClipAudio({"v"}); QCOMPARE(again.size(), 1);
    QCOMPARE(clean(backend->clipById(again.first())), path);
    QVariantMap changed = backend->clipById(again.first());
    auto effects = changed.value("effects").toMap();
    effects["noiseRemovalInputPath"] = "different-source.wav"; changed["effects"] = effects;
    QVERIFY(backend->clipReplacementAudioPath(changed).isEmpty());
    effects["noiseRemovalInputPath"] = source; effects["noiseRemovalPath"] = temp.filePath("missing.wav"); changed["effects"] = effects;
    QVERIFY(backend->clipReplacementAudioPath(changed).isEmpty());
  }
  void nativeDialogPreviewAndCloseCancels() {
    QVERIFY(load({clip("v", "video", "V1")}));
    QQmlComponent component(engine.get(), QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/components/timeline/NoiseRemovalDialog.qml")));
    QTRY_VERIFY2(component.isReady(), qPrintable(component.errorString()));
    QStringList warnings;
    const auto connection = connect(engine.get(), &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
      for (const auto &error : errors) warnings.append(error.toString());
    });
    auto disconnect = qScopeGuard([&]() { QObject::disconnect(connection); });
    std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    object->setProperty("clipId", "v"); object->setProperty("clipName", "Noisy speech sample");
    object->setProperty("targetIds", QVariantList{"v"});
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(window->flags().testFlag(Qt::WindowTitleHint));
    QVERIFY(!window->flags().testFlag(Qt::FramelessWindowHint));
    auto click = [&](const char *name) {
      auto *button = object->findChild<QQuickItem *>(name); QVERIFY(button);
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
          button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
    };
    click("noiseOriginalPreview");
    QTRY_VERIFY_WITH_TIMEOUT(!backend->noiseRemovalBusy(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(backend->noiseRemovalPreview(), QString("original"), 10000);
    backend->stopNoiseRemovalPreview();
    QVERIFY(backend->noiseRemovalPreview().isEmpty());
    click("noiseCleanedPreview");
    QTRY_VERIFY_WITH_TIMEOUT(!backend->noiseRemovalBusy(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(backend->noiseRemovalPreview(), QString("cleaned"), 10000);
    backend->stopNoiseRemovalPreview();
    QSignalSpy done(backend.get(), &Backend::noiseRemovalFinished);
    QVERIFY(backend->applyClipsNoiseRemoval({"v"}, 100));
    QVERIFY(backend->noiseRemovalBusy()); window->close();
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
    QVERIFY(!done.last().first().toBool()); QVERIFY(clean(backend->clipById("v")).isEmpty());
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    done.clear(); click("noiseRemovalApply");
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(done.last().first().toBool());
    if (!fixtures.isEmpty())
      QVERIFY(window->grabWindow().save(QDir(fixtures).filePath("noise-removal-dialog.png")));
    window->close(); QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
  }
  void exportKeepsVideoAndUsesCleanedAudio_data() {
    QTest::addColumn<double>("speed"); QTest::addColumn<bool>("extracted");
    QTest::newRow("embedded") << 100.0 << false;
    QTest::newRow("extracted") << 100.0 << true;
    QTest::newRow("retimed") << 125.0 << false;
  }
  void exportKeepsVideoAndUsesCleanedAudio() {
    QFETCH(double, speed); QFETCH(bool, extracted);
    if (guard::exportGuardSeed() != 0)
      QSKIP("Export integration requires CUTPRO_TEST_STORED_LICENSE=1 and a valid installed license.");
    QVERIFY(load({clip("v", "video", "V1")}));
    if (speed != 100)
      QVERIFY(backend->setClipSpeed("v", speed));
    QSignalSpy done(backend.get(), &Backend::noiseRemovalFinished);
    QVERIFY(backend->applyClipsNoiseRemoval({"v"}, 100));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(done.last().first().toBool());
    const QString path = clean(backend->clipById("v")); QVERIFY(!path.isEmpty());
    if (extracted)
      QCOMPARE(backend->extractClipAudio({"v"}).size(), 1);
    const qint64 duration = qRound64(2000 * 100 / speed);
    const QString output = temp.filePath("export.mp4"); QString error;
    const auto args = SequenceExportBuilder::build(backend->media(), backend->clips(), {}, {}, duration, {},
        {{"width", 64}, {"height", 64}, {"frameRate", 10}, {"encodingSpeed", "ultrafast"}}, output, &error);
    QVERIFY2(!args.isEmpty(), qPrintable(error)); QVERIFY(args.contains(source)); QVERIFY(args.contains(path));
    run(FfmpegRuntime::executable(), args); QVERIFY(QFileInfo(output).size() > 44);
    const QByteArray frames = run(FfmpegRuntime::probeExecutable(), {"-v", "error", "-select_streams", "v:0",
        "-count_frames", "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", output});
    QCOMPARE(frames.trimmed().toInt(), int(duration / 100));
    const QByteArray expected = run(FfmpegRuntime::executable(), {"-v", "error", "-ss", "1", "-i", path,
        "-af", speed == 100 ? QString("anull") : QString("atempo=%1").arg(speed / 100),
        "-t", QString::number(duration / 1000.0),
        "-ac", "1", "-ar", "48000", "-f", "f32le", "-"});
    const QByteArray actual = pcm(output); QVERIFY(!actual.isEmpty() && !expected.isEmpty());
    QVERIFY(qAbs(actual.size() - expected.size()) <= 4096);
    double ab = 0, aa = 0, bb = 0;
    for (qsizetype i = 0; i < qMin(actual.size(), expected.size()); i += sizeof(float)) {
      float a, b; memcpy(&a, actual.constData() + i, sizeof(float)); memcpy(&b, expected.constData() + i, sizeof(float));
      ab += a*b; aa += a*a; bb += b*b;
    }
    QVERIFY2(ab / std::sqrt(aa * bb) > 0.98, "Exported audio must match the trimmed cleaned source.");
  }
  void downloadedSpeechOutputs() {
    if (fixtures.isEmpty()) QSKIP("Set CUTPRO_NOISE_FIXTURES after preparing downloaded quality fixtures.");
    QDir root(fixtures); QFile manifest(root.filePath("fixtures.json")); QVERIFY(manifest.open(QIODevice::ReadOnly));
    const QJsonArray cases = QJsonDocument::fromJson(manifest.readAll()).array(); QVERIFY(!cases.isEmpty());
    QVERIFY(root.mkpath("app-outputs"));
    for (const auto &value : cases) {
      const QString name = value.toObject().value("name").toString();
      NoiseRemoval processor; QSignalSpy done(&processor, &NoiseRemoval::finished);
      QVERIFY(processor.start(root.filePath(name + "-input.wav"), 100));
      QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
      QCOMPARE(done.last().at(1).toString(), QString());
      const QString target = root.filePath("app-outputs/" + name + "-output.wav");
      if (QFileInfo::exists(target)) QFile::remove(target);
      QVERIFY(QFile::copy(done.last().at(0).toString(), target));
    }
  }
};

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  QQuickStyle::setStyle("Basic");
  NoiseRemovalTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "noise_removal_tests.moc"
