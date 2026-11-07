#include <QtTest>
#include "app/subtitles/transcript_translator.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace {
void writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
    qFatal("Could not write translation fixture");
}
const QByteArray worker = R"PY(
import json, os, pathlib, sys, time
message = json.dumps({'type':'status','message':'Translation: waiting for a free enabled account'}) + '\n'
split = len(message) // 2
sys.stdout.write(message[:split]); sys.stdout.flush()
time.sleep(0.02)
sys.stdout.write(message[split:]); sys.stdout.flush()
if sys.argv[1] != '--test' and os.environ.get('CUTPRO_TRANSLATOR_PROGRESS') == '1':
    event = json.dumps({'type':'progress','offset':200,'texts':['traducido'] * 50}) + '\n'
    split = len(event) // 2
    sys.stdout.write(event[:split]); sys.stdout.flush()
    time.sleep(0.02)
    sys.stdout.write(event[split:]); sys.stdout.flush()
    # Duplicate delivery and malformed batches must not inflate the count.
    sys.stdout.write(event)
    print(json.dumps({'type':'progress','offset':399,'texts':['overflow'] * 2}))
    print(json.dumps({'type':'progress','offset':0,'texts':['']}))
    print(json.dumps({'type':'status','message':'Translation batch 1/10: retry queued'}), flush=True)
deadline = time.monotonic() + 15
while not pathlib.Path(os.environ['CUTPRO_TRANSLATOR_RELEASE']).exists():
    if time.monotonic() > deadline:
        raise TimeoutError('fixture not released')
    time.sleep(0.02)
if os.environ.get('CUTPRO_TRANSLATOR_FAIL') == '1':
    print(json.dumps({'error':'fixture terminal error'}), flush=True)
    sys.exit(1)
if sys.argv[1] == '--test':
    result = {'ok':True,'message':'fixture connected'}
else:
    result = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8'))
    for segment in result['segments']:
        segment['text'] = 'translated'
sys.stdout.write(json.dumps(result)); sys.stdout.flush()
)PY";
QVariantMap settings() {
  return {{"pythonExecutable", qEnvironmentVariable("CUTPRO_BATCH_TEST_PYTHON")},
          {"translationProvider", "free"}};
}
QVariantList segments() {
  return {QVariantMap{{"start", 0.0}, {"end", 1.0}, {"text", "speech"}}};
}
}

