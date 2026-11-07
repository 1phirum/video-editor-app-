#include <QtTest>

#include "app/export/sequence_export_builder.h"
#include "app/export/sequential_export_plan.h"
#include "app/media/ffmpeg_runtime.h"
#include "app/guard/license_session.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

class SequenceExportTests : public QObject {
  Q_OBJECT

  QTemporaryDir m_temp;
  QString m_source;
  QString m_processError;
  QStringList m_rasterFolders;

  bool run(const QStringList &arguments, QByteArray *output = nullptr) {
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
    process.start(FfmpegRuntime::executable(), arguments);
    if (!process.waitForStarted(10000)) {
      m_processError = process.errorString();
      return false;
    }
    if (!process.waitForFinished(60000)) {
      process.kill();
      process.waitForFinished();
      m_processError = QStringLiteral("FFmpeg timed out");
      return false;
    }
    m_processError = QString::fromUtf8(process.readAllStandardError());
    if (output)
      *output = process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
  }

  QVariantMap caption(int index, int start, int duration) const {
    return {{"id", QString::number(index)}, {"kind", "subtitle"}, {"track", "S1"},
            {"startMs", start}, {"durationMs", duration}, {"text", "Caption words"}};
  }

  QStringList build(const QVariantList &captions, const QString &output,
                    const QString &animation = "none") {
    const QVariantList media{QVariantMap{{"id", "video"}, {"path", m_source},
        {"width", 64}, {"height", 64}, {"frameRate", 10}, {"channels", 0}}};
    QVariantList clips{QVariantMap{{"id", "video"}, {"mediaId", "video"},
        {"kind", "video"}, {"track", "V1"}, {"startMs", 0}, {"sourceInMs", 0},
        {"durationMs", 1000}}};
    clips += captions;
    QString error;
    auto arguments = SequenceExportBuilder::build(media, clips, {}, {}, 1000, {},
        {{"audioEnabled", false}, {"encodingSpeed", "ultrafast"},
         {"captionAnimation", animation}, {"captionFontSize", 120}}, output, &error);
    m_processError = error;
    // Record only this invocation's renderer-owned folders for test cleanup.
    const QRegularExpression folder("[A-Za-z]\\\\?:[^'\\n;]*?cutpro-khmer-[^/\\\\'\\n;]+/");
    auto matches = folder.globalMatch(arguments.join(';'));
    while (matches.hasNext()) {
      QString path = matches.next().captured();
      path.replace("\\:", ":");
      if (!m_rasterFolders.contains(path))
        m_rasterFolders.append(path);
    }
    return arguments;
  }

  bool render(QStringList arguments, QByteArray *frames) {
    const int graphIndex = arguments.indexOf("-filter_complex");
    if (graphIndex < 0)
      return false;
    // Match Backend's existing long-graph launch path. The caption input list
    // must also fit Windows' command limit after the graph has been moved.
    QFile script(m_temp.filePath("filter.txt"));
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate))
      return false;
    const auto graph = arguments.at(graphIndex + 1).toUtf8();
    if (script.write(graph) != graph.size())
      return false;
    script.close();
    arguments[graphIndex] = "-/filter_complex";
    arguments[graphIndex + 1] = script.fileName();
    // Inspect lossless frames rather than comparing H.264 compression noise.
    const int codecIndex = arguments.indexOf("-c:v");
    if (codecIndex < 0)
      return false;
    arguments = arguments.mid(0, codecIndex) +
        QStringList{"-c:v", "rawvideo", "-pix_fmt", "rgba", "-f", "rawvideo", "pipe:1"};
    arguments.prepend("-nostdin");
    return run(arguments, frames);
  }

