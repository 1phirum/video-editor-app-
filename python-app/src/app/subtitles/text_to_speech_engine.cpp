#include "app/subtitles/text_to_speech_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString resolvePython(const QString &configured) {
  QString executable = configured.trimmed();
  if (executable.isEmpty())
    executable = QStringLiteral("python");
  QString resolved = QStandardPaths::findExecutable(executable);
  if (!resolved.isEmpty())
    return resolved;
  if (QFileInfo::exists(executable))
    return QFileInfo(executable).absoluteFilePath();
  return {};
}
}

TextToSpeechEngine::TextToSpeechEngine(QObject *parent) : QObject(parent) {
  connect(&m_process, &QProcess::readyReadStandardOutput, this,
          [this]() { consumeStdout(); });
  connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
    const QString error = QString::fromLocal8Bit(m_process.readAllStandardError())
                              .trimmed();
    if (!error.isEmpty())
      setState(m_progress, error.left(300));
  });
  connect(&m_process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError) {
            if (m_cancelRequested)
              return;
            finish(false, {}, m_process.errorString());
          });
  connect(&m_process,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
            consumeStdout();
            // The last line may have arrived without its newline.
            if (!m_stdoutTail.trimmed().isEmpty())
              handleWorkerLine(m_stdoutTail);
            m_stdoutTail.clear();
            if (m_cancelRequested) {
              finish(false, {}, QStringLiteral("Voice generation cancelled"));
              return;
            }
            const bool processOk = status == QProcess::NormalExit && code == 0;
            QString error;
            if (!m_resultLine.isEmpty()) {
              const QJsonObject object =
                  QJsonDocument::fromJson(m_resultLine).object();
              error = object.value(QStringLiteral("error")).toString();
            }
            QVariantList outputs;
            if (processOk)
              outputs = readManifest(&error);
            const bool success = processOk && !outputs.isEmpty();
            if (!success && error.isEmpty())
              error = QStringLiteral("Text-to-speech failed.");
            finish(success, success ? outputs : QVariantList{}, error);
          });
}

void TextToSpeechEngine::consumeStdout() {
  m_stdoutTail += m_process.readAllStandardOutput();
  int start = 0;
  for (;;) {
    const int newline = m_stdoutTail.indexOf('\n', start);
    if (newline < 0)
      break;
    // mid(), not fromRawData(): a line that needs no trimming would otherwise be
    // kept as a view into a buffer this function is about to shift underneath it.
    handleWorkerLine(m_stdoutTail.mid(start, newline - start));
    start = newline + 1;
  }
  if (start > 0)
    m_stdoutTail.remove(0, start);
}

void TextToSpeechEngine::handleWorkerLine(const QByteArray &line) {
  const QByteArray trimmed = line.trimmed();
  if (trimmed.isEmpty())
    return;
  if (trimmed.startsWith("PROGRESS ")) {
    // "PROGRESS <fraction> [done] [total]". The counts are authoritative when the
    // worker sends them: with requests overlapping, completions no longer arrive in
    // order, so an index reconstructed from the fraction would jitter backwards.
    const QList<QByteArray> fields = trimmed.simplified().split(' ');
    bool ok = false;
    const double value = fields.value(1).toDouble(&ok);
    if (!ok)
      return;
    const double bounded = qBound(0.0, value, 0.99);
    int done = -1;
    int total = m_segmentCount;
    if (fields.size() >= 4) {
      bool doneOk = false;
      bool totalOk = false;
      const int parsedDone = fields.at(2).toInt(&doneOk);
      const int parsedTotal = fields.at(3).toInt(&totalOk);
      if (doneOk && totalOk && parsedTotal > 0) {
        done = parsedDone;
        total = parsedTotal;
      }
    }
    if (done < 0)
      done = qRound(bounded * qMax(1, total));
    setState(bounded, QStringLiteral("Generating subtitle voice %1 of %2...")
                          .arg(qBound(0, done, total))
                          .arg(total));
    return;
  }
  if (trimmed.startsWith("MANIFEST ")) {
    m_manifestPath = QString::fromUtf8(trimmed.mid(9)).trimmed();
    return;
  }
  if (trimmed.startsWith('{'))
    m_resultLine = trimmed;
}

QVariantList TextToSpeechEngine::readManifest(QString *error) const {
  if (m_manifestPath.isEmpty()) {
    // No manifest line at all: fall back to a result object that carried the list
    // inline, so an older worker on disk still works.
    if (!m_resultLine.isEmpty())
      return QJsonDocument::fromJson(m_resultLine)
          .object()
          .value(QStringLiteral("outputs"))
          .toArray()
          .toVariantList();
    if (error && error->isEmpty())
      *error = QStringLiteral("Text-to-speech produced no manifest.");
    return {};
  }
  QFile file(m_manifestPath);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error && error->isEmpty())
      *error = QStringLiteral("Could not read the voice manifest: %1")
                   .arg(file.errorString());
    return {};
  }
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();
  if (!document.isObject()) {
    if (error && error->isEmpty())
      *error = QStringLiteral("The voice manifest is not readable: %1")
                   .arg(parseError.errorString());
    return {};
  }
  const QJsonObject object = document.object();
  if (error && error->isEmpty())
    *error = object.value(QStringLiteral("error")).toString();
  // The worker already verified that every one of these exists and is long enough
  // to be speech, and it fails loudly when one does not. Re-stat'ing twenty
  // thousand files here only moved that work onto the GUI thread.
  return object.value(QStringLiteral("outputs")).toArray().toVariantList();
}

