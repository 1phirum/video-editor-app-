#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "core/module_api.h"

// Asks a translation endpoint which models it actually serves.
//
// Its own QProcess, deliberately: the model list is a settings-dialog question
// and has to be answerable while a transcript translation is still running, and
// TranscriptTranslator owns exactly one process - the run in flight.
//
// The answer is not guessed from the URL. An OpenAI-compatible gateway
// publishes GET <base>/models, and which names it accepts is the one thing a
// hard-coded list in the interface can never keep up with: every gateway serves
// a different catalogue, and the same gateway changes it without warning.
class CUTPRO_SUBTITLES_API TranslationModelProbe : public QObject {
  Q_OBJECT

public:
  explicit TranslationModelProbe(QObject *parent = nullptr);

  bool inProgress() const;
  QString status() const { return m_status; }
  QStringList models() const { return m_models; }
  // Display name derived from the endpoint's host ("GoRouter" for
  // gorouter.app/v1), empty until a probe has answered.
  QString label() const { return m_label; }
  // The endpoint the probe actually reached, with the scheme the user left out
  // filled in.
  QString baseUrl() const { return m_baseUrl; }

  // false when a probe is already running or the worker is missing; status()
  // then carries the reason.
  bool start(const QVariantMap &settings);
  void cancel();

signals:
  void stateChanged();
  void finished(bool ok, const QStringList &models, const QString &label,
                const QString &baseUrl, const QString &message);

private:
  QProcess m_process;
  QString m_status;
  QStringList m_models;
  QString m_label;
  QString m_baseUrl;
  bool m_cancelRequested = false;
};
