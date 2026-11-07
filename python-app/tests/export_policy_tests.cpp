#include <QtTest>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QUuid>

#include "app/core_app/backend.h"
#include "app/export/export_encoder_policy.h"
#include "app/media/ffmpeg_runtime.h"

class ExportPolicyTests : public QObject {
  Q_OBJECT
  QTemporaryDir m_temp;
  QString m_source;
  bool run(const QString &executable, const QStringList &args, QByteArray *output = nullptr) {
    QProcess process;
    process.start(executable, args);
    if (!process.waitForFinished(30000) || process.exitCode() != 0) return false;
    if (output) *output = process.readAllStandardOutput();
    return true;
  }
  bool load(Backend &backend, bool general = false, const QString &source = {}) {
    QVariantList clips;
    for (int i = 0; i < 9; ++i)
      clips.append(QVariantMap{{"id", QString::number(i)}, {"mediaId", "source"},
          {"kind", "video"}, {"track", "V1"}, {"startMs", i * 500},
          {"durationMs", 500}, {"sourceInMs", i % 2 * 500}, {"sourceDurationMs", 2000},
          {"effects", QVariantMap{{"horizontalFlip", true}}}});
    if (general) clips.append(QVariantMap{{"id", "caption"}, {"kind", "subtitle"},
        {"track", "S1"}, {"startMs", 0}, {"durationMs", 4500}, {"text", "Export caption"}});
    const QVariantMap state{{"schemaVersion", 4}, {"projectId", "export-policy-tests"},
        {"sequenceId", "sequence"}, {"media", QVariantList{QVariantMap{{"id", "source"},
          {"path", source.isEmpty() ? m_source : source}, {"kind", "video"}, {"channels", 2}, {"width", 320},
          {"height", 180}, {"frameRate", 30}, {"durationMs", 2000}}}}, {"clips", clips}};
    // loadProject intentionally prefers its newer SQLite sidecar. A new
    // fixture path prevents a prior test's autosave replacing this JSON.
    QFile file(m_temp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".cutpro.json"));
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(QJsonObject::fromVariantMap(state)).toJson());
    file.close();
    return backend.loadProject(file.fileName());
  }