bool TextToSpeechEngine::inProgress() const {
  return m_process.state() != QProcess::NotRunning || m_importing;
}

void TextToSpeechEngine::beginTimelineImport(int segmentCount) {
  m_importing = true;
  m_cancelRequested = false;
  setState(0.95, QStringLiteral("Adding voice clips to the timeline 0 of %1...")
                     .arg(qMax(0, segmentCount)));
}

void TextToSpeechEngine::updateTimelineImport(int completed, int total) {
  const int safeTotal = qMax(1, total);
  const int safeCompleted = qBound(0, completed, safeTotal);
  const double progress = 0.95 + 0.05 * safeCompleted / safeTotal;
  setState(qBound(0.95, progress, 0.999),
           QStringLiteral("Adding voice clips to the timeline %1 of %2...")
               .arg(safeCompleted)
               .arg(total));
}

void TextToSpeechEngine::finishTimelineImport(bool success, int imported,
                                               const QString &error) {
  m_importing = false;
  setState(success ? 1.0 : 0.0,
           success ? QStringLiteral("Added %1 timed voice clips")
                         .arg(imported)
                   : error);
}

void TextToSpeechEngine::setState(double progress, const QString &status) {
  m_progress = progress;
  m_status = status;
  emit stateChanged();
}

void TextToSpeechEngine::finish(bool success, const QVariantList &outputs,
                                const QString &error) {
  if (m_finishHandled)
    return;
  m_finishHandled = true;
  if (!m_requestPath.isEmpty()) {
    QFile::remove(m_requestPath);
    m_requestPath.clear();
  }
  m_outputPath = success && !outputs.isEmpty()
                     ? outputs.first().toMap().value("path").toString()
                     : QString();
  setState(success ? 1.0 : 0.0,
           success ? QStringLiteral("Generated %1 timed voice clips")
                         .arg(outputs.size())
                   : error);
  emit finished(success, success ? outputs : QVariantList{}, error);
}

bool TextToSpeechEngine::generateSegments(
    const QVariantList &segments, const QString &language,
    const QString &gender, const QString &python, const QString &worker,
    const QString &outputDir, const QString &ffmpeg) {
  if (inProgress())
    return false;
  if (segments.isEmpty()) {
    setState(0.0, QStringLiteral("Add subtitles to the timeline first."));
    return false;
  }
  if (!QFileInfo::exists(worker)) {
    setState(0.0, QStringLiteral("Text-to-speech worker is missing."));
    return false;
  }
  const QString executable = resolvePython(python);
  if (executable.isEmpty()) {
    setState(0.0, QStringLiteral("Python is not available for text-to-speech."));
    return false;
  }
  if (!QDir().mkpath(outputDir)) {
    setState(0.0, QStringLiteral("Could not create the voice output folder."));
    return false;
  }

  m_stdoutTail.clear();
  m_resultLine.clear();
  m_manifestPath.clear();
  m_outputPath.clear();
  m_cancelRequested = false;
  m_finishHandled = false;
  m_segmentCount = segments.size();
  m_requestPath =
      QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
          .filePath(QStringLiteral("cutpro-tts-%1.json")
                        .arg(QUuid::createUuid().toString(
                            QUuid::WithoutBraces)));
  QFile request(m_requestPath);
  if (!request.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_requestPath.clear();
    setState(0.0, QStringLiteral("Could not prepare the voice request."));
    return false;
  }
  request.write(
      QJsonDocument(QJsonObject{
                        {QStringLiteral("segments"),
                         QJsonArray::fromVariantList(segments)},
                        {QStringLiteral("language"), language},
                        {QStringLiteral("gender"), gender},
                        {QStringLiteral("ffmpeg"), ffmpeg}})
          .toJson(QJsonDocument::Compact));
  request.close();
  setState(0.02, QStringLiteral("Preparing voice..."));
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  // A broken PYTHONHOME/PYTHONPATH must not redirect the configured Python to
  // the application's source tree. The worker uses the selected interpreter's
  // normal site-packages, including edge-tts.
  environment.remove(QStringLiteral("PYTHONHOME"));
  environment.remove(QStringLiteral("PYTHONPATH"));
  m_process.setProcessEnvironment(environment);
  m_process.start(executable, {worker, m_requestPath, outputDir});
  if (!m_process.waitForStarted(1500)) {
    setState(0.0, QStringLiteral("Could not start text-to-speech."));
    return false;
  }
  return true;
}

void TextToSpeechEngine::cancel() {
  if (!inProgress())
    return;
  m_cancelRequested = true;
  setState(m_progress, m_importing
                           ? QStringLiteral("Cancelling timeline import...")
                           : QStringLiteral("Cancelling voice generation..."));
  if (m_process.state() != QProcess::NotRunning)
    m_process.kill();
}
