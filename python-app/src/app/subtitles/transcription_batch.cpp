#include "app/subtitles/transcription_batch.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

struct TranscriptionBatch::Job {
  TranscriptionBatchItem item;
  QProcess process;
  std::unique_ptr<QTemporaryDir> scratch;
  QByteArray output;
  QByteArray stderrTail;
  QVariantList segments;
  QString language;
  QString status = QStringLiteral("Queued");
  QString detail;
  QString error;
  QSet<int> completedWindows;
  bool singleReply = false;
  bool done = false;
  bool finished = false;
  double progress = 0;
#ifdef Q_OS_WIN
  HANDLE processGroup = nullptr;
  void closeProcessGroup() {
    if (processGroup) {
      CloseHandle(processGroup);
      processGroup = nullptr;
    }
  }
  ~Job() { closeProcessGroup(); }
#endif
};

TranscriptionBatch::TranscriptionBatch(QObject *parent) : QObject(parent) {}
TranscriptionBatch::~TranscriptionBatch() {
  // No callbacks may touch Backend during teardown. Scratch folders are owned
  // by these jobs and must outlive their workers, including a cancelled run.
  for (auto &job : m_jobs) {
    disconnect(&job->process, nullptr, this, nullptr);
#ifdef Q_OS_WIN
    job->closeProcessGroup();
#endif
    if (job->process.state() != QProcess::NotRunning) {
      job->process.kill();
      job->process.waitForFinished(2000);
    }
  }
}

bool TranscriptionBatch::start(const TranscriptionBatchRequest &request) {
  if (m_busy || request.items.isEmpty())
    return false;
  m_jobs.clear();
  m_request = request;
  QSet<QString> seen;
  for (const auto &item : request.items) {
    const QString id = item.media.value("id").toString();
    if (id.isEmpty() || seen.contains(id))
      continue;
    seen.insert(id);
    auto job = std::make_unique<Job>();
    job->item = item;
    job->segments = item.resumeSegments;
    m_jobs.push_back(std::move(job));
  }
  if (m_jobs.empty())
    return false;
  m_request.concurrency = qBound(1, request.concurrency, int(m_jobs.size()));
  m_next = m_running = 0;
  m_cancelled = false;
  m_busy = true;
  emit stateChanged();
  pump();
  return true;
}

void TranscriptionBatch::clear() {
  if (m_busy)
    return;
  m_jobs.clear();
  m_cancelled = false;
  emit stateChanged();
}

void TranscriptionBatch::pump() {
  while (!m_cancelled && m_running < m_request.concurrency && m_next < int(m_jobs.size())) {
    Job &job = *m_jobs[m_next++];
    ++m_running;
    job.status = QStringLiteral("Transcribing");
    job.detail = tr("Starting transcription...");
    launch(job);
  }
  if (m_running == 0 && (m_cancelled || m_next == int(m_jobs.size())))
    m_busy = false;
  emit stateChanged();
}

void TranscriptionBatch::launch(Job &job) {
  QStringList args{m_request.worker};
  if (job.item.plan.chunked) {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (root.isEmpty() || !QDir().mkpath(root)) {
      job.error = tr("Could not create a transcription scratch folder.");
      finish(job, false);
      return;
    }
    job.scratch = std::make_unique<QTemporaryDir>(QDir(root).filePath("transcribe-batch-XXXXXX"));
    if (!job.scratch->isValid()) {
      job.error = tr("Could not create a transcription scratch folder.");
      finish(job, false);
      return;
    }
    QJsonArray windows;
    for (const auto &window : job.item.plan.windows)
      windows.append(QJsonObject{{"startMs", window.startMs}, {"lengthMs", window.lengthMs},
                                 {"leadInMs", window.leadInMs}});
    const QJsonObject document{{"source", job.item.media.value("path").toString()},
        {"model", m_request.model}, {"language", m_request.language},
        {"ffmpeg", m_request.ffmpeg}, {"tempDir", job.scratch->path()},
        // Each worker extracts its own audio. Sharing Backend's single
        // prefetcher would replace another clip's WAVs during a parallel run.
        {"prefetch", false}, {"segmentation", QJsonObject{{"maxMs", m_request.maxSegmentMs}}},
        {"windows", windows}};
    QFile file(QDir(job.scratch->path()).filePath("job.json"));
    const QByteArray bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
      job.error = tr("Could not write the transcription job file.");
      file.close();
      finish(job, false);
      return;
    }
    file.close();
    args << QStringLiteral("--job") << file.fileName();
  } else {
    args << job.item.media.value("path").toString() << m_request.model << m_request.language;
  }
  QProcessEnvironment env = m_request.environment;
  // The batch owns the account-sized concurrency budget. A clip must not also
  // fan out its windows and fill every account ahead of the other selected clips.
  env.insert(QStringLiteral("CUTPRO_POOL_PARALLELISM"), QStringLiteral("1"));
  env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
  job.process.setProcessEnvironment(env);
