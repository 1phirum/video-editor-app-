#include "app/core_app/backend.h"
#include "app/project/project_media_actions.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QSet>
#include <QUrl>

bool Backend::addMediaSelectionToTimeline(const QStringList &mediaIds) {
  const QStringList ids =
      ProjectMediaActions::normalizeSelection(m_media, mediaIds);
  if (ids.isEmpty()) {
    setError(QStringLiteral("Select at least one media item first."));
    return false;
  }

  const qint64 start = qMax(
      qMax(ProjectMediaActions::appendPosition(m_clips), durationMs()),
      m_playheadMs);
  return beginTimelinePlacement(ids, start, QString());
}

bool Backend::renameMedia(const QString &mediaId, const QString &name) {
  const int index = mediaIndex(mediaId);
  const QString trimmedName = name.trimmed();
  if (index < 0 || trimmedName.isEmpty()) {
    setError(QStringLiteral("Enter a name for the media item."));
    return false;
  }

  QVariantMap media = m_media.at(index).toMap();
  if (media.value(QStringLiteral("name")).toString() == trimmedName)
    return true;

  rememberState();
  media[QStringLiteral("name")] = trimmedName;
  m_media[index] = media;
  markDirty();
  emit mediaChanged();
  emit timelineChanged();
  return true;
}

bool Backend::openMediaExternally(const QString &mediaId) {
  const QString path = mediaById(mediaId).value(QStringLiteral("path")).toString();
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    setError(QStringLiteral("The source media file could not be found."));
    return false;
  }
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
    setError(QStringLiteral("Could not open the source media file."));
    return false;
  }
  return true;
}

bool Backend::revealMediaInFileManager(const QString &mediaId) {
  const QString path = mediaById(mediaId).value(QStringLiteral("path")).toString();
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    setError(QStringLiteral("The source media file could not be found."));
    return false;
  }

#if defined(Q_OS_WIN)
  const bool opened = QProcess::startDetached(
      QStringLiteral("explorer.exe"),
      {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#elif defined(Q_OS_MACOS)
  const bool opened =
      QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#else
  const bool opened = QDesktopServices::openUrl(
      QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
  if (!opened)
    setError(QStringLiteral("Could not reveal the source media file."));
  return opened;
}

bool Backend::copyMediaPath(const QString &mediaId) {
  const QString path = mediaById(mediaId).value(QStringLiteral("path")).toString();
  if (path.isEmpty()) {
    setError(QStringLiteral("This media item has no source file path."));
    return false;
  }
  QClipboard *clipboard = QGuiApplication::clipboard();
  if (!clipboard)
    return false;
  clipboard->setText(QDir::toNativeSeparators(path));
  return true;
}

bool Backend::removeMediaSelection(const QStringList &mediaIds) {
  const QStringList ids =
      ProjectMediaActions::normalizeSelection(m_media, mediaIds);
  if (ids.isEmpty())
    return false;

  QSet<QString> selected;
  for (const QString &id : ids)
    selected.insert(id);
  rememberState();
  for (int index = m_media.size() - 1; index >= 0; --index) {
    const QString mediaId =
        m_media.at(index).toMap().value(QStringLiteral("id")).toString();
    if (!selected.contains(mediaId))
      continue;
    m_media.removeAt(index);
    m_sourceTranscripts.remove(mediaId);
    m_sourceTranscriptLanguages.remove(mediaId);
    m_transcriptCoverageMs.remove(mediaId);
  }
  for (int index = m_clips.size() - 1; index >= 0; --index) {
    if (selected.contains(m_clips.at(index).toMap()
                              .value(QStringLiteral("mediaId"))
                              .toString()))
      m_clips.removeAt(index);
  }
  pruneEmptyTracks();
  markDirty();
  emit mediaChanged();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}
