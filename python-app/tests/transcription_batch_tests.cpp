#include <QtTest>
#include "app/subtitles/transcription_batch.h"
#include "app/subtitles/gemini_work_policy.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
void writeFile(const QString &path, const QByteArray &bytes = "fixture") {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
    qFatal("Could not write fixture file");
}
const QByteArray fixture = R"PY(
import json, os, pathlib, sys, time
folder = pathlib.Path(os.environ['CUTPRO_BATCH_FIXTURE_DIR'])
chunked = sys.argv[1] == '--job'
job = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding='utf-8')) if chunked else None
source = pathlib.Path(job['source'] if chunked else sys.argv[1]).stem
if source.startswith('child'):
    import subprocess
    child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
    (folder / ('child-' + source)).write_text(str(child.pid), encoding='utf-8')
(folder / ('started-' + source)).write_text(os.environ.get('CUTPRO_POOL_PARALLELISM', ''), encoding='utf-8')
def emit(value, final=False):
    text = json.dumps(value) + ('' if final else '\n')
    split = len(text) // 2
    sys.stdout.write(text[:split]); sys.stdout.flush()
    time.sleep(0.01)
    sys.stdout.write(text[split:]); sys.stdout.flush()
def segments(index):
    lead = job['windows'][index].get('leadInMs', 0) / 1000.0
    return [{'start':lead,'end':lead+1,'text':source}]
if chunked:
    assert job['prefetch'] is False
    emit({'type':'window','index':0,'count':len(job['windows'])})
    emit({'type':'segments','index':0,'segments':segments(0)})
else:
    emit({'type':'status','message':'Fixture running'})
if source.startswith('retry'):
    emit({'type':'status','message':'Transcription: waiting for a free enabled account'})
deadline = time.monotonic() + 15
while not (folder / ('release-' + source)).exists():
    if time.monotonic() > deadline:
        raise TimeoutError('fixture was not released')
    time.sleep(0.02)
if source.startswith('fail'):
    emit({'error':'fixture provider failure'})
    sys.exit(4)
if chunked:
    if source.startswith('incomplete'):
        sys.exit(0)
    for index in range(1, len(job['windows'])):
        emit({'type':'window','index':index,'count':len(job['windows'])})
        emit({'type':'segments','index':index,'segments':segments(index)})
    emit({'type':'done','language':'zh'}, final=True)
else:
    emit({'segments':[{'start':0,'end':1,'text':source}],'language':'zh'}, final=True)
)PY";

TranscriptionBatchRequest request(const QString &folder, int count, int concurrency) {
  const QString python = qEnvironmentVariable("CUTPRO_BATCH_TEST_PYTHON");
  writeFile(folder + "/worker.py", fixture);
  TranscriptionBatchRequest result;
  result.program = python;
  result.worker = folder + "/worker.py";
  result.model = QStringLiteral("gemini-test");
  result.language = QStringLiteral("auto");
  result.concurrency = concurrency;
  result.environment = QProcessEnvironment::systemEnvironment();
  result.environment.insert(QStringLiteral("CUTPRO_BATCH_FIXTURE_DIR"), folder);
  result.environment.insert(QStringLiteral("CUTPRO_POOL_PARALLELISM"), QStringLiteral("99"));
  for (int i = 0; i < count; ++i) {
    const QString id = QStringLiteral("clip%1").arg(i);
    const QString path = folder + "/" + id + ".wav";
    writeFile(path);
    result.items.append({{{"id", id}, {"name", id}, {"path", path}, {"durationMs", 60000}}, {}, {}});
  }
  return result;
}
QString status(const TranscriptionBatch &batch, int index) {
  return batch.jobs().at(index).toMap().value("status").toString();
}
int started(const QString &folder) {
  return QDir(folder).entryList({"started-*"}, QDir::Files).size();
}
void releaseAll(const TranscriptionBatchRequest &request, const QString &folder) {
  for (const auto &item : request.items)
    writeFile(folder + "/release-" + item.media.value("id").toString());
}
void rename(TranscriptionBatchItem &item, const QString &folder, const QString &id) {
  item.media["id"] = id;
  item.media["path"] = folder + "/" + id + ".wav";
  writeFile(item.media.value("path").toString());
}
}

