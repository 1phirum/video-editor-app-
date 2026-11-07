#include "app/subtitles/transcript_translator.h"
#include "app/subtitles/gemini_work_policy.h"

#include "app/core_app/pool_manager.h"

#include "app/media/python_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QProcessEnvironment>

namespace {
QString languageName(const QString &code) {
  static const QHash<QString, QString> names = {
      {QStringLiteral("en"), QStringLiteral("English")},
      {QStringLiteral("zh-CN"), QStringLiteral("Chinese")},
      {QStringLiteral("km"), QStringLiteral("Khmer")},
      {QStringLiteral("es"), QStringLiteral("Spanish")},
      {QStringLiteral("fr"), QStringLiteral("French")},
      {QStringLiteral("de"), QStringLiteral("German")},
      {QStringLiteral("ja"), QStringLiteral("Japanese")},
      {QStringLiteral("ko"), QStringLiteral("Korean")},
      {QStringLiteral("vi"), QStringLiteral("Vietnamese")},
      {QStringLiteral("th"), QStringLiteral("Thai")},
      {QStringLiteral("id"), QStringLiteral("Indonesian")},
      {QStringLiteral("pt"), QStringLiteral("Portuguese")},
      {QStringLiteral("ru"), QStringLiteral("Russian")},
      {QStringLiteral("it"), QStringLiteral("Italian")},
      {QStringLiteral("hi"), QStringLiteral("Hindi")},
      {QStringLiteral("ar"), QStringLiteral("Arabic")},
  };
  return names.value(code);
}

void applyTranslationEnvironment(QProcess *process,
                                 const QVariantMap &settings) {
  process->setProcessEnvironment(translationProcessEnvironment(settings));
}
} // namespace

QProcessEnvironment
translationProcessEnvironment(const QVariantMap &settings) {
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  const QString provider = settings.value("translationProvider", "free").toString();
  QString model = settings.value("translationModel").toString();
  QString baseUrl = settings.value("translationBaseUrl").toString();
  QString apiKeys = settings.value("translationApiKeys").toString();
  if (provider == QStringLiteral("gemini")) {
    model = settings.value("geminiModel", settings.value("translationGeminiModel", model)).toString();
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
  // The account pool, when it is on and has ready accounts: the translation
  // worker tries the pool's loopback endpoint first and falls back to keys.
  const bool usePool = provider == QStringLiteral("gemini") && settings.value("geminiUsePool", true).toBool();
  const QString poolEndpoint = usePool ? cutpro::pool::endpointIfReady() : QString();
  environment.insert(QStringLiteral("CUTPRO_POOL_MODE"), usePool ? QStringLiteral("1") : QStringLiteral("0"));
  if (!poolEndpoint.isEmpty()) {
    environment.insert(QStringLiteral("CUTPRO_POOL_ENDPOINT"), poolEndpoint);
    const int ready = PoolManager::instance().state().value("readyCount").toInt();
    environment.insert(QStringLiteral("CUTPRO_POOL_PARALLELISM"),
        QString::number(GeminiWorkPolicy::parallelism(settings, ready)));
  } else {
    environment.remove(QStringLiteral("CUTPRO_POOL_ENDPOINT"));
    environment.insert(QStringLiteral("CUTPRO_POOL_PARALLELISM"), QStringLiteral("1"));
  }
  return environment;
}

TranscriptTranslator::TranscriptTranslator(QObject *parent) : QObject(parent) {
  connect(&m_process, &QProcess::readyReadStandardOutput, this,
          [this] { readOutput(); });
  connect(&m_process, &QProcess::stateChanged, this,
          [this](QProcess::ProcessState) { emit stateChanged(); });
  connect(
      &m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
      this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        readOutput(true);
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
        const QJsonDocument document = QJsonDocument::fromJson(m_result);
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
          m_preview = segments;
          m_completedSegments.clear();
          for (int index = 0; index < segments.size(); ++index)
            m_completedSegments.insert(index);
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

void TranscriptTranslator::readOutput(bool flush) {
  m_outputBuffer += m_process.readAllStandardOutput();
  while (!m_outputBuffer.isEmpty()) {
    const auto newline = m_outputBuffer.indexOf('\n');
    if (newline < 0 && !flush)
      break;
    const QByteArray line = (newline < 0 ? m_outputBuffer : m_outputBuffer.left(newline)).trimmed();
    m_outputBuffer.remove(0, newline < 0 ? m_outputBuffer.size() : newline + 1);
    const QJsonDocument document = QJsonDocument::fromJson(line);
    if (!document.isObject())
      continue;
    if (document.object().value("type").toString() == QStringLiteral("status")) {
      if (!m_cancelRequested) {
        m_status = document.object().value("message").toString();
        emit stateChanged();
      }
    } else if (document.object().value("type").toString() == QStringLiteral("progress")) {
      if (m_cancelRequested || m_testRequested)
        continue;
      const QJsonObject event = document.object();
      const int offset = event.value("offset").toInt(-1);
      const QJsonArray texts = event.value("texts").toArray();
      if (offset < 0 || offset >= m_preview.size() || texts.isEmpty() ||
          texts.size() > m_preview.size() - offset)
        continue;
      bool valid = true;
      for (const auto &text : texts) {
        if (!text.isString() || text.toString().trimmed().isEmpty()) {
          valid = false;
          break;
        }
      }
      if (!valid)
        continue;
      for (int index = 0; index < texts.size(); ++index) {
        QVariantMap segment = m_preview.at(offset + index).toMap();
        segment[QStringLiteral("text")] = texts.at(index).toString();
        m_preview[offset + index] = segment;
        m_completedSegments.insert(offset + index);
      }
      m_status = QStringLiteral("Translating %1/%2 segments")
                     .arg(completedSegments()).arg(totalSegments());
      emit stateChanged();
    } else {
      m_result = line;
    }
  }
}

bool TranscriptTranslator::inProgress() const {
  return m_process.state() != QProcess::NotRunning;
}

void TranscriptTranslator::clearStatus() {
  if (inProgress() || m_status.isEmpty())
    return;
  m_status.clear();
  m_preview.clear();
  m_completedSegments.clear();
  emit stateChanged();
}

bool TranscriptTranslator::start(const QVariantList &segments,
                                 const QString &targetLanguage,
                                 const QVariantMap &settings) {
  const QString targetName = languageName(targetLanguage);
  if (inProgress() || segments.isEmpty() || targetName.isEmpty())
    return false;

  const QString python = PythonRuntime::executable(
      settings.value("pythonExecutable", "python").toString());
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
  m_preview = segments;
  m_completedSegments.clear();
  m_outputBuffer.clear();
  m_result.clear();
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
  const QString python = PythonRuntime::executable(
      settings.value("pythonExecutable", "python").toString());
  const QString worker = QDir(QCoreApplication::applicationDirPath())
                             .filePath("tools/translate_transcript.py");
  if (python.isEmpty() || !QFileInfo::exists(worker)) {
    m_status = QStringLiteral("Translation worker is not available");
    emit stateChanged();
    return false;
  }
  m_cancelRequested = false;
  m_testRequested = true;
  m_preview.clear();
  m_completedSegments.clear();
  m_outputBuffer.clear();
  m_result.clear();
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
