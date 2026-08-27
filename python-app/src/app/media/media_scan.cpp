#include "app/media/media_scan.h"

#include "app/media/media_path.h"

#include <QDir>
#include <QElapsedTimer>
#include <QQueue>
#include <QSet>
#include <QUrl>

namespace {

// Suffix tables kept in one place. Backend::kindFor used to hold its own copy;
// the two drifting apart meant a file could be listed by the scan and then
// rejected by the probe, which showed up as an import that silently dropped
// files.
const QSet<QString> &videoSuffixes() {
  static const QSet<QString> suffixes{
      QStringLiteral("mp4"),  QStringLiteral("mov"),  QStringLiteral("mkv"),
      QStringLiteral("avi"),  QStringLiteral("webm"), QStringLiteral("m4v"),
      QStringLiteral("mts"),  QStringLiteral("m2ts")};
  return suffixes;
}

const QSet<QString> &audioSuffixes() {
  static const QSet<QString> suffixes{
      QStringLiteral("mp3"),  QStringLiteral("wav"), QStringLiteral("aac"),
      QStringLiteral("m4a"),  QStringLiteral("flac"), QStringLiteral("ogg"),
      QStringLiteral("opus"), QStringLiteral("wma")};
  return suffixes;
}

const QSet<QString> &imageSuffixes() {
  static const QSet<QString> suffixes{
      QStringLiteral("png"), QStringLiteral("jpg"),  QStringLiteral("jpeg"),
      QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif"),
      QStringLiteral("tif"), QStringLiteral("tiff")};
  return suffixes;
}

// A directory identity that survives symlinks and junctions. canonicalFilePath
// resolves them; when it cannot (a dangling link, or a path the OS refuses to
// resolve) the cleaned path is a safe fallback because it is at least stable.
QString directoryKey(const QFileInfo &info) {
  const QString canonical = info.canonicalFilePath();
  const QString key =
      canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
#if defined(Q_OS_WIN)
  return key.toCaseFolded();
#else
  return key;
#endif
}

struct PendingDirectory {
  QString path;
  int depth = 0;
};

} // namespace

QString MediaScan::kindForSuffix(const QString &suffix) {
  const QString lower = suffix.toLower();
  if (videoSuffixes().contains(lower))
    return QStringLiteral("video");
  if (audioSuffixes().contains(lower))
    return QStringLiteral("audio");
  if (imageSuffixes().contains(lower))
    return QStringLiteral("image");
  return QStringLiteral("unknown");
}

QString MediaScan::kindForFile(const QFileInfo &info) {
  return kindForSuffix(info.suffix());
}

bool MediaScan::isSupported(const QFileInfo &info) {
  return kindForFile(info) != QStringLiteral("unknown");
}

QString MediaScan::normalizeInput(const QString &value) {
  const QString trimmed = value.trimmed();
  if (trimmed.isEmpty())
    return {};
  const QUrl url(trimmed);
  if (url.isLocalFile())
    return QDir::cleanPath(url.toLocalFile());
  return QDir::cleanPath(trimmed);
}

QString MediaScan::Result::truncationMessage() const {
  QStringList reasons;
  if (hitFileLimit)
    reasons << QStringLiteral("the file limit was reached");
  if (hitEntryLimit)
    reasons << QStringLiteral("too many folder entries were scanned");
  if (hitDepthLimit)
    reasons << QStringLiteral("some folders were nested too deeply");
  if (hitTimeBudget)
    reasons << QStringLiteral("the folder scan ran out of time");
  if (reasons.isEmpty())
    return {};
  return QStringLiteral("Imported the first %1 media files: %2. Pick a more "
                        "specific folder to import the rest.")
      .arg(files.size())
      .arg(reasons.join(QStringLiteral(", ")));
}

