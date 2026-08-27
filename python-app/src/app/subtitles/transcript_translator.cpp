#include "app/subtitles/transcript_translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QProcessEnvironment>

namespace {
QString languageName(const QString &code) {
  if (code == QStringLiteral("en"))
    return QStringLiteral("English");
  if (code == QStringLiteral("zh-CN"))
    return QStringLiteral("Chinese");
  if (code == QStringLiteral("km"))
    return QStringLiteral("Khmer");
  if (code == QStringLiteral("es"))
    return QStringLiteral("Spanish");
  return {};
}

void applyTranslationEnvironment(QProcess *process,
                                 const QVariantMap &settings) {
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  const QString provider = settings.value("translationProvider", "free").toString();
  QString model = settings.value("translationModel").toString();
  QString baseUrl = settings.value("translationBaseUrl").toString();
  QString apiKeys = settings.value("translationApiKeys").toString();
  if (provider == QStringLiteral("gemini")) {
    model = settings.value("translationGeminiModel", model).toString();
    apiKeys = settings.value("translationGeminiApiKeys", apiKeys).toString();
  } else if (provider == QStringLiteral("openai_compatible")) {
    model = settings.value("translationTabitokenModel", model).toString();
    baseUrl = settings.value("translationTabitokenBaseUrl", baseUrl).toString();
    apiKeys = settings.value("translationTabitokenApiKeys", apiKeys).toString();
  }
  environment.insert(QStringLiteral("CUTPRO_TRANSLATION_PROVIDER"),
                     provider);
  environment.insert(QStringLiteral("CUTPRO_TRANSLATION_MODEL"),
                     model);
  environment.insert(QStringLiteral("CUTPRO_TRANSLATION_BASE_URL"),
                     baseUrl);
  environment.insert(QStringLiteral("CUTPRO_TRANSLATION_API_KEYS"),
                     apiKeys);
  process->setProcessEnvironment(environment);
}
} // namespace

TranscriptTranslator::TranscriptTranslator(QObject *parent) : QObject(parent) {
  connect(&m_process, &QProcess::stateChanged, this,
          [this](QProcess::ProcessState) { emit stateChanged(); });
  connect(
      &m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
      this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_cancelRequested) {
          m_cancelRequested = false;
          m_status = QStringLiteral("Translation cancelled");
          removeInputFile();
          if (m_testRequested) {
            m_testRequested = false;
            emit stateChanged();
            emit testFinished(false, m_status);
            return;
          }
          emit stateChanged();
          emit finished(false, {}, m_targetLanguage, {});
          return;
        }
        const QByteArray output =
            m_process.readAllStandardOutput().trimmed().split('\n').last();
        const QJsonDocument document = QJsonDocument::fromJson(output);
        if (m_testRequested) {
          const bool success = exitStatus == QProcess::NormalExit &&
                               exitCode == 0 && document.isObject() &&
                               document.object().value("ok").toBool();
          QString message = document.isObject()
                                ? document.object().value(success ? "message" : "error").toString()
                                : QString();
          if (message.isEmpty())
            message = success ? QStringLiteral("Provider connection successful")
                              : QStringLiteral("Provider connection failed");
          m_testRequested = false;
          m_status = message;
          removeInputFile();
          emit stateChanged();
          emit testFinished(success, message);
          return;
        }
        const bool success = exitStatus == QProcess::NormalExit &&
                             exitCode == 0 && document.isObject() &&
                             document.object().value("segments").isArray();
        QVariantList segments;
        QString error;
        if (success) {
          segments =
              document.object().value("segments").toArray().toVariantList();
          m_status = QStringLiteral("Translated to %1")
                         .arg(languageName(m_targetLanguage));
        } else {
          error = document.isObject()
                      ? document.object().value("error").toString()
                      : QString();
          if (error.isEmpty())
            error =
                QString::fromUtf8(m_process.readAllStandardError()).trimmed();
          if (error.isEmpty())
            error = QStringLiteral("Subtitle translation failed.");
          m_status = QStringLiteral("Translation failed");
        }
        removeInputFile();
        emit stateChanged();
        emit finished(success, segments, m_targetLanguage, error);
      });
}