private slots:
  void initTestCase() {
    QVERIFY(m_temp.isValid());
    // Copy only the installed license into an ephemeral test store. All
    // preferences, databases, projects and outputs remain isolated.
    const QString key = QSettings().value("License/key").toString();
    const QString settings = m_temp.filePath("settings.ini");
    QSettings(settings, QSettings::IniFormat).setValue("License/key", key);
    qputenv("CUTPRO_SETTINGS_FILE", settings.toUtf8());
    qputenv("CUTPRO_SETTINGS_DB", m_temp.filePath("settings.db").toUtf8());
    qputenv("CUTPRO_CACHE_DIR", m_temp.filePath("cache").toUtf8());
    m_source = m_temp.filePath("source with spaces.mp4");
    QVERIFY(run(FfmpegRuntime::executable(), {"-nostdin", "-y", "-f", "lavfi", "-i",
        "testsrc2=size=320x180:rate=30:duration=2", "-f", "lavfi", "-i",
        "sine=sample_rate=48000:duration=2", "-c:v", "libx264", "-c:a", "aac", m_source}));
  }
  void encoderPolicyPreservesFormatsAndQuality() {
    for (const QString &format : {"hevc422", "av1", "rle"})
      QVERIFY(ExportEncoderPolicy::hardware({{"videoFormat", format}}).isEmpty());
    const auto args = ExportEncoderPolicy::hardwareArguments({{"videoBitrateMbps", 16}});
    QVERIFY(!args.contains("-crf"));
    QVERIFY(args.at(args.indexOf("-b:v") + 1) != args.at(args.indexOf("-maxrate") + 1));
    const auto baseline = ExportEncoderPolicy::hardwareArguments({{"profile", "baseline"}});
    QCOMPARE(baseline.at(baseline.indexOf("-bf") + 1), QString("0"));
  }
  void remainingTimeSurvivesChunksAndSlowdown() {
    ExportTimeEstimate estimate;
    estimate.start(0, 120000, 20);
    estimate.sample(500, 0);
    QCOMPARE(estimate.remainingSeconds(), -1);
    estimate.sample(2000, 4000);
    QCOMPARE(estimate.remainingSeconds(), 64);
    estimate.sample(10000, 20000);
    QCOMPARE(estimate.remainingSeconds(), 56);
    estimate.sample(12000, 20000); // new process startup is elapsed export time
    QVERIFY(estimate.remainingSeconds() > 56);
    estimate.sample(20000, 30000);
    const qint64 beforeSlowdown = estimate.remainingSeconds();
    estimate.sample(60000, 40000);
    QVERIFY(estimate.remainingSeconds() > beforeSlowdown);
    estimate.beginFinalization(61000);
    QCOMPARE(estimate.remainingSeconds(), 6);
    estimate.sample(62000, 10000);
    QCOMPARE(estimate.remainingSeconds(), 11);
    estimate.sample(65000, 80000);
    QCOMPARE(estimate.remainingSeconds(), 2);
    estimate.sample(65000, -500); // malformed/regressed record cannot undo work
    QCOMPARE(estimate.remainingSeconds(), 2);
    estimate.start(70000, 120000);
    QVERIFY(!estimate.includesFinalization());
    estimate.beginFinalization(71000);
    QCOMPARE(estimate.remainingSeconds(), -1);
  }
  void pipelineEncodesAndJoins_data() {
    QTest::addColumn<QString>("codec");
    QTest::addColumn<QString>("policy");
    QTest::addColumn<bool>("general");
    QTest::newRow("software-chunks") << QString("h264") << QString("software") << false;
    QTest::newRow("H264-QSV-chunks") << QString("h264") << QString("qsv") << false;
    QTest::newRow("H264-QSV-caption") << QString("h264") << QString("qsv") << true;
    QTest::newRow("HEVC-QSV-chunks") << QString("hevc") << QString("qsv") << false;
    QTest::newRow("HEVC-QSV-caption") << QString("hevc") << QString("qsv") << true;
  }
  void pipelineEncodesAndJoins() {
    QFETCH(QString, codec); QFETCH(QString, policy); QFETCH(bool, general);
    Backend backend;
    if (!backend.licensed()) QSKIP("Installed test license unavailable");
    QVERIFY(load(backend, general));
    QCOMPARE(backend.hasSubtitleClips(), general);
    const QString output = m_temp.filePath(QString::fromLatin1(QTest::currentDataTag()) + ".mp4");
    QSignalSpy finished(&backend, &Backend::exportFinished);
    double progress = 0;
    bool regressed = false;
    connect(&backend, &Backend::exportStateChanged, this, [&]() {
      regressed = regressed || backend.exportProgress() < progress;
      progress = backend.exportProgress();
    });
    QVERIFY(backend.startExportWithSettings(output, {{"videoFormat", codec}, {"videoEncoder", policy},
        {"width", 1280}, {"height", 720}, {"frameRate", 30}, {"videoBitrateMbps", 6}}));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 60000);
    QVERIFY2(finished.last().first().toBool(), qPrintable(backend.lastError()));
    QCOMPARE(backend.exportEncoder(), policy == "qsv" ? codec + "_qsv" : QString("libx264"));
    QCOMPARE(backend.exportProgress(), 1.0);
    QCOMPARE(backend.exportRemainingSeconds(), 0);
    QVERIFY(!regressed);
    QByteArray frames;
    QVERIFY(run(FfmpegRuntime::probeExecutable(), {"-v", "error", "-select_streams", "v:0",
        "-count_frames", "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", output}, &frames));
    QCOMPARE(frames.trimmed().toInt(), 135);
    QVERIFY(run(FfmpegRuntime::executable(), {"-v", "error", "-i", output, "-f", "null", "-"}));
    QTRY_VERIFY_WITH_TIMEOUT(QDir(m_temp.path()).entryList({".cutpro-export-*"}, QDir::Dirs | QDir::Hidden).isEmpty(), 5000);
  }
  void unsupportedConfigurationFallsBackBeforeRendering() {
    Backend backend;
    if (!backend.licensed()) QSKIP("Installed test license unavailable");
    QVERIFY(load(backend));
    QSignalSpy finished(&backend, &Backend::exportFinished);
    QString selected;
    connect(&backend, &Backend::exportStateChanged, this, [&]() {
      if (backend.exportInProgress() && backend.exportEncoder() == "libx264") {
        selected = backend.exportEncoder();
        backend.cancelExport();
      }
    });
    const QString output = m_temp.filePath("unsupported.mp4");
    QVERIFY(backend.startExportWithSettings(output, {{"width", 32768}, {"height", 180},
        {"frameRate", 30}, {"videoEncoder", "auto"}}));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 20000);
    QCOMPARE(selected, QString("libx264"));
    QVERIFY(!backend.exportEncoderFallback().isEmpty());
    QVERIFY(!QFileInfo::exists(output));
    QCOMPARE(backend.exportStatus(), QString("Export cancelled"));
    finished.clear();
    QVERIFY(backend.startExportWithSettings(output, {{"width", 32768}, {"height", 180},
        {"frameRate", 30}, {"videoEncoder", "qsv"}}));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 20000);
    QVERIFY(!finished.last().first().toBool());
    QVERIFY(backend.lastError().contains("Select Software"));
    QVERIFY(!QFileInfo::exists(output));
  }
  void cancelDuringPreflight() {
    Backend backend;
    if (!backend.licensed()) QSKIP("Installed test license unavailable");
    QVERIFY(load(backend));
    QSignalSpy finished(&backend, &Backend::exportFinished);
    const QString output = m_temp.filePath("cancel-probe.mp4");
    QVERIFY(backend.startExportWithSettings(output, {{"width", 1920}, {"height", 1080}, {"videoEncoder", "qsv"}}));
    backend.cancelExport();
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 5000);
    QCOMPARE(finished.size(), 1);
    QCOMPARE(backend.exportStatus(), QString("Export cancelled"));
    QVERIFY(!backend.exportInProgress());
    QVERIFY(!QFileInfo::exists(output));
  }
  void runtimeHardwareFailureDoesNotChangeEncoder() {
    Backend backend;
    if (!backend.licensed()) QSKIP("Installed test license unavailable");
    const QString broken = m_temp.filePath("unreadable-video.mp4");
    QFile file(broken);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("invalid media");
    file.close();
    QVERIFY(load(backend, false, broken));
    QCOMPARE(backend.media().first().toMap().value("path").toString(), broken);
    QSignalSpy finished(&backend, &Backend::exportFinished);
    QVERIFY(backend.startExportWithSettings(m_temp.filePath("runtime-failure.mp4"),
        {{"width", 1280}, {"height", 720}, {"videoEncoder", "qsv"}}));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 20000);
    QVERIFY(!finished.last().first().toBool());
    QCOMPARE(backend.exportEncoder(), QString("h264_qsv"));
    QVERIFY(backend.lastError().contains("restart the export"));
    QVERIFY(!backend.exportInProgress());
    QTRY_VERIFY_WITH_TIMEOUT(QDir(m_temp.path()).entryList({".cutpro-export-*"}, QDir::Dirs | QDir::Hidden).isEmpty(), 5000);
  }
  void cancelDuringRenderOrJoin_data() {
    QTest::addColumn<bool>("joining");
    QTest::newRow("render") << false;
    QTest::newRow("join") << true;
  }
  void cancelDuringRenderOrJoin() {
    QFETCH(bool, joining);
    Backend backend;
    if (!backend.licensed()) QSKIP("Installed test license unavailable");
    QVERIFY(load(backend));
    QSignalSpy finished(&backend, &Backend::exportFinished);
    bool cancelled = false;
    connect(&backend, &Backend::exportStateChanged, this, [&]() {
      if (!cancelled && backend.exportInProgress() &&
          backend.exportFinalizing() == joining &&
          backend.exportStatus().startsWith(joining ? "Finalizing" : "Exporting clip")) {
        cancelled = true;
        backend.cancelExport();
      }
    });
    QVERIFY(backend.startExportWithSettings(m_temp.filePath(QString::fromLatin1(QTest::currentDataTag()) + ".mp4"),
        {{"width", 1280}, {"height", 720}, {"videoEncoder", "qsv"}}));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 60000);
    QVERIFY(cancelled);
    QCOMPARE(finished.size(), 1);
    QVERIFY(!finished.last().first().toBool());
    QVERIFY(!backend.exportInProgress());
    QCOMPARE(backend.exportStatus(), QString("Export cancelled"));
    QCOMPARE(backend.exportRemainingSeconds(), -1);
    QTRY_VERIFY_WITH_TIMEOUT(QDir(m_temp.path()).entryList({".cutpro-export-*"}, QDir::Dirs | QDir::Hidden).isEmpty(), 5000);
  }
  void hardwareVideoOnlyExport() {
    Backend backend;
    if (!backend.licensed()) QSKIP("Installed test license unavailable");
    QVERIFY(load(backend));
    const QString output = m_temp.filePath("video-only.mp4");
    QSignalSpy finished(&backend, &Backend::exportFinished);
    QVERIFY(backend.startExportWithSettings(output, {{"width", 1920}, {"height", 1080},
        {"frameRate", 30}, {"videoEncoder", "qsv"}, {"audioEnabled", false}}));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 60000);
    QVERIFY2(finished.last().first().toBool(), qPrintable(backend.lastError()));
    QByteArray streams;
    QVERIFY(run(FfmpegRuntime::probeExecutable(), {"-v", "error", "-show_entries", "stream=codec_type",
        "-of", "csv=p=0", output}, &streams));
    QCOMPARE(streams.trimmed(), QByteArray("video"));
    QVERIFY(run(FfmpegRuntime::executable(), {"-v", "error", "-xerror", "-i", output, "-f", "null", "-"}));
  }
  void closeDuringPreflight() {
    const QString output = m_temp.filePath("close-probe.mp4");
    {
      Backend backend;
      if (!backend.licensed()) QSKIP("Installed test license unavailable");
      QVERIFY(load(backend));
      QVERIFY(backend.startExportWithSettings(output, {{"width", 1920}, {"height", 1080}, {"videoEncoder", "qsv"}}));
    }
    QTest::qWait(100);
    QVERIFY(!QFileInfo::exists(output));
    QTRY_VERIFY_WITH_TIMEOUT(QDir(m_temp.path()).entryList({".cutpro-export-*"}, QDir::Dirs | QDir::Hidden).isEmpty(), 5000);
  }
  void fullProjectExport() {
    const QString snapshot = qEnvironmentVariable("CUTPRO_EXPORT_FULL_PROJECT");
    const QString output = qEnvironmentVariable("CUTPRO_EXPORT_FULL_OUTPUT");
    if (snapshot.isEmpty() || output.isEmpty()) QSKIP("Full real-project export is opt-in");
    QVERIFY(!QFileInfo::exists(output));
    const QString copy = m_temp.filePath("real-project.cutpro.json");
    QVERIFY(QFile::copy(snapshot, copy));
    Backend backend;
    QVERIFY(backend.licensed());
    QVERIFY(backend.loadProject(copy));
    QSignalSpy finished(&backend, &Backend::exportFinished);
    QFile progressLog(output + ".progress.jsonl");
    QVERIFY(progressLog.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    double previousProgress = 0;
    bool regressed = false;
    connect(&backend, &Backend::exportStateChanged, this, [&]() {
      regressed = regressed || backend.exportProgress() < previousProgress;
      previousProgress = backend.exportProgress();
    });
    QTimer progressTimer;
    connect(&progressTimer, &QTimer::timeout, this, [&]() {
      const QVariantMap sample{{"elapsedSeconds", backend.exportElapsedSeconds()},
          {"progress", backend.exportProgress()}, {"encoder", backend.exportEncoder()},
          {"status", backend.exportStatus()}, {"framesPerSecond", backend.exportFramesPerSecond()},
          {"speed", backend.exportSpeed()}, {"remainingSeconds", backend.exportRemainingSeconds()},
          {"finalizing", backend.exportFinalizing()},
          {"remainingIncludesFinalization", backend.exportRemainingIncludesFinalization()}};
      progressLog.write(QJsonDocument::fromVariant(sample).toJson(QJsonDocument::Compact) + '\n');
      progressLog.flush();
    });
    progressTimer.start(5000);
    QVERIFY(backend.startExportWithSettings(output, {{"width", 1920}, {"height", 1080},
        {"frameRate", 30}, {"videoEncoder", "qsv"}, {"videoBitrateMbps", 16}}));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 7200000);
    QVERIFY2(finished.last().first().toBool(), qPrintable(backend.lastError()));
    progressTimer.stop();
    QVERIFY(!regressed);
    QCOMPARE(backend.exportProgress(), 1.0);
    QCOMPARE(backend.exportRemainingSeconds(), 0);
    QCOMPARE(backend.exportEncoder(), QString("h264_qsv"));
    QByteArray frames;
    // Metadata is checked here; the complete output decode is a separate run.
    QVERIFY(run(FfmpegRuntime::probeExecutable(), {"-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=nb_frames", "-of", "csv=p=0", output}, &frames));
    QCOMPARE(frames.trimmed().toLongLong(), qCeil(backend.durationMs() * 30.0 / 1000));
    qInfo() << "Full project export seconds" << backend.exportElapsedSeconds() << "frames" << frames.trimmed();
  }
};

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName("Antigravity");
  app.setApplicationName("Cut Pro");
  ExportPolicyTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "export_policy_tests.moc"
