#include "app/subtitles/recap_generator.h"

#include "app/subtitles/transcript_translator.h"

#include "app/media/python_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTemporaryFile>

RecapGenerator::RecapGenerator(QObject *parent) : QObject(parent) {
  connect(&m_process, &QProcess::stateChanged, this,
          [this](QProcess::ProcessState) { emit stateChanged(); });
  connect(
      &m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
      this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_cancelRequested) {
          m_cancelRequested = false;
          m_status = QStringLiteral("Recap cancelled");
          removeInputFile();
          emit stateChanged();
          emit finished(false, {}, {});
          return;
        }
        const QByteArray output =
            m_process.readAllStandardOutput().trimmed().split('\n').last();
        const QJsonDocument document = QJsonDocument::fromJson(output);
        const bool success = exitStatus == QProcess::NormalExit &&
                             exitCode == 0 && document.isObject() &&
                             document.object().value("recap").isString();
        QString error;
        if (success) {
          m_recap = document.object().value("recap").toString();
          m_status = QStringLiteral("Recap ready");
        } else {
          error = document.isObject()
                      ? document.object().value("error").toString()
                      : QString();
          if (error.isEmpty())
            error = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
          if (error.isEmpty())
            error = QStringLiteral("Recap generation failed.");
          m_status = QStringLiteral("Recap failed");
        }
        removeInputFile();
        emit stateChanged();
        emit finished(success, m_recap, error);
      });
}

bool RecapGenerator::inProgress() const {
  return m_process.state() != QProcess::NotRunning;
}

void RecapGenerator::clearStatus() {
  if (inProgress() || m_status.isEmpty())
    return;
  m_status.clear();
  emit stateChanged();
}

bool RecapGenerator::start(const QVariantList &segments, const QString &language,
                           const QString &title, const QVariantMap &settings) {
  if (inProgress() || segments.isEmpty())
    return false;

  const QString python = PythonRuntime::executable(
      settings.value("pythonExecutable", "python").toString());
  const QString worker = QDir(QCoreApplication::applicationDirPath())
                             .filePath("tools/recap_transcript.py");
  if (python.isEmpty() || !QFileInfo::exists(worker)) {
    m_status = QStringLiteral("Recap worker is not available");
    emit stateChanged();
    return false;
  }

  QTemporaryFile input(QDir::tempPath() +
                       QStringLiteral("/cutpro-recap-XXXXXX.json"));
  input.setAutoRemove(false);
  if (!input.open()) {
    m_status = QStringLiteral("Could not prepare the transcript for recap");
    emit stateChanged();
    return false;
  }
  const QJsonObject payload{
      {QStringLiteral("segments"), QJsonArray::fromVariantList(segments)},
      {QStringLiteral("language"), language.isEmpty() ? QStringLiteral("en")
                                                      : language},
      {QStringLiteral("title"), title}};
  if (input.write(QJsonDocument(payload).toJson(QJsonDocument::Compact)) < 0) {
    input.setAutoRemove(true);
    m_status = QStringLiteral("Could not prepare the transcript for recap");
    emit stateChanged();
    return false;
  }
  m_inputPath = input.fileName();
  input.close();
  m_cancelRequested = false;
  m_recap.clear();
  m_status = QStringLiteral("Generating recap...");

  // The Gemini translator's environment, forced to the gemini provider so the
  // recap always uses the model/keys configured in Settings > Translation.
  QVariantMap recapSettings = settings;
  recapSettings.insert(QStringLiteral("translationProvider"),
                       QStringLiteral("gemini"));
  m_process.setProcessEnvironment(translationProcessEnvironment(recapSettings));
  m_process.start(python, {worker, m_inputPath});
  if (!m_process.waitForStarted(1500)) {
    m_status = QStringLiteral("Recap worker could not be started");
    removeInputFile();
    emit stateChanged();
    return false;
  }
  emit stateChanged();
  return true;
}

void RecapGenerator::cancel() {
  if (!inProgress())
    return;
  m_cancelRequested = true;
  m_process.kill();
}

void RecapGenerator::removeInputFile() {
  if (m_inputPath.isEmpty())
    return;
  QFile::remove(m_inputPath);
  m_inputPath.clear();
}
