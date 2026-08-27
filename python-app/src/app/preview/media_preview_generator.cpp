#include "app/preview/media_preview_generator.h"
#include "app/media/ffmpeg_runtime.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace {
QString ffmpegExecutable() {
  return FfmpegRuntime::executable();
}
} // namespace

QString MediaPreviewGenerator::cachedOutput(const QString &path,
                                            const QString &variant,
                                            const QString &extension) {
  const QFileInfo source(path);
  const QByteArray key =
      source.canonicalFilePath().toUtf8() + variant.toUtf8() +
      QByteArray::number(source.lastModified().toMSecsSinceEpoch()) +
      QByteArray::number(source.size());
  const QString fileName =
      QString::fromLatin1(
          QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex())
          .left(24) +
      "." + extension;
  QString cacheRoot = qEnvironmentVariable("CUTPRO_CACHE_DIR");
  if (cacheRoot.isEmpty())
    cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir(cacheRoot).filePath("timeline-previews/" + fileName);
}

QString MediaPreviewGenerator::run(const QString &path,
                                   const QString &outputPath,
                                   const QStringList &arguments,
                                   int timeoutMs,
                                   bool seekBeforeInput) {
  if (QFileInfo::exists(outputPath) && QFileInfo(outputPath).size() > 0)
    return QUrl::fromLocalFile(outputPath).toString();

  QDir().mkpath(QFileInfo(outputPath).absolutePath());
  QStringList command{"-y", "-hide_banner", "-loglevel", "error"};
  QStringList outputArguments = arguments;
  if (seekBeforeInput) {
    const int seekIndex = outputArguments.indexOf(QStringLiteral("-ss"));
    if (seekIndex >= 0 && seekIndex + 1 < outputArguments.size()) {
      command << QStringLiteral("-ss") << outputArguments.at(seekIndex + 1);
      outputArguments.remove(seekIndex, 2);
    }
  }
  command << QStringLiteral("-i") << path;
  command.append(outputArguments);
  command.append(outputPath);

  QProcess process;
  process.start(ffmpegExecutable(), command);
  if (!process.waitForFinished(timeoutMs) || process.exitCode() != 0 ||
      !QFileInfo::exists(outputPath) || QFileInfo(outputPath).size() == 0) {
    qWarning().noquote() << "FFmpeg preview failed:" << path << arguments
                         << process.errorString()
                         << process.readAllStandardError();
    process.kill();
    QFile::remove(outputPath);
    return {};
  }
  return QUrl::fromLocalFile(outputPath).toString();
}

QString MediaPreviewGenerator::thumbnail(const QString &path,
                                         qint64 durationMs) {
  const QString output = cachedOutput(path, "thumbnail-v2", "jpg");
  const double seekSeconds =
      durationMs > 0 ? std::min(1.0, durationMs / 2000.0) : 0.0;
  return run(path, output,
             {"-ss", QString::number(seekSeconds, 'f', 3), "-frames:v", "1",
              "-vf", "scale=320:-2:force_original_aspect_ratio=decrease",
              "-q:v", "3"},
             10000, true);
}

QString MediaPreviewGenerator::filmstrip(const QString &path,
                                         qint64 durationMs) {
  const double seconds = std::max(0.1, durationMs / 1000.0);
  const double frameRate = kFilmstripFrames / seconds;
  const QString filter =
      QString("fps=%1,scale=%2:%3:force_original_aspect_ratio=increase,"
              "crop=%4:%5,tile=%6x1")
          .arg(frameRate, 0, 'f', 6)
          .arg(kFilmstripFrameWidth)
          .arg(kFilmstripFrameHeight)
          .arg(kFilmstripFrameWidth)
          .arg(kFilmstripFrameHeight)
          .arg(kFilmstripFrames);
  const QString output = cachedOutput(path, "filmstrip-v1", "jpg");
  return run(path, output, {"-frames:v", "1", "-vf", filter, "-q:v", "3"},
             30000);
}

QString MediaPreviewGenerator::waveform(const QString &path) {
  const QString output = cachedOutput(path, "waveform-bottom-v2", "png");
  const QString filter =
      "[0:a]aformat=channel_layouts=mono,showwavespic=s=1600x320:"
      "colors=0x63a4ff,format=rgba,crop=1600:160:0:0[wave]";
  return run(path, output,
             {"-filter_complex", filter, "-map", "[wave]", "-frames:v", "1"},
             30000);
}
