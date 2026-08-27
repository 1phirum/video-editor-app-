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
  connect(&m_process, &QProcess::readyReadStandardOutput, this, [this]() {
    m_stdout += m_process.readAllStandardOutput();
    const QList<QByteArray> lines = m_stdout.split('\n');
    for (const QByteArray &line : lines) {
      const QByteArray trimmed = line.trimmed();
      if (!trimmed.startsWith("PROGRESS "))
        continue;
      bool ok = false;
      const double value = trimmed.mid(9).toDouble(&ok);
      if (ok) {
        const double bounded = qBound(0.0, value, 0.99);
        const int completed =
            qMin(m_segmentCount,
                 qMax(0, qRound(bounded * qMax(1, m_segmentCount))));
        setState(bounded,
                 QStringLiteral("Generating subtitle voice %1 of %2...")
                     .arg(completed)
                     .arg(m_segmentCount));
      }
    }
  });
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
            if (m_cancelRequested) {
              finish(false, {}, QStringLiteral("Voice generation cancelled"));
              return;
            }
            const bool processOk = status == QProcess::NormalExit && code == 0;
            QVariantList outputs;
            QString error;
            const QList<QByteArray> lines = m_stdout.split('\n');
            for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
              const QJsonDocument document = QJsonDocument::fromJson(it->trimmed());
              if (!document.isObject())
                continue;
              const QJsonObject object = document.object();
              outputs = object.value(QStringLiteral("outputs"))
                            .toArray()
                            .toVariantList();
              error = object.value(QStringLiteral("error")).toString();
              break;
            }
            bool filesExist = !outputs.isEmpty();
            for (const QVariant &value : outputs) {
              if (!QFileInfo::exists(value.toMap().value("path").toString())) {
                filesExist = false;
                break;
              }
            }
            const bool success = processOk && filesExist;
            if (!success && error.isEmpty())
              error = QStringLiteral("Text-to-speech failed.");
            finish(success, success ? outputs : QVariantList{}, error);
          });
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

  m_stdout.clear();
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