bool TranscriptTranslator::inProgress() const {
  return m_process.state() != QProcess::NotRunning;
}

void TranscriptTranslator::clearStatus() {
  if (inProgress() || m_status.isEmpty())
    return;
  m_status.clear();
  emit stateChanged();
}

bool TranscriptTranslator::start(const QVariantList &segments,
                                 const QString &targetLanguage,
                                 const QVariantMap &settings) {
  const QString targetName = languageName(targetLanguage);
  if (inProgress() || segments.isEmpty() || targetName.isEmpty())
    return false;

  const QString configuredPython =
      settings.value("pythonExecutable", "python").toString().trimmed();
  const QString python = QStandardPaths::findExecutable(configuredPython);
  const QString worker = QDir(QCoreApplication::applicationDirPath())
                             .filePath("tools/translate_transcript.py");
  if (python.isEmpty() || !QFileInfo::exists(worker)) {
    m_status = QStringLiteral("Translation worker is not available");
    emit stateChanged();
    return false;
  }

  QTemporaryFile input(QDir::tempPath() +
                       QStringLiteral("/cutpro-translation-XXXXXX.json"));
  input.setAutoRemove(false);
  if (!input.open()) {
    m_status = QStringLiteral("Could not prepare subtitles for translation");
    emit stateChanged();
    return false;
  }
  const QJsonObject payload{
      {QStringLiteral("segments"), QJsonArray::fromVariantList(segments)},
      {QStringLiteral("target"), targetLanguage}};
  if (input.write(QJsonDocument(payload).toJson(QJsonDocument::Compact)) < 0) {
    input.setAutoRemove(true);
    m_status = QStringLiteral("Could not prepare subtitles for translation");
    emit stateChanged();
    return false;
  }
  m_inputPath = input.fileName();
  input.close();
  m_cancelRequested = false;
  m_testRequested = false;
  m_targetLanguage = targetLanguage;
  m_status = QStringLiteral("Translating to %1...").arg(targetName);
  applyTranslationEnvironment(&m_process, settings);
  m_process.start(python, {worker, m_inputPath});
  if (!m_process.waitForStarted(1500)) {
    m_status = QStringLiteral("Translation worker could not be started");
    removeInputFile();
    emit stateChanged();
    return false;
  }
  emit stateChanged();
  return true;
}

bool TranscriptTranslator::test(const QVariantMap &settings) {
  if (inProgress())
    return false;
  const QString configuredPython =
      settings.value("pythonExecutable", "python").toString().trimmed();
  const QString python = QStandardPaths::findExecutable(configuredPython);
  const QString worker = QDir(QCoreApplication::applicationDirPath())
                             .filePath("tools/translate_transcript.py");
  if (python.isEmpty() || !QFileInfo::exists(worker)) {
    m_status = QStringLiteral("Translation worker is not available");
    emit stateChanged();
    return false;
  }
  m_cancelRequested = false;
  m_testRequested = true;
  m_status = QStringLiteral("Testing translation provider...");
  applyTranslationEnvironment(&m_process, settings);
  m_process.start(python, {worker, QStringLiteral("--test")});
  if (!m_process.waitForStarted(1500)) {
    m_testRequested = false;
    m_status = QStringLiteral("Translation worker could not be started");
    emit stateChanged();
    return false;
  }
  emit stateChanged();
  return true;
}

void TranscriptTranslator::cancel() {
  if (!inProgress())
    return;
  m_cancelRequested = true;
  m_process.kill();
}

void TranscriptTranslator::removeInputFile() {
  if (m_inputPath.isEmpty())
    return;
  QFile::remove(m_inputPath);
  m_inputPath.clear();
}
