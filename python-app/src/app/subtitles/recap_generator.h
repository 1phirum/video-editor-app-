#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "core/module_api.h"

// Turns a finished transcript into a narrator recap script via Gemini. It reuses
// the translator's CUTPRO_TRANSLATION_* environment (translationProcessEnvironment
// in transcript_translator.h), forcing the provider to gemini, so no new settings
// exist for the user to keep in step.
class CUTPRO_SUBTITLES_API RecapGenerator : public QObject {
  Q_OBJECT

public:
  explicit RecapGenerator(QObject *parent = nullptr);

  bool inProgress() const;
  QString status() const { return m_status; }
  QString recap() const { return m_recap; }
  void clearStatus();
  bool start(const QVariantList &segments, const QString &language,
             const QString &title, const QVariantMap &settings = {});
  void cancel();

signals:
  void stateChanged();
  void finished(bool success, const QString &recap, const QString &error);

private:
  void removeInputFile();

  QProcess m_process;
  QString m_inputPath;
  QString m_status;
  QString m_recap;
  bool m_cancelRequested = false;
};
