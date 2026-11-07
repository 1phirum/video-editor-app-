#pragma once

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include "core/module_api.h"

// The CUTPRO_TRANSLATION_* variables tools/translate_transcript.py reads.
// Shared with TranslationModelProbe so a model list is always fetched from
// exactly the endpoint the next translation would use.
CUTPRO_SUBTITLES_API QProcessEnvironment
translationProcessEnvironment(const QVariantMap &settings);

class CUTPRO_SUBTITLES_API TranscriptTranslator : public QObject {
  Q_OBJECT

public:
  explicit TranscriptTranslator(QObject *parent = nullptr);

  bool inProgress() const;
  bool testInProgress() const { return m_testRequested; }
  QString status() const { return m_status; }
  QVariantList preview() const { return m_preview; }
  int completedSegments() const { return int(m_completedSegments.size()); }
  int totalSegments() const { return int(m_preview.size()); }
  void clearStatus();
  bool start(const QVariantList &segments, const QString &targetLanguage,
             const QVariantMap &settings = {});
  bool test(const QVariantMap &settings);
  void cancel();

signals:
  void stateChanged();
  void finished(bool success, const QVariantList &segments,
                const QString &targetLanguage, const QString &error);
  void testFinished(bool success, const QString &message);

private:
  void removeInputFile();
  void readOutput(bool flush = false);

  QProcess m_process;
  QByteArray m_outputBuffer;
  QByteArray m_result;
  QString m_inputPath;
  QString m_targetLanguage;
  QString m_status;
  QVariantList m_preview;
  QSet<int> m_completedSegments;
  bool m_cancelRequested = false;
  bool m_testRequested = false;
};
