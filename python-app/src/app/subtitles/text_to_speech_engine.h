#pragma once

#include <QProcess>
#include <QString>
#include <QObject>
#include <QVariantList>

#include "core/module_api.h"

class CUTPRO_SUBTITLES_API TextToSpeechEngine final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool inProgress READ inProgress NOTIFY stateChanged)
  Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  Q_PROPERTY(QString outputPath READ outputPath NOTIFY stateChanged)

public:
  explicit TextToSpeechEngine(QObject *parent = nullptr);

  bool inProgress() const;
  double progress() const { return m_progress; }
  QString status() const { return m_status; }
  QString outputPath() const { return m_outputPath; }
  bool importCancellationRequested() const { return m_cancelRequested; }

  void beginTimelineImport(int segmentCount);
  void updateTimelineImport(int completed, int total);
  void finishTimelineImport(bool success, int imported,
                            const QString &error = QString());

  Q_INVOKABLE bool generateSegments(const QVariantList &segments,
                                    const QString &language,
                                    const QString &gender,
                                    const QString &python,
                                    const QString &worker,
                                    const QString &outputDir,
                                    const QString &ffmpeg);
  Q_INVOKABLE void cancel();

signals:
  void stateChanged();
  void finished(bool success, const QVariantList &outputs,
                const QString &error);

private:
  void setState(double progress, const QString &status);
  void finish(bool success, const QVariantList &outputs,
              const QString &error = QString());

  QProcess m_process;
  QString m_outputPath;
  QString m_status;
  QByteArray m_stdout;
  double m_progress = 0.0;
  QString m_requestPath;
  bool m_cancelRequested = false;
  bool m_finishHandled = false;
  bool m_importing = false;
  int m_segmentCount = 0;
};