#ifdef Q_OS_WIN
  // Gemini launches ffmpeg children. Cancelling just the Python PID can leave
  // those children writing into a scratch folder the app is trying to remove.
  job.processGroup = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job.processGroup || !SetInformationJobObject(job.processGroup, JobObjectExtendedLimitInformation,
                                                   &limits, sizeof(limits))) {
    job.error = tr("Could not create a background transcription process group.");
    finish(job, false);
    return;
  }
  connect(&job.process, &QProcess::started, this, [&job] {
    HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(job.process.processId()));
    const bool attached = process && AssignProcessToJobObject(job.processGroup, process);
    if (process)
      CloseHandle(process);
    if (!attached) {
      job.error = QStringLiteral("Could not isolate the background transcription process.");
      job.process.kill();
    }
  });
  job.process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
    arguments->flags |= 0x08000000;
  });
#endif
  connect(&job.process, &QProcess::readyReadStandardOutput, this, [this, &job] { readOutput(job); });
  connect(&job.process, &QProcess::readyReadStandardError, this, [&job] {
    job.stderrTail = (job.stderrTail + job.process.readAllStandardError()).right(8192);
  });
  connect(&job.process, &QProcess::errorOccurred, this, [this, &job](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      job.error = tr("The transcription worker could not start.");
      finish(job, false);
    }
  });
  connect(&job.process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, &job](int code, QProcess::ExitStatus status) {
    finish(job, code == 0 && status == QProcess::NormalExit);
  });
  // Asynchronous start: launching four or five jobs must not block the GUI for
  // a waitForStarted timeout per clip.
  job.process.start(m_request.program, args);
}

void TranscriptionBatch::readOutput(Job &job, bool final) {
  job.output += job.process.readAllStandardOutput();
  while (!job.output.isEmpty()) {
    const int newline = job.output.indexOf('\n');
    if (newline < 0 && !final)
      break;
    const QByteArray line = newline < 0 ? job.output.trimmed() : job.output.left(newline).trimmed();
    job.output.remove(0, newline < 0 ? job.output.size() : newline + 1);
    const auto document = QJsonDocument::fromJson(line);
    if (document.isObject())
      handleEvent(job, document.object().toVariantMap());
  }
}

void TranscriptionBatch::handleEvent(Job &job, const QVariantMap &event) {
  const QString type = event.value("type").toString();
  if (event.contains("error"))
    job.error = event.value("error").toString();
  if (type.isEmpty() && event.contains("segments")) {
    job.singleReply = QJsonObject::fromVariantMap(event).value("segments").isArray();
    job.segments = event.value("segments").toList();
    job.language = event.value("language").toString();
  } else if (type == "status") {
    job.detail = event.value("message").toString();
  } else if (type == "window") {
    job.detail = tr("Transcribing %1/%2").arg(event.value("index").toInt() + 1)
                    .arg(job.item.plan.windows.size());
  } else if (type == "segments") {
    const int index = event.value("index", -1).toInt();
    if (index < 0 || index >= job.item.plan.windows.size() || job.completedWindows.contains(index))
      return;
    TranscriptionPlanner::mergeWindow(&job.segments, event.value("segments").toList(),
                                     job.item.plan.windows.at(index));
    job.completedWindows.insert(index);
    job.progress = std::min(0.999, double(job.completedWindows.size()) / job.item.plan.windows.size());
    emit sourceReady(job.item.media.value("id").toString(), job.segments, job.language, false);
  } else if (type == "done") {
    job.done = true;
    job.language = event.value("language").toString();
  }
  emit stateChanged();
}