class TranscriptTranslatorTests : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    if (!QFileInfo::exists(qEnvironmentVariable("CUTPRO_BATCH_TEST_PYTHON")))
      QSKIP("Set CUTPRO_BATCH_TEST_PYTHON to the bundled Python runtime");
    const QString tools = QCoreApplication::applicationDirPath() + "/tools";
    QVERIFY(QDir().mkpath(tools));
    writeFile(tools + "/translate_transcript.py", worker);
    qputenv("CUTPRO_TRANSLATOR_PROGRESS", "0");
  }
  void livePreviewCountsSegmentsAndPreservesOriginalOrder() {
    QTemporaryDir folder;
    const QString release = folder.filePath("release");
    qputenv("CUTPRO_TRANSLATOR_RELEASE", release.toUtf8());
    qputenv("CUTPRO_TRANSLATOR_FAIL", "0");
    qputenv("CUTPRO_TRANSLATOR_PROGRESS", "1");
    QVariantList input;
    for (int index = 0; index < 400; ++index)
      input.append(QVariantMap{{"start", index}, {"end", index + 1},
                               {"text", "speech"}, {"sourceIndex", index}});
    TranscriptTranslator translator;
    QSignalSpy finished(&translator, &TranscriptTranslator::finished);
    QVERIFY(translator.start(input, "es", settings()));
    QCOMPARE(translator.completedSegments(), 0);
    QCOMPARE(translator.totalSegments(), 400);
    QTRY_VERIFY_WITH_TIMEOUT(translator.status().contains("retry queued"), 3000);
    QCOMPARE(translator.completedSegments(), 50);
    QCOMPARE(translator.totalSegments(), 400);
    QVERIFY(translator.inProgress());
    QVERIFY(finished.isEmpty());
    QCOMPARE(translator.preview().at(199).toMap().value("text").toString(), QStringLiteral("speech"));
    QCOMPARE(translator.preview().at(200).toMap().value("text").toString(), QStringLiteral("traducido"));
    QCOMPARE(translator.preview().at(200).toMap().value("start").toInt(), 200);
    QCOMPARE(translator.preview().at(200).toMap().value("sourceIndex").toInt(), 200);
    QCOMPARE(translator.preview().at(250).toMap().value("text").toString(), QStringLiteral("speech"));
    writeFile(release, "ready");
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QVERIFY(finished.first().at(0).toBool());
    QCOMPARE(translator.completedSegments(), 400);
    QCOMPARE(translator.preview().last().toMap().value("text").toString(), QStringLiteral("translated"));
    translator.clearStatus();
    QCOMPARE(translator.completedSegments(), 0);
    QCOMPARE(translator.totalSegments(), 0);
    QVERIFY(translator.preview().isEmpty());
    qputenv("CUTPRO_TRANSLATOR_PROGRESS", "0");
  }
  void statusStreamsBeforeFinalReplyAndRestartClearsOldResult() {
    QTemporaryDir folder;
    const QString release = folder.filePath("release");
    qputenv("CUTPRO_TRANSLATOR_RELEASE", release.toUtf8());
    qputenv("CUTPRO_TRANSLATOR_FAIL", "0");
    TranscriptTranslator translator;
    QSignalSpy finished(&translator, &TranscriptTranslator::finished);
    QVERIFY(translator.start(segments(), "es", settings()));
    QTRY_VERIFY_WITH_TIMEOUT(translator.status().contains("waiting for a free"), 3000);
    QVERIFY(translator.inProgress());
    QVERIFY(finished.isEmpty());
    writeFile(release, "ready");
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QVERIFY(finished.first().at(0).toBool());
    QCOMPARE(finished.first().at(1).toList().first().toMap().value("text").toString(), QStringLiteral("translated"));
    qputenv("CUTPRO_TRANSLATOR_FAIL", "1");
    QVERIFY(translator.start(segments(), "es", settings()));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 3000);
    QVERIFY(!finished.last().at(0).toBool());
    QCOMPARE(finished.last().at(3).toString(), QStringLiteral("fixture terminal error"));
    QCOMPARE(translator.completedSegments(), 0);
    QCOMPARE(translator.preview().first().toMap().value("text").toString(), QStringLiteral("speech"));
  }
  void cancelDuringAccountWait() {
    QTemporaryDir folder;
    qputenv("CUTPRO_TRANSLATOR_RELEASE", folder.filePath("release").toUtf8());
    qputenv("CUTPRO_TRANSLATOR_FAIL", "0");
    TranscriptTranslator translator;
    QSignalSpy finished(&translator, &TranscriptTranslator::finished);
    QVERIFY(translator.start(segments(), "es", settings()));
    QTRY_VERIFY_WITH_TIMEOUT(translator.status().contains("waiting"), 3000);
    translator.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!translator.inProgress(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QVERIFY(!finished.first().at(0).toBool());
    QVERIFY(finished.first().at(1).toList().isEmpty());
    QCOMPARE(translator.status(), QStringLiteral("Translation cancelled"));
  }
  void providerTestStillReadsFinalResponse() {
    QTemporaryDir folder;
    const QString release = folder.filePath("release");
    qputenv("CUTPRO_TRANSLATOR_RELEASE", release.toUtf8());
    qputenv("CUTPRO_TRANSLATOR_FAIL", "0");
    writeFile(release, "ready");
    TranscriptTranslator translator;
    QSignalSpy finished(&translator, &TranscriptTranslator::testFinished);
    QVERIFY(translator.test(settings()));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QVERIFY(finished.first().at(0).toBool());
    QCOMPARE(finished.first().at(1).toString(), QStringLiteral("fixture connected"));
  }
  void cleanupTestCase() {
    const QString tools = QCoreApplication::applicationDirPath() + "/tools";
    QFile::remove(tools + "/translate_transcript.py");
    QDir().rmdir(tools);
    qunsetenv("CUTPRO_TRANSLATOR_RELEASE");
    qunsetenv("CUTPRO_TRANSLATOR_FAIL");
    qunsetenv("CUTPRO_TRANSLATOR_PROGRESS");
  }
};
QTEST_GUILESS_MAIN(TranscriptTranslatorTests)
#include "transcript_translator_tests.moc"
