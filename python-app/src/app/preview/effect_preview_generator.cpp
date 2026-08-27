#include "app/preview/effect_preview_generator.h"

#include "app/effects/effect_registry.h"
#include "app/effects/effect_stack.h"
#include "app/effects/video_effect_pipeline.h"
#include "app/media/ffmpeg_runtime.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

namespace {
QString seconds(qint64 ms) {
  return QString::number(qMax<qint64>(0, ms) / 1000.0, 'f', 3);
}
}

EffectPreviewGenerator::EffectPreviewGenerator(QObject *parent)
    : QObject(parent) {
  connect(&m_process,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
            const bool ok = status == QProcess::NormalExit && code == 0 &&
                            QFileInfo::exists(m_current.outputPath) &&
                            QFileInfo(m_current.outputPath).size() > 0;
            if (!ok)
              QFile::remove(m_current.outputPath);
            emit previewReady(
                m_current.clipId, m_current.effectId, m_current.animated,
                ok ? QUrl::fromLocalFile(m_current.outputPath).toString()
                   : QString());
            m_pending.remove(m_current.key);
            m_running = false;
            startNext();
          });
  connect(&m_process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError) {
            if (m_running && m_process.state() == QProcess::NotRunning) {
              QFile::remove(m_current.outputPath);
              emit previewReady(m_current.clipId, m_current.effectId,
                                m_current.animated, QString());
              m_pending.remove(m_current.key);
              m_running = false;
              startNext();
            }
          });
}

QString EffectPreviewGenerator::ffmpegExecutable() {
  return FfmpegRuntime::executable();
}

QString EffectPreviewGenerator::cachePath(const QString &mediaPath,
                                          qint64 sourcePositionMs,
                                          qint64 sourceDurationMs,
                                          const QString &effectId,
                                          bool animated) {
  const QFileInfo source(mediaPath);
  const QByteArray key = source.canonicalFilePath().toUtf8() +
                         QByteArray::number(source.lastModified().toMSecsSinceEpoch()) +
                         QByteArray::number(source.size()) +
                         QByteArray::number(sourcePositionMs / 250) +
                         QByteArray::number(sourceDurationMs) + effectId.toUtf8() +
                         (animated ? QByteArrayLiteral("motion-v1")
                                   : QByteArrayLiteral("still-v1"));
  const QString fileName = QString::fromLatin1(
                               QCryptographicHash::hash(key, QCryptographicHash::Sha256)
                                   .toHex())
                               .left(24) +
                           (animated ? QStringLiteral(".gif")
                                     : QStringLiteral(".jpg"));
  QString root = qEnvironmentVariable("CUTPRO_CACHE_DIR");
  if (root.isEmpty())
    root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir(root).filePath(QStringLiteral("effect-previews/") + fileName);
}

QString EffectPreviewGenerator::cachedPreview(const QString &mediaPath,
                                              qint64 sourcePositionMs,
                                              qint64 sourceDurationMs,
                                              const QString &effectId,
                                              bool animated) const {
  const QString path = cachePath(mediaPath, sourcePositionMs, sourceDurationMs,
                                 effectId, animated);
  return QFileInfo::exists(path) && QFileInfo(path).size() > 0
             ? QUrl::fromLocalFile(path).toString()
             : QString();
}

QString EffectPreviewGenerator::filterFor(const QString &effectId) {
  const QVariantMap definition = EffectRegistry::definition(effectId);
  QVariantMap instance = EffectStack::create(definition, QStringLiteral("preview"));
  if (instance.isEmpty())
    return {};

  // Several production defaults are intentionally neutral. Browser previews
  // use a visible representative value while applied effects keep their real
  // defaults and user-controlled parameters.
  QVariantMap p = instance.value("parameters").toMap();
  if (effectId == "brightness_contrast") {
    p["brightness"] = 10;
    p["contrast"] = 18;
    p["saturation"] = 115;
  } else if (effectId == "lens_correction") {
    p["k1"] = 28;
  }
  instance["parameters"] = p;
  return VideoEffectPipeline::filters({instance});
}

void EffectPreviewGenerator::request(const QString &clipId,
                                     const QString &mediaPath,
                                     const QString &mediaKind,
                                     qint64 sourcePositionMs,
                                     qint64 sourceDurationMs,
                                     const QString &effectId, bool animated) {
  if (mediaPath.isEmpty() || mediaKind == QStringLiteral("audio") ||
      effectId.isEmpty())
    return;
  const QString key = cachePath(mediaPath, sourcePositionMs, sourceDurationMs,
                                effectId, animated);
  if (QFileInfo::exists(key) && QFileInfo(key).size() > 0) {
    emit previewReady(clipId, effectId, animated,
                      QUrl::fromLocalFile(key).toString());
    return;
  }
  if (m_pending.contains(key))
    return;

  Job job{clipId, mediaPath, mediaKind, sourcePositionMs, sourceDurationMs,
          effectId, animated, key, key};
  m_pending.insert(key);
  // Hover previews are latency-sensitive. Put them ahead of queued stills.
  if (animated)
    m_queue.prepend(job);
  else
    m_queue.enqueue(job);
  startNext();
}

void EffectPreviewGenerator::startNext() {
  if (m_running || m_queue.isEmpty())
    return;
  m_current = m_queue.dequeue();
  m_running = true;
  QDir().mkpath(QFileInfo(m_current.outputPath).absolutePath());

  QStringList args{"-y", "-hide_banner", "-loglevel", "error"};
  const qint64 position =
      qBound<qint64>(0, m_current.sourcePositionMs,
                     qMax<qint64>(0, m_current.sourceDurationMs - 1800));
  if (m_current.mediaKind == QStringLiteral("image")) {
    args << "-loop" << "1" << "-i" << m_current.mediaPath;
  } else {
    args << "-ss" << seconds(position) << "-i" << m_current.mediaPath;
  }

  const QString filter = filterFor(m_current.effectId);
  const QString videoFilter = filter.isEmpty()
                                  ? QStringLiteral("scale=240:-2:flags=lanczos")
                                  : filter + QStringLiteral(",scale=240:-2:flags=lanczos");
  if (m_current.animated) {
    args << "-t" << "1.8" << "-vf" << (videoFilter + ",fps=8") << "-an"
         << "-f" << "gif" << "-loop" << "0" << m_current.outputPath;
  } else {
    args << "-frames:v" << "1" << "-vf" << videoFilter << "-q:v" << "3"
         << m_current.outputPath;
  }
  m_process.start(ffmpegExecutable(), args);
}