private slots:
  void initTestCase() {
    QVERIFY(m_temp.isValid());
    if (guard::exportGuardSeed() != 0)
      QSKIP("Export integration requires a valid test license (CUTPRO_TEST_STORED_LICENSE=1 for the installed license)");
    m_source = m_temp.filePath("source with spaces.mp4");
    QVERIFY2(run({"-nostdin", "-y", "-f", "lavfi", "-i",
        "color=c=blue:s=64x64:r=10:d=1", "-c:v", "libx264", "-preset", "ultrafast", m_source}),
        qPrintable(m_processError));
  }

  void cleanup() {
    for (const auto &folder : std::as_const(m_rasterFolders))
      QDir(folder).removeRecursively();
    m_rasterFolders.clear();
  }

  void realProjectWindowArguments() {
    const QString snapshot = qEnvironmentVariable("CUTPRO_EXPORT_BENCHMARK_PROJECT");
    const QString folder = qEnvironmentVariable("CUTPRO_EXPORT_BENCHMARK_DIR");
    if (snapshot.isEmpty() || folder.isEmpty()) QSKIP("Real-media benchmark is opt-in");
    QFile project(snapshot);
    QVERIFY(project.open(QIODevice::ReadOnly));
    const auto state = QJsonDocument::fromJson(project.readAll()).object().toVariantMap();
    const auto media = state.value("media").toList();
    const auto clips = state.value("clips").toList();
    const auto jobs = SequentialExportPlan::build(clips, media, {}, 30);
    QVERIFY(!jobs.isEmpty());
    const auto job = jobs.first().toMap();
    const qint64 duration = qMin<qint64>(30000, job.value("durationMs").toLongLong());
    QVariantList window;
    for (const QVariant &value : job.value("clips").toList())
      if (value.toMap().value("startMs").toLongLong() < duration) window.append(value);
    const QVariantMap settings{{"width", 1920}, {"height", 1080}, {"frameRate", 30},
        {"quality", "high"}, {"encodingSpeed", "medium"}, {"videoFormat", "h264"},
        {"videoBitrateMbps", 16}, {"continuousCanvas", true}};
    QString error;
    auto args = SequenceExportBuilder::build(media, window,
        state.value("mutedTracks").toStringList(), {}, duration,
        state.value("colorSettings").toMap(), settings,
        QDir(folder).filePath("software.mov"), &error);
    QVERIFY2(!args.isEmpty(), qPrintable(error));
    const int audioCodec = args.indexOf("-c:a");
    QVERIFY(audioCodec >= 0);
    args[audioCodec + 1] = "pcm_f32le";
    args.insert(args.size() - 1, "-frames:v");
    args.insert(args.size() - 1, QString::number(duration * 30 / 1000));
    QFile command(QDir(folder).filePath("window-arguments.json"));
    QVERIFY(command.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    const auto report = QJsonDocument(QJsonObject::fromVariantMap(
        {{"arguments", args}, {"settings", settings}, {"durationMs", duration},
         {"inputClips", window.size()}, {"totalProjectClips", clips.size()}})).toJson();
    QCOMPARE(command.write(report), report.size());
  }

  void layeredAudioWindowsMatchContinuousMix_data() {
    QTest::addColumn<bool>("compressed");
    QTest::newRow("PCM-and-H264") << false;
    QTest::newRow("MP3-and-HEVC") << true;
  }

  void layeredAudioWindowsMatchContinuousMix() {
    QFETCH(bool, compressed);
    const double frameRate = compressed ? 30 : 25;
    QString video = m_source;
    if (compressed) {
      video = m_temp.filePath("hevc with spaces.mp4");
      QVERIFY2(run({"-nostdin", "-y", "-f", "lavfi", "-i", "color=c=blue:s=64x64:r=30:d=1",
          "-c:v", "libx265", "-preset", "ultrafast", "-x265-params", "pools=1:frame-threads=1", video}),
          qPrintable(m_processError));
    }
    const QString tone = m_temp.filePath(compressed ? "narration with spaces.mp3" : "narration with spaces.wav");
    QVERIFY2(run({"-nostdin", "-y", "-f", "lavfi", "-i",
        compressed ? "sine=frequency=440:sample_rate=24000:duration=5"
                   : "sine=frequency=440:sample_rate=48000:duration=5",
        "-c:a", compressed ? "libmp3lame" : "pcm_f32le", tone}),
        qPrintable(m_processError));
    const QVariantList media{
        QVariantMap{{"id", "v"}, {"path", video}, {"width", 64}, {"height", 64},
                    {"frameRate", frameRate}, {"channels", 0}},
        QVariantMap{{"id", "a"}, {"path", tone}, {"channels", 1}}};
    QVariantList clips;
    for (int i = 0; i < 9; ++i)
      clips.append(QVariantMap{{"id", QString("v%1").arg(i)}, {"mediaId", "v"},
          {"kind", "video"}, {"track", "V1"}, {"startMs", i * 501},
          {"sourceInMs", 0}, {"durationMs", 501}});
    clips.append(QVariantMap{{"id", "voice"}, {"mediaId", "a"}, {"kind", "audio"},
        {"track", "A1"}, {"startMs", 350}, {"sourceInMs", 200}, {"durationMs", 2200},
        {"generatedBy", "text_to_speech"}, {"effects", QVariantMap{{"volumeDb", -3}}},
        {"effectStack", QVariantList{QVariantMap{{"definitionId", "transition_audio_gain_both"},
            {"parameters", QVariantMap{{"duration", 0.4}}}}}}});
    clips.append(QVariantMap{{"id", "bed"}, {"mediaId", "a"}, {"kind", "audio"},
        {"track", "A2"}, {"startMs", 2000}, {"sourceInMs", 100}, {"durationMs", 2400}});
    const auto jobs = SequentialExportPlan::build(clips, media, {}, frameRate);
    QCOMPARE(jobs.size(), 9);
    const QVariantMap settings{{"width", 64}, {"height", 64}, {"frameRate", frameRate},
        {"encodingSpeed", "ultrafast"}, {"audioChannels", 1}};
    const auto makeArguments = [&](const QVariantList &window, qint64 duration, const QString &path,
                                   bool continuousCanvas = false) {
      QString error;
      auto windowSettings = settings;
      windowSettings["continuousCanvas"] = continuousCanvas;
      auto args = SequenceExportBuilder::build(media, window, {}, {}, duration, {}, windowSettings, path, &error);
      m_processError = error;
      const int codec = args.indexOf("-c:a");
      if (codec >= 0) args[codec + 1] = "pcm_f32le";
      args.prepend("-nostdin");
      return args;
    };
    const QString reference = m_temp.filePath("continuous.mov");
    const qint64 totalMs = qRound64(qCeil(4509 * frameRate / 1000.0) * 1000.0 / frameRate);
    QVERIFY2(run(makeArguments(clips, totalMs, reference)), qPrintable(m_processError));
    QByteArray concat("ffconcat version 1.0\n");
    for (int i = 0; i < jobs.size(); ++i) {
      const auto job = jobs.at(i).toMap();
      const QString name = QString("audio-window-%1.mov").arg(i);
      auto args = makeArguments(job.value("clips").toList(), job.value("durationMs").toLongLong(),
                                m_temp.filePath(name), true);
      args.insert(args.size() - 1, "-frames:v");
      args.insert(args.size() - 1, QString::number(job.value("frameCount").toLongLong()));
      QVERIFY2(run(args), qPrintable(m_processError));
      concat += "file '" + name.toUtf8() + "'\n";
      concat += "duration " + QByteArray::number(job.value("durationMs").toLongLong() / 1000.0, 'f', 3) + '\n';
    }
    QFile list(m_temp.filePath("audio-windows.ffconcat"));
    QVERIFY(list.open(QIODevice::WriteOnly));
    QCOMPARE(list.write(concat), concat.size());
    list.close();
    QByteArray actual, expected;
    QVERIFY2(run({"-nostdin", "-v", "error", "-f", "concat", "-safe", "1", "-i", list.fileName(),
        "-vn", "-ac", "1", "-ar", "48000", "-f", "f32le", "pipe:1"}, &actual), qPrintable(m_processError));
    QVERIFY2(run({"-nostdin", "-v", "error", "-i", reference, "-vn", "-ac", "1", "-ar", "48000",
        "-f", "f32le", "pipe:1"}, &expected), qPrintable(m_processError));
    QCOMPARE(actual.size(), expected.size());
    double maxDifference = 0;
    for (qsizetype i = 0; i < actual.size(); i += sizeof(float)) {
      float a, b;
      memcpy(&a, actual.constData() + i, sizeof(float));
      memcpy(&b, expected.constData() + i, sizeof(float));
      maxDifference = qMax(maxDifference, double(qAbs(a - b)));
    }
    QVERIFY2(maxDifference < 0.00001, qPrintable(QString("Audio differs at a window boundary: %1").arg(maxDifference)));
  }

  void largeCaptionExportStarts_data() {
    QTest::addColumn<int>("count");
    QTest::newRow("over-Windows-command-limit") << 400;
    QTest::newRow("long-transcript") << 1744;
  }

  void largeCaptionExportStarts() {
    QFETCH(int, count);
    QVariantList captions;
    for (int i = 0; i < count; ++i)
      captions.append(caption(i, 100, 100));
    const auto arguments = build(captions, m_temp.filePath("large.mp4"));
    QVERIFY2(!arguments.isEmpty(), qPrintable(m_processError));
    QByteArray frames;
    QVERIFY2(render(arguments, &frames), qPrintable(m_processError));
    QCOMPARE(frames.size(), 10 * 64 * 64 * 4);
  }

  void captionsRemainVisibleOnlyDuringTheirTimeRanges() {
    QByteArray base, actual;
    QVERIFY2(render(build({}, m_temp.filePath("base.mp4")), &base), qPrintable(m_processError));
    // Place the edges between frames so floating-point endpoint rounding does
    // not decide this visibility check. Exact boundaries are compared below.
    const QVariantList captions{caption(0, 50, 200), caption(1, 450, 300), caption(2, 550, 200)};
    QVERIFY2(render(build(captions, m_temp.filePath("timed.mp4")), &actual), qPrintable(m_processError));
    QCOMPARE(actual.size(), base.size());
    constexpr int frameBytes = 64 * 64 * 4;
    for (int i = 0; i < 10; ++i) {
      const bool visible = i == 1 || i == 2 || i == 5 || i == 6 || i == 7;
      QCOMPARE(actual.mid(i * frameBytes, frameBytes) != base.mid(i * frameBytes, frameBytes), visible);
    }
  }

  void captionFramesMatchLoopingImageInputs_data() {
    QTest::addColumn<QString>("animation");
    QTest::newRow("static") << QString("none");
    QTest::newRow("fade") << QString("fadeIn");
    QTest::newRow("karaoke") << QString("karaoke");
  }

  void captionFramesMatchLoopingImageInputs() {
    QFETCH(QString, animation);
    const auto arguments = build({caption(0, 100, 200), caption(1, 500, 400), caption(2, 600, 200)},
                                 m_temp.filePath("animation.mp4"), animation);
    QVERIFY2(!arguments.isEmpty(), qPrintable(m_processError));
    QByteArray actual, reference;
    QVERIFY2(render(arguments, &actual), qPrintable(m_processError));

    // Independent reference: the previous exporter looped each rendered PNG as
    // a normal input. Reuse the same images to compare every frame, including
    // animation steps, overlapping cues and frames at exact cue boundaries.
    auto legacyArguments = arguments;
    const int graphIndex = legacyArguments.indexOf("-filter_complex");
    QString graph = legacyArguments.at(graphIndex + 1);
    const QRegularExpression movie("movie=filename='([^']+)':dec_threads=1,");
    QStringList inputs;
    int inputIndex = legacyArguments.count("-i");
    for (;;) {
      const auto match = movie.match(graph);
      if (!match.hasMatch())
        break;
      QString path = match.captured(1);
      path.replace("\\:", ":");
      inputs << "-loop" << "1" << "-t" << "1.000" << "-i" << path;
      graph.replace(match.capturedStart(), match.capturedLength(), QString("[%1:v]").arg(inputIndex++));
    }
    QVERIFY(!inputs.isEmpty());
    graph.replace("eof_action=repeat:repeatlast=1:format=auto", "eof_action=pass:repeatlast=0:format=auto");
    legacyArguments[graphIndex + 1] = graph;
    legacyArguments = legacyArguments.mid(0, graphIndex) + inputs + legacyArguments.mid(graphIndex);
    QVERIFY2(render(legacyArguments, &reference), qPrintable(m_processError));
    QCOMPARE(actual.size(), reference.size());
    // movie and command-line inputs use slightly different RGB/YUV rounding.
    // A four-level tolerance permits that conversion difference while still
    // detecting a missing/extra caption, wrong animation step or shifted frame.
    int maxDifference = 0;
    for (qsizetype i = 0; i < actual.size(); ++i)
      maxDifference = qMax(maxDifference, qAbs(int(uchar(actual[i])) - int(uchar(reference[i]))));
    QVERIFY2(maxDifference <= 4, qPrintable(QString("Frame pixel difference: %1").arg(maxDifference)));
  }
};

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  // Read the existing license only when explicitly requested. These tests do
  // not create a Backend or write app preferences/projects.
  if (qEnvironmentVariableIntValue("CUTPRO_TEST_STORED_LICENSE") == 1) {
    QCoreApplication::setOrganizationName("Antigravity");
    QCoreApplication::setApplicationName("Cut Pro");
  }
  SequenceExportTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "sequence_export_tests.moc"
