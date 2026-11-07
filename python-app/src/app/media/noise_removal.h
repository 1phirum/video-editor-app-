#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantMap>
#include "core/module_api.h"

// Offline processing only. The GUI owns this object; FFmpeg does the work in
// a child process and completion is delivered through the event loop.
class CUTPRO_MEDIA_API NoiseRemoval final : public QObject {
  Q_OBJECT
public:
  explicit NoiseRemoval(QObject *parent = nullptr);
  ~NoiseRemoval() override;
  bool busy() const { return m_busy; }
  double progress() const { return m_progress; }
  bool start(const QString &input, int strength, qint64 sourceInMs = 0,
             qint64 durationMs = 0, qint64 sourceDurationMs = 0);
  void cancel();
  static QString modelPath();
  static QString sourcePath(const QVariantMap &clip, const QVariantMap &media);
  // Empty means use the original media. Both preview and export use this rule.
  static QString replacementPath(const QVariantMap &clip,
                                 const QVariantMap &media);
signals:
  void changed();
  void finished(const QString &path, const QString &error);
private:
  void complete(int code, QProcess::ExitStatus status);
  QProcess m_process;
  bool m_busy = false;
  bool m_cancelled = false;
  double m_progress = 0;
  qint64 m_expectedMs = 0;
  QString m_output;
  QString m_temporary;
  QByteArray m_progressBuffer;
};