class TranscriptionBatchTests : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    if (!QFileInfo::exists(qEnvironmentVariable("CUTPRO_BATCH_TEST_PYTHON")))
      QSKIP("Set CUTPRO_BATCH_TEST_PYTHON to a bundled Python runtime");
  }
  void parallelClips_data() {
    QTest::addColumn<bool>("bulk");
    QTest::addColumn<int>("accounts");
    QTest::addColumn<int>("clipCount");
    QTest::addColumn<int>("capacity");
    QTest::newRow("one-account") << true << 1 << 3 << 1;
    QTest::newRow("four-accounts") << true << 4 << 6 << 4;
    QTest::newRow("five-accounts") << true << 5 << 7 << 5;
    QTest::newRow("five-videos-four-accounts") << true << 4 << 5 << 4;
    QTest::newRow("single-five-videos-five-accounts") << false << 5 << 5 << 1;
    QTest::newRow("no-ready-accounts") << true << 0 << 3 << 1;
  }
  void parallelClips() {
    QFETCH(bool, bulk);
    QFETCH(int, accounts);
    QFETCH(int, clipCount);
    QFETCH(int, capacity);
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    auto input = request(folder.path(), clipCount,
        GeminiWorkPolicy::parallelism({{"geminiBulkProcessing", bulk}}, accounts));
    QCOMPARE(input.concurrency, capacity);
    TranscriptionBatch batch;
    QSignalSpy sources(&batch, &TranscriptionBatch::sourceReady);
    bool oversubscribed = false;
    connect(&batch, &TranscriptionBatch::stateChanged, this, [&] {
      int active = 0;
      for (const auto &job : batch.jobs())
        active += job.toMap().value("status") == "Transcribing";
      oversubscribed |= active > capacity;
    });
    QElapsedTimer clock;
    clock.start();
    QVERIFY(batch.start(input));
    QVERIFY(clock.elapsed() < 500);
    QTRY_COMPARE_WITH_TIMEOUT(started(folder.path()), capacity, 5000);
    QVERIFY(batch.inProgress());
    QCOMPARE(status(batch, capacity), QStringLiteral("Queued"));
    const int firstFinish = capacity - 1;
    writeFile(folder.filePath(QStringLiteral("release-clip%1").arg(firstFinish)));
    QTRY_COMPARE_WITH_TIMEOUT(status(batch, firstFinish), QStringLiteral("Complete"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(started(folder.path()), capacity + 1, 5000);
    if (capacity > 1)
      QCOMPARE(status(batch, 0), QStringLiteral("Transcribing"));
    releaseAll(input, folder.path());
    QTRY_VERIFY_WITH_TIMEOUT(!batch.inProgress(), 5000);
    QVERIFY(!oversubscribed);
    QCOMPARE(sources.size(), input.items.size());
    for (const auto &result : sources) {
      QCOMPARE(result.at(1).toList().first().toMap().value("text").toString(), result.at(0).toString());
      QVERIFY(result.at(3).toBool());
    }
    for (const auto &job : batch.jobs()) {
      QCOMPARE(job.toMap().value("status").toString(), QStringLiteral("Complete"));
      QCOMPARE(job.toMap().value("progress").toDouble(), 1.0);
    }
    for (const QString &file : QDir(folder.path()).entryList({"started-*"}, QDir::Files)) {
      QFile marker(folder.filePath(file));
      QVERIFY(marker.open(QIODevice::ReadOnly));
      QCOMPARE(marker.readAll(), QByteArray("1"));
    }
  }
  void failureReleasesSlotAndContinuesOtherClips() {
    QTemporaryDir folder;
    auto input = request(folder.path(), 4, 2);
    rename(input.items[0], folder.path(), "fail0");
    TranscriptionBatch batch;
    QVERIFY(batch.start(input));
    QTRY_COMPARE_WITH_TIMEOUT(started(folder.path()), 2, 5000);
    writeFile(folder.filePath("release-fail0"));
    QTRY_COMPARE_WITH_TIMEOUT(status(batch, 0), QStringLiteral("Failed"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(started(folder.path()), 3, 5000);
    releaseAll(input, folder.path());
    QTRY_VERIFY_WITH_TIMEOUT(!batch.inProgress(), 5000);
    for (int i = 1; i < 4; ++i)
      QCOMPARE(status(batch, i), QStringLiteral("Complete"));
    QVERIFY(batch.status().contains("1 of 4"));
  }
  void cancelStopsActiveAndQueuedClipsAndKeepsPartialSpeech() {
    QTemporaryDir folder;
    auto input = request(folder.path(), 6, 4);
    rename(input.items[0], folder.path(), "retry0");
    input.items[0].plan.chunked = true;
    input.items[0].plan.windows = {{0, 30000, 0}, {30000, 30000, 3000}};
    TranscriptionBatch batch;
    QSignalSpy sources(&batch, &TranscriptionBatch::sourceReady);
    QVERIFY(batch.start(input));
    QTRY_COMPARE_WITH_TIMEOUT(started(folder.path()), 4, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!sources.isEmpty(), 5000);
    QCOMPARE(sources.first().at(0).toString(), QStringLiteral("retry0"));
    QTRY_VERIFY_WITH_TIMEOUT(batch.jobs().first().toMap().value("detail").toString().contains("waiting"), 3000);
    QVERIFY(!sources.first().at(3).toBool());
    batch.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!batch.inProgress(), 5000);
    QCOMPARE(started(folder.path()), 4);
    for (const auto &job : batch.jobs())
      QCOMPARE(job.toMap().value("status").toString(), QStringLiteral("Cancelled"));
    QVERIFY(batch.status().startsWith("Transcription cancelled"));
    QCOMPARE(sources.last().at(1).toList().first().toMap().value("text").toString(), QStringLiteral("retry0"));
  }
  void zeroExitWithoutAllWindowsIsIncomplete() {
    QTemporaryDir folder;
    auto input = request(folder.path(), 1, 1);
    rename(input.items[0], folder.path(), "incomplete0");
    input.items[0].plan.chunked = true;
    input.items[0].plan.windows = {{0, 30000, 0}, {30000, 30000, 3000}};
    releaseAll(input, folder.path());
    TranscriptionBatch batch;
    QSignalSpy sources(&batch, &TranscriptionBatch::sourceReady);
    QVERIFY(batch.start(input));
    QTRY_VERIFY_WITH_TIMEOUT(!batch.inProgress(), 5000);
    QCOMPARE(status(batch, 0), QStringLiteral("Failed"));
    QVERIFY(!sources.isEmpty());
    for (const auto &source : sources)
      QVERIFY(!source.at(3).toBool());
  }
  void chunkedRepliesKeepWindowOffsetsAndHandleFinalLineWithoutNewline() {
    QTemporaryDir folder;
    auto input = request(folder.path(), 1, 1);
    input.items[0].plan.chunked = true;
    input.items[0].plan.windows = {{0, 30000, 0}, {30000, 30000, 3000}};
    releaseAll(input, folder.path());
    TranscriptionBatch batch;
    QSignalSpy sources(&batch, &TranscriptionBatch::sourceReady);
    QVERIFY(batch.start(input));
    QTRY_VERIFY_WITH_TIMEOUT(!batch.inProgress(), 5000);
    QCOMPARE(status(batch, 0), QStringLiteral("Complete"));
    const auto segments = sources.last().at(1).toList();
    QCOMPARE(segments.size(), 2);
    QCOMPARE(segments.at(1).toMap().value("start").toDouble(), 30.0);
    QCOMPARE(sources.last().at(2).toString(), QStringLiteral("zh"));
    QVERIFY(sources.last().at(3).toBool());
  }
  void failedProcessStartDoesNotStrandQueuedJobs() {
    QTemporaryDir folder;
    auto input = request(folder.path(), 5, 4);
    input.program = folder.filePath("missing-python.exe");
    TranscriptionBatch batch;
    QVERIFY(batch.start(input));
    QTRY_VERIFY_WITH_TIMEOUT(!batch.inProgress(), 5000);
    for (const auto &job : batch.jobs())
      QCOMPARE(job.toMap().value("status").toString(), QStringLiteral("Failed"));
  }
  void cancelAlsoStopsWorkerChildren() {
#ifdef Q_OS_WIN
    QTemporaryDir folder;
    auto input = request(folder.path(), 1, 1);
    rename(input.items[0], folder.path(), "child0");
    TranscriptionBatch batch;
    QVERIFY(batch.start(input));
    QTRY_COMPARE_WITH_TIMEOUT(started(folder.path()), 1, 5000);
    QFile file(folder.filePath("child-child0"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const DWORD pid = file.readAll().toUInt();
    HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, pid);
    QVERIFY(child);
    batch.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!batch.inProgress(), 5000);
    const DWORD stopped = WaitForSingleObject(child, 3000);
    CloseHandle(child);
    QCOMPARE(stopped, DWORD(WAIT_OBJECT_0));
#else
    QSKIP("Windows process group regression");
#endif
  }
};
QTEST_GUILESS_MAIN(TranscriptionBatchTests)
#include "transcription_batch_tests.moc"
