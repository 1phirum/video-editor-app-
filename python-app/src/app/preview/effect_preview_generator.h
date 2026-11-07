#pragma once

#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QObject>

#include "core/module_api.h"

// Generates effect-browser previews from the currently selected timeline clip.
// Work is serialized so hover events never start competing FFmpeg processes.
class CUTPRO_PREVIEW_API EffectPreviewGenerator final : public QObject {
  Q_OBJECT

public:
  explicit EffectPreviewGenerator(QObject *parent = nullptr);

  QString cachedPreview(const QString &mediaPath, qint64 sourcePositionMs,
                        qint64 sourceDurationMs, const QString &effectId,
                        bool animated) const;

  void request(const QString &clipId, const QString &mediaPath,
               const QString &mediaKind, qint64 sourcePositionMs,
               qint64 sourceDurationMs, const QString &effectId,
               bool animated);

signals:
  void previewReady(const QString &clipId, const QString &effectId,
                    bool animated, const QString &url);

private:
  struct Job {
    QString clipId;
    QString mediaPath;
    QString mediaKind;
    qint64 sourcePositionMs = 0;
    qint64 sourceDurationMs = 0;
    QString effectId;
    bool animated = false;
    QString key;
    QString outputPath;
  };

  static QString cachePath(const QString &mediaPath, qint64 sourcePositionMs,
                           qint64 sourceDurationMs, const QString &effectId,
                           bool animated);
  static QString filterFor(const QString &effectId);
  static QString ffmpegExecutable();
  void startNext();

  QQueue<Job> m_queue;
  QSet<QString> m_pending;
  QProcess m_process;
  Job m_current;
  bool m_running = false;
};