MediaScan::Result MediaScan::expand(const QStringList &paths,
                                    const Limits &limits,
                                    const std::atomic_bool *cancel) {  Result result;
  const int maximumFiles = qMax(1, limits.maximumFiles);
  const int maximumEntries = qMax(1, limits.maximumEntries);
  const int maximumDepth = qMax(0, limits.maximumDepth);

  QElapsedTimer timer;
  timer.start();
  const auto outOfTime = [&]() {
    return limits.timeBudgetMs > 0 && timer.elapsed() > limits.timeBudgetMs;
  };
  const auto cancelled = [&]() {
    return cancel && cancel->load(std::memory_order_acquire);
  };

  // Duplicate suppression is O(1) per file. The old code called
  // QStringList::removeDuplicates() at the end, which is O(n log n) on top of
  // an already-materialised list and did not collapse two different spellings
  // of the same file.
  QSet<QString> seenFiles;
  QSet<QString> seenDirectories;
  QQueue<PendingDirectory> queue;

  const auto acceptFile = [&](const QFileInfo &info) {
    if (!isSupported(info)) {
      ++result.skippedUnsupported;
      return;
    }
    // A zero-byte or unreadable file would be probed, fail, and be dropped
    // later anyway; rejecting it here keeps the progress total honest.
    if (!info.isReadable() || info.size() <= 0) {
      ++result.skippedUnreadable;
      return;
    }
    const QString key = MediaPath::duplicateKey(info.absoluteFilePath());
    if (key.isEmpty()) {
      ++result.skippedUnreadable;
      return;
    }
    if (seenFiles.contains(key)) {
      ++result.skippedDuplicates;
      return;
    }
    seenFiles.insert(key);
    result.files << QDir::cleanPath(info.absoluteFilePath());
  };

  for (const QString &raw : paths) {
    if (cancelled()) {
      result.cancelled = true;
      return result;
    }
    const QString normalized = normalizeInput(raw);
    if (normalized.isEmpty())
      continue;
    const QFileInfo info(normalized);
    ++result.entriesVisited;
    if (info.isDir()) {
      // An explicitly selected folder is always visited, even if a symlink
      // elsewhere in the selection resolves to the same place.
      const QString key = directoryKey(info);
      if (!seenDirectories.contains(key)) {
        seenDirectories.insert(key);
        queue.enqueue({info.absoluteFilePath(), 0});
      }
    } else if (info.isFile()) {
      acceptFile(info);
      if (result.files.size() >= maximumFiles) {
        result.hitFileLimit = true;
        return result;
      }
    }
  }

  while (!queue.isEmpty()) {
    if (cancelled()) {
      result.cancelled = true;
      return result;
    }
    if (outOfTime()) {
      result.hitTimeBudget = true;
      return result;
    }

    const PendingDirectory current = queue.dequeue();
    QDir directory(current.path);
    if (!directory.isReadable()) {
      if (result.unreadableDirectories.size() < kMaxReportedUnreadableDirectories)
        result.unreadableDirectories << current.path;
      continue;
    }
    ++result.directoriesVisited;

    // Locale-aware name order per directory: users expect EP01, EP02, EP10 in
    // that order, and a truncated scan should truncate at the end of the list
    // rather than somewhere arbitrary.
    const QFileInfoList entries = directory.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
        QDir::Name | QDir::IgnoreCase | QDir::LocaleAware | QDir::DirsLast);

    for (const QFileInfo &entry : entries) {
      if (cancelled()) {
        result.cancelled = true;
        return result;
      }
      if (++result.entriesVisited > maximumEntries) {
        result.hitEntryLimit = true;
        return result;
      }
      // Checked inside the entry loop as well: a single directory can hold
      // hundreds of thousands of files.
      if (outOfTime()) {
        result.hitTimeBudget = true;
        return result;
      }

      if (entry.isDir()) {
        if (current.depth + 1 > maximumDepth) {
          result.hitDepthLimit = true;
          continue;
        }
        const QString key = directoryKey(entry);
        if (key.isEmpty() || seenDirectories.contains(key))
          continue;
        seenDirectories.insert(key);
        queue.enqueue({entry.absoluteFilePath(), current.depth + 1});
        continue;
      }

      acceptFile(entry);
      if (result.files.size() >= maximumFiles) {
        result.hitFileLimit = true;
        return result;
      }
    }
  }

  return result;
}
