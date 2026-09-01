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
  // Complete lines only, and each line exactly once. The old reader appended to a
  // buffer and re-scanned the whole thing on every readyRead, so a run that prints
  // one line per cue re-read its own output tens of thousands of times.
  void consumeStdout();
  void handleWorkerLine(const QByteArray &line);
  // The result is a file the worker names on stdout, not a line of stdout. A
  // twenty-thousand-cue manifest is megabytes of JSON, which is not something to
  // carry through a pipe and then search for.
  QVariantList readManifest(QString *error) const;

  QProcess m_process;
  QString m_outputPath;
  QString m_status;
  // Only the bytes after the last newline seen so far.
  QByteArray m_stdoutTail;
  // The last JSON object the worker printed: its final verdict.
  QByteArray m_resultLine;
  QString m_manifestPath;
  double m_progress = 0.0;
  QString m_requestPath;
  bool m_cancelRequested = false;
  bool m_finishHandled = false;
  bool m_importing = false;
  int m_segmentCount = 0;
};
