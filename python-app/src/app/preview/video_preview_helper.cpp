#include "app/preview/video_preview_helper.h"

#include <limits>

VideoPreviewHelper::VideoPreviewHelper(QObject *parent) : QObject(parent) {}

void VideoPreviewHelper::setTimeline(const QVariantList &clips,
                                     const QVariantList &media) {
  m_clips = clips;
  m_media = media;
  rebuildIndexes();
  resolve(true);
}

void VideoPreviewHelper::setTrackStates(const QVariantList &trackStates) {
  m_trackStates = trackStates;
  resolve(true);
}

void VideoPreviewHelper::setCustomPreviewClipId(const QString &clipId) {
  if (m_customPreviewClipId == clipId)
    return;
  m_customPreviewClipId = clipId;
  resolve(true);
}

void VideoPreviewHelper::setPosition(qint64 positionMs) {
  positionMs = qMax<qint64>(0, positionMs);
  const bool movedBackward = positionMs < m_lastResolvedPositionMs;
  m_positionMs = positionMs;
  if (movedBackward || m_lastResolvedPositionMs < 0 ||
      positionMs >= m_nextBoundaryMs)
    resolve();
}

void VideoPreviewHelper::rebuildIndexes() {
  m_mediaById.clear();
  for (const QVariant &value : m_media) {
    const QVariantMap item = value.toMap();
    m_mediaById.insert(item.value("id").toString(), item);
  }
  m_clipById.clear();
  for (const QVariant &value : m_clips) {
    const QVariantMap clip = value.toMap();
    m_clipById.insert(clip.value("id").toString(), clip);
  }
}

bool VideoPreviewHelper::trackEnabled(const QString &track) const {
  QVariantMap state;
  bool prefixHasSolo = false;
  const QChar prefix = track.isEmpty() ? QChar() : track.at(0).toUpper();
  for (const QVariant &value : m_trackStates) {
    const QVariantMap candidate = value.toMap();
    const QString id = candidate.value("id").toString().toUpper();
    if (!id.isEmpty() && id.at(0) == prefix &&
        candidate.value("solo").toBool())
      prefixHasSolo = true;
    if (id == track.toUpper())
      state = candidate;
  }
  if (!state.isEmpty() && state.value("visible", true).toBool() == false)
    return false;
  return !prefixHasSolo || (!state.isEmpty() && state.value("solo").toBool());
}

QVariantMap VideoPreviewHelper::mediaForClip(const QVariantMap &clip) const {
  return m_mediaById.value(clip.value("mediaId").toString());
}

QVariantMap VideoPreviewHelper::audioForClip(const QVariantMap &clip) const {
  if (clip.isEmpty() || clip.value("kind").toString() == QStringLiteral("audio") ||
      clip.value("linkGroupId").toString().isEmpty())
    return clip;
  const QString group = clip.value("linkGroupId").toString();
  for (const QVariant &value : m_clips) {
    const QVariantMap candidate = value.toMap();
    if (candidate.value("linkGroupId").toString() == group &&
        candidate.value("linkedRole").toString() == QStringLiteral("audio"))
      return candidate;
  }
  return clip;
}

void VideoPreviewHelper::resolve(bool force) {
  QVariantMap nextClip;
  QVariantMap nextSubtitle;
  QVariantList nextAudioClips;
  int selectedRank = -1;
  qint64 nextBoundary = std::numeric_limits<qint64>::max();

  if (!m_customPreviewClipId.isEmpty()) {
    const QVariantMap custom = m_clipById.value(m_customPreviewClipId);
    const QString kind = custom.value("kind").toString();
    if (!custom.isEmpty() && kind != QStringLiteral("audio") &&
        kind != QStringLiteral("subtitle"))
      nextClip = custom;
  }

  for (const QVariant &value : m_clips) {
    const QVariantMap clip = value.toMap();
    if (clip.value("enabled", true).toBool() == false)
      continue;
    const QString track = clip.value("track").toString();
    if (!trackEnabled(track))
      continue;
    const qint64 start = clip.value("startMs").toLongLong();
    const qint64 end = start + clip.value("durationMs").toLongLong();
    if (start > m_positionMs)
      nextBoundary = qMin(nextBoundary, start);
    if (end > m_positionMs)
      nextBoundary = qMin(nextBoundary, end);
    if (m_positionMs < start || m_positionMs >= end)
      continue;

    const QString kind = clip.value("kind").toString();
    if (kind == QStringLiteral("subtitle")) {
      if (nextSubtitle.isEmpty())
        nextSubtitle = clip;
      continue;
    }
    // An effect bar carries no picture of its own. Its edges still count toward
    // nextBoundary above, so the preview refreshes when the effect starts or
    // ends, but it can never be the clip that gets decoded.
    if (kind == QStringLiteral("effect"))
      continue;
    if (kind == QStringLiteral("audio"))
      nextAudioClips.append(clip);
    if (!m_customPreviewClipId.isEmpty())
      continue;
    const bool video = track.startsWith(QLatin1Char('V'), Qt::CaseInsensitive);
    bool ok = false;
    const int trackNumber = track.mid(1).toInt(&ok);
    const int rank = video ? 1000 + (ok ? trackNumber : 0) : 0;
    if (rank > selectedRank) {
      nextClip = clip;
      selectedRank = rank;
    }
  }

  if (nextBoundary == std::numeric_limits<qint64>::max())
    nextBoundary = std::numeric_limits<qint64>::max() - 1;
  m_nextBoundaryMs = qMax(m_positionMs + 1, nextBoundary);
  m_lastResolvedPositionMs = m_positionMs;

  const QVariantMap nextMedia = mediaForClip(nextClip);
  const QVariantMap nextAudio = audioForClip(nextClip);
  const bool changed = force || nextClip != m_activeClip ||
                       nextMedia != m_activeMedia ||
                       nextAudio != m_activeAudioClip ||
                       nextAudioClips != m_activeAudioClips ||
                       nextSubtitle != m_activeSubtitle;
  if (!changed)
    return;
  m_activeClip = nextClip;
  m_activeMedia = nextMedia;
  m_activeAudioClip = nextAudio;
  m_activeAudioClips = nextAudioClips;
  m_activeSubtitle = nextSubtitle;
  emit activeStateChanged();
}
