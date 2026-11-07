#include "app/media/ffmpeg_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
QString bundledRoot() {
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString sibling = QDir(appDir).filePath(QStringLiteral("ffmpeg"));
  if (QFileInfo::exists(QDir(sibling).filePath(QStringLiteral("bin/ffmpeg.exe"))))
    return sibling;

  const QString parentBuild = QDir(appDir).filePath(QStringLiteral("../ffmpeg-9.0.1-full_build-shared"));
  if (QFileInfo::exists(QDir(parentBuild).filePath(QStringLiteral("bin/ffmpeg.exe"))))
    return QDir::cleanPath(parentBuild);

  const QString configured = qEnvironmentVariable("CUTPRO_FFMPEG_ROOT").trimmed();
  if (!configured.isEmpty() && QFileInfo::exists(
          QDir(configured).filePath(QStringLiteral("bin/ffmpeg.exe"))))
    return QDir::cleanPath(configured);
  return {};
}

QString resolve(const QString &name) {
  const QString root = bundledRoot();
  if (!root.isEmpty()) {
    const QString bundled = QDir(root).filePath(QStringLiteral("bin/") + name);
    if (QFileInfo::exists(bundled))
      return bundled;
  }
  const QString found = QStandardPaths::findExecutable(name);
  return found.isEmpty() ? name : found;
}
} // namespace

QString FfmpegRuntime::root() { return bundledRoot(); }
QString FfmpegRuntime::executable() { return resolve(QStringLiteral("ffmpeg.exe")); }
QString FfmpegRuntime::probeExecutable() { return resolve(QStringLiteral("ffprobe.exe")); }
