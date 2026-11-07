#include "app/subtitles/translation_model_probe.h"

#include "app/subtitles/transcript_translator.h"
#include "app/media/python_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

TranslationModelProbe::TranslationModelProbe(QObject *parent)
    : QObject(parent) {
  connect(&m_process, &QProcess::stateChanged, this,
          [this](QProcess::ProcessState) { emit stateChanged(); });
  connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
            if (m_cancelRequested) {
              m_cancelRequested = false;
              m_status = QStringLiteral("Model check cancelled");
              emit stateChanged();
              emit finished(false, {}, {}, {}, m_status);
              return;
            }
            // The worker prints one JSON line; anything before it is a warning
            // from an import, which is not an answer and not an error either.
            const QByteArray output =
                m_process.readAllStandardOutput().trimmed().split('\n').last();
            const QJsonObject payload =
                QJsonDocument::fromJson(output).object();
            const bool ok = exitStatus == QProcess::NormalExit &&
                            exitCode == 0 &&
                            payload.value(QStringLiteral("ok")).toBool();
            QStringList models;
            QString label;
            QString baseUrl;
            QString message;
            if (ok) {
              const QJsonArray entries =
                  payload.value(QStringLiteral("models")).toArray();
              models.reserve(entries.size());
              for (const QJsonValue &entry : entries) {
                const QString name = entry.toString().trimmed();
                if (!name.isEmpty() && !models.contains(name))
                  models.append(name);
              }
              label = payload.value(QStringLiteral("label")).toString();
              baseUrl = payload.value(QStringLiteral("baseUrl")).toString();
              message = payload.value(QStringLiteral("message")).toString();
              if (message.isEmpty())
                message = QStringLiteral("Found %1 models").arg(models.size());
            } else {
              message = payload.value(QStringLiteral("error")).toString();
              if (message.isEmpty())
                message =
                    QString::fromUtf8(m_process.readAllStandardError()).trimmed();
              if (message.isEmpty())
                message = QStringLiteral("Could not read the model list.");
            }
            if (ok) {
              m_models = models;
              m_label = label;
              m_baseUrl = baseUrl;
            }
            m_status = message;
            emit stateChanged();
            emit finished(ok, models, label, baseUrl, message);
          });
}

bool TranslationModelProbe::inProgress() const {
  return m_process.state() != QProcess::NotRunning;
}

bool TranslationModelProbe::start(const QVariantMap &settings) {
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
  m_status = QStringLiteral("Checking available models...");
  m_process.setProcessEnvironment(translationProcessEnvironment(settings));
  m_process.start(python, {worker, QStringLiteral("--models")});
  if (!m_process.waitForStarted(1500)) {
    m_status = QStringLiteral("Translation worker could not be started");
    emit stateChanged();
    return false;
  }
  emit stateChanged();
  return true;
}

void TranslationModelProbe::cancel() {
  if (!inProgress())
    return;
  m_cancelRequested = true;
  m_process.kill();
}
