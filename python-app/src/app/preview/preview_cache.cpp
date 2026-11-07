#include "app/preview/preview_cache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace {

constexpr auto kSubdirectory = "timeline-previews";

QString cacheKey(const QString &sourcePath, const QString &variant) {
  const QFileInfo source(sourcePath);
  // canonicalFilePath() collapses symlinks and case differences on Windows so
  // the same file reached two ways shares one cache entry. It is empty for a
  // file that has just been removed, where the cleaned path is the better key.
  const QString canonical = source.canonicalFilePath();
  const QByteArray key =
      (canonical.isEmpty() ? QDir::cleanPath(source.absoluteFilePath())
                           : canonical)
          .toUtf8() +
      '|' + variant.toUtf8() + '|' +
      QByteArray::number(source.lastModified().toMSecsSinceEpoch()) + '|' +
      QByteArray::number(source.size());
  return QString::fromLatin1(
             QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex())
      .left(24);
}

} // namespace

QString PreviewCache::root() {
  QString cacheRoot = qEnvironmentVariable("CUTPRO_CACHE_DIR");
  if (cacheRoot.isEmpty())
    cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir(cacheRoot).filePath(QString::fromLatin1(kSubdirectory));
}

QString PreviewCache::reserve(const QString &sourcePath,
                              const QString &variant,
                              const QString &extension) {
  if (sourcePath.isEmpty())
    return {};
  const QString directory = root();
  if (!QDir().mkpath(directory))
    return {};
  return QDir(directory).filePath(cacheKey(sourcePath, variant) + '.' +
                                  extension);
}

QString PreviewCache::lookup(const QString &sourcePath, const QString &variant,
                             const QString &extension) {
  const QString candidate = QDir(root()).filePath(
      cacheKey(sourcePath, variant) + '.' + extension);
  const QFileInfo info(candidate);
  return info.exists() && info.size() > 0 ? candidate : QString();
}

QString PreviewCache::lookupUrl(const QString &sourcePath,
                                const QString &variant,
                                const QString &extension) {
  return toUrl(lookup(sourcePath, variant, extension));
}

QString PreviewCache::toUrl(const QString &absolutePath) {
  if (absolutePath.isEmpty())
    return {};
  return QUrl::fromLocalFile(absolutePath).toString();
}

qint64 PreviewCache::sweep(qint64 maximumBytes) {
  QDir directory(root());
  if (!directory.exists())
    return 0;
  // Oldest first, so the loop below deletes the least recently produced entries
  // and stops as soon as the cache is back inside its budget.
  QFileInfoList entries =
      directory.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
  qint64 total = 0;
  for (const QFileInfo &entry : entries)
    total += entry.size();
  if (total <= maximumBytes)
    return total;

  for (const QFileInfo &entry : entries) {
    if (total <= maximumBytes)
      break;
    const qint64 size = entry.size();
    if (QFile::remove(entry.absoluteFilePath()))
      total -= size;
  }
  return total;
}
