#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class TranscriptTranslator : public QObject {
  Q_OBJECT

public:
  explicit TranscriptTranslator(QObject *parent = nullptr);

  bool inProgress() const;
  bool testInProgress() const { return m_testRequested; }
  QString status() const { return m_status; }
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

  QProcess m_process;
  QString m_inputPath;
  QString m_targetLanguage;
  QString m_status;
  bool m_cancelRequested = false;
  bool m_testRequested = false;
};