void TranscriptionBatch::finish(Job &job, bool cleanExit) {
  if (job.finished)
    return;
  readOutput(job, true);
  job.stderrTail = (job.stderrTail + job.process.readAllStandardError()).right(8192);
  const bool complete = cleanExit && job.error.isEmpty() && !m_cancelled &&
      (job.item.plan.chunked ? job.done && job.completedWindows.size() == job.item.plan.windows.size()
                             : job.singleReply);
  job.finished = true;
  --m_running;
  job.status = complete ? QStringLiteral("Complete") : m_cancelled ? QStringLiteral("Cancelled")
                                                                   : QStringLiteral("Failed");
  if (complete) {
    job.progress = 1;
    job.detail = tr("Transcription complete");
  } else if (m_cancelled) {
    job.detail = tr("Cancelled by user");
  } else {
    job.detail = job.error.isEmpty() ? QString::fromUtf8(job.stderrTail).trimmed() : job.error;
    if (job.detail.isEmpty())
      job.detail = tr("The worker stopped without a complete transcript.");
  }
  if (complete || !job.segments.isEmpty())
    emit sourceReady(job.item.media.value("id").toString(), job.segments, job.language, complete);
  // Process has ended; only now may its job.json and window audio be removed.
#ifdef Q_OS_WIN
  job.closeProcessGroup();
#endif
  job.scratch.reset();
  emit stateChanged();
  emit jobFinished(complete, job.item.media.value("id").toString(),
                   job.status == "Failed" ? job.detail : QString());
  QTimer::singleShot(0, this, [this] { pump(); });
}

void TranscriptionBatch::cancel() {
  if (!m_busy)
    return;
  m_cancelled = true;
  for (auto &job : m_jobs) {
    if (job->status == "Queued") {
      job->status = QStringLiteral("Cancelled");
      job->detail = tr("Cancelled by user");
      job->finished = true;
    } else if (job->status == "Transcribing") {
#ifdef Q_OS_WIN
      job->closeProcessGroup();
#endif
      job->process.kill();
    }
  }
  pump();
}

QVariantList TranscriptionBatch::jobs() const {
  QVariantList result;
  for (const auto &job : m_jobs) {
    const auto &media = job->item.media;
    result.append(QVariantMap{{"id", media.value("id")}, {"name", media.value("name")},
        {"thumbnailUrl", media.value("thumbnailUrl")}, {"durationMs", media.value("durationMs")},
        {"status", job->status}, {"detail", job->detail}, {"progress", job->progress}});
  }
  return result;
}

QString TranscriptionBatch::status() const {
  if (m_jobs.empty())
    return {};
  int complete = 0, failed = 0, queued = 0;
  for (const auto &job : m_jobs) {
    complete += job->status == "Complete";
    failed += job->status == "Failed";
    queued += job->status == "Queued";
  }
  if (m_busy)
    return tr("Transcribing %1 clips · %2 active · %3 queued").arg(m_jobs.size()).arg(m_running).arg(queued);
  if (m_cancelled)
    return tr("Transcription cancelled · %1 clips complete").arg(complete);
  if (failed)
    return tr("Transcription failed for %1 of %2 clips").arg(failed).arg(m_jobs.size());
  return tr("Transcription complete · %1 clips").arg(complete);
}

double TranscriptionBatch::progress() const {
  double total = 0, completed = 0;
  for (const auto &job : m_jobs) {
    const double duration = qMax<qint64>(1, job->item.media.value("durationMs").toLongLong());
    total += duration;
    completed += duration * job->progress;
  }
  return total > 0 ? completed / total : 0;
}
