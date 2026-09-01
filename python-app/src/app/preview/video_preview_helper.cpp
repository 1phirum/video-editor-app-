#include "app/preview/video_preview_helper.h"

#include "app/preview/gui_thread_watchdog.h"

#include <algorithm>
#include <limits>

namespace {
constexpr qint64 kNoBoundary = std::numeric_limits<qint64>::max();
} // namespace

VideoPreviewHelper::VideoPreviewHelper(QObject *parent) : QObject(parent) {}

void VideoPreviewHelper::setTimeline(const QVariantList &clips,
                                     const QVariantList &media) {
  CUTPRO_GUI_SCOPE("VideoPreviewHelper::setTimeline");
  m_clips = clips;
  m_media = media;
  rebuildIndexes();
  resolve(true);
}

void VideoPreviewHelper::setTrackStates(const QVariantList &trackStates) {
  CUTPRO_GUI_SCOPE("VideoPreviewHelper::setTrackStates");
  m_trackStates = trackStates;
  rebuildTrackEnabled();
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

QVariantMap VideoPreviewHelper::clipAt(int index) const {
  if (index < 0 || index >= m_clips.size())
    return {};
  return m_clips.at(index).toMap();
}

void VideoPreviewHelper::rebuildIndexes() {
  CUTPRO_GUI_SCOPE("VideoPreviewHelper::rebuildIndexes");
  m_mediaById.clear();
  m_mediaById.reserve(m_media.size());
  for (const QVariant &value : m_media) {
    const QVariantMap item = value.toMap();
    m_mediaById.insert(item.value("id").toString(), item);
  }

  m_clipIndexById.clear();
  m_clipIndexById.reserve(m_clips.size());
  m_audioIndexByLinkGroup.clear();
  m_audioIndexByExtractedFrom.clear();
  m_tracks.clear();
  QHash<QString, int> slotByTrack;

  // Keys hoisted out of the loop. Each one is otherwise re-created (and
  // re-hashed against the map's QString comparisons) 19,831 times over.
  static const QString kId = QStringLiteral("id");
  static const QString kEnabled = QStringLiteral("enabled");
  static const QString kTrack = QStringLiteral("track");
  static const QString kStartMs = QStringLiteral("startMs");
  static const QString kDurationMs = QStringLiteral("durationMs");
  static const QString kKind = QStringLiteral("kind");
  static const QString kLinkGroupId = QStringLiteral("linkGroupId");
  static const QString kLinkedRole = QStringLiteral("linkedRole");
  static const QString kExtractedFrom = QStringLiteral("extractedFromClipId");
  static const QString kAudio = QStringLiteral("audio");
  static const QString kSubtitle = QStringLiteral("subtitle");
  static const QString kEffect = QStringLiteral("effect");
  static const QString kOverlay = QStringLiteral("overlay");
  static const QString kImage = QStringLiteral("image");

  for (int i = 0; i < m_clips.size(); ++i) {
    const QVariantMap clip = m_clips.at(i).toMap();
    const QString id = clip.value(kId).toString();
    if (!id.isEmpty())
      m_clipIndexById.insert(id, i);

    const QString linkGroup = clip.value(kLinkGroupId).toString();
    if (!linkGroup.isEmpty() &&
        clip.value(kLinkedRole).toString() == kAudio &&
        !m_audioIndexByLinkGroup.contains(linkGroup))
      m_audioIndexByLinkGroup.insert(linkGroup, i);

    const QString extractedFrom = clip.value(kExtractedFrom).toString();
    if (!extractedFrom.isEmpty() &&
        !m_audioIndexByExtractedFrom.contains(extractedFrom))
      m_audioIndexByExtractedFrom.insert(extractedFrom, i);

    // Disabled clips are left out of the index entirely: the old scan skipped
    // them before it even looked at their edges, so they contribute neither a
    // picture nor a boundary. Toggling one re-emits clipsChanged, which lands
    // back here.
    if (clip.value(kEnabled, true).toBool() == false)
      continue;

    const QString track = clip.value(kTrack).toString();
    Span span;
    span.startMs = clip.value(kStartMs).toLongLong();
    span.endMs = span.startMs + clip.value(kDurationMs).toLongLong();
    span.clipIndex = i;

    const QString kind = clip.value(kKind).toString();
    if (kind == kSubtitle)
      span.kind = Kind::Subtitle;
    else if (kind == kEffect)
      span.kind = Kind::Effect;
    else if (kind == kAudio)
      span.kind = Kind::Audio;
    else if (clip.value(kOverlay).toBool())
      // A logo / graphic / shape. Composited over the base picture by the QML
      // overlay layer, so it must not win base-picture selection in resolve().
      span.kind = Kind::Overlay;
    else
      span.kind = Kind::Picture;

    const bool video = track.startsWith(QLatin1Char('V'), Qt::CaseInsensitive);
    bool ok = false;
    const int trackNumber = track.mid(1).toInt(&ok);
    const int lane = ok ? trackNumber : 0;
    // A real video outranks an image on any track, so a picture-track image no
    // longer replaces the video beneath it: the base stays the decoded video and
    // the image is drawn as an overlay on top (matching how the export stacks
    // tracks, and how CapCut/Premiere show a higher image over the clip below).
    // An image still outranks nothing, so an image with no video under it is
    // still chosen as the base picture and shown full-frame.
    if (!video)
      span.rank = 0;
    else if (kind == kImage)
      span.rank = 1000 + lane;
    else
      span.rank = 2000 + lane;

    const auto slot = slotByTrack.constFind(track);
    int index = -1;
    if (slot != slotByTrack.constEnd()) {
      index = *slot;
    } else {
      index = m_tracks.size();
      TrackIndex fresh;
      fresh.id = track;
      m_tracks.append(fresh);
      slotByTrack.insert(track, index);
    }
    m_tracks[index].spans.append(span);
  }

  for (TrackIndex &track : m_tracks) {
    // A caption track arrives in time order, so this sort is almost always a
    // single is_sorted pass. It still has to be here: a hand-cut track can be
    // in any order once clips have been dragged around.
    if (!std::is_sorted(track.spans.begin(), track.spans.end(),
                        [](const Span &a, const Span &b) {
                          return a.startMs < b.startMs;
                        }))
      std::stable_sort(track.spans.begin(), track.spans.end(),
                       [](const Span &a, const Span &b) {
                         return a.startMs < b.startMs;
                       });

    const int count = track.spans.size();
    track.starts.resize(count);
    track.maxEnd.resize(count);
    track.sortedEnds.resize(count);
    qint64 running = std::numeric_limits<qint64>::min();
    for (int i = 0; i < count; ++i) {
      const Span &span = track.spans.at(i);
      track.starts[i] = span.startMs;
      running = qMax(running, span.endMs);
      track.maxEnd[i] = running;
      track.sortedEnds[i] = span.endMs;
    }
    std::sort(track.sortedEnds.begin(), track.sortedEnds.end());
  }

  rebuildTrackEnabled();
}

void VideoPreviewHelper::rebuildTrackEnabled() {
  // The old trackEnabled() re-derived all of this per clip, allocating a
  // toUpper() copy of both ids on every comparison. There are a handful of
  // tracks, so it is resolved once here and read as a flag afterwards.
  QHash<QString, QVariantMap> stateByUpperId;
  QHash<QChar, bool> soloByPrefix;
  stateByUpperId.reserve(m_trackStates.size());
  for (const QVariant &value : m_trackStates) {
    const QVariantMap candidate = value.toMap();
    const QString id = candidate.value(QStringLiteral("id")).toString().toUpper();
    if (id.isEmpty())
      continue;
    stateByUpperId.insert(id, candidate);
    if (candidate.value(QStringLiteral("solo")).toBool())
      soloByPrefix.insert(id.at(0), true);
  }

  for (TrackIndex &track : m_tracks) {
    const QChar prefix =
        track.id.isEmpty() ? QChar() : track.id.at(0).toUpper();
    const bool prefixHasSolo = soloByPrefix.value(prefix, false);
    const QVariantMap state = stateByUpperId.value(track.id.toUpper());
    if (!state.isEmpty() &&
        state.value(QStringLiteral("visible"), true).toBool() == false) {
      track.enabled = false;
      continue;
    }
    track.enabled = !prefixHasSolo ||
                    (!state.isEmpty() &&
                     state.value(QStringLiteral("solo")).toBool());
  }
}

QVariantMap VideoPreviewHelper::mediaForClip(const QVariantMap &clip) const {
  return m_mediaById.value(clip.value("mediaId").toString());
}

// Where this clip's sound actually is. Normally the clip itself; after Extract
// Audio the video clip has handed its sound to an independent A-track clip, and
// that clip is what carries the level, the mute and any vocal replacement. The
// answer is used for routing only - the two clips are not linked, so moving or
// deleting the audio simply stops the video from resolving to anything.
QVariantMap VideoPreviewHelper::audioForClip(const QVariantMap &clip) const {
  if (clip.isEmpty() || clip.value("kind").toString() == QStringLiteral("audio"))
    return clip;
  if (clip.value("separateAudio").toBool()) {
    const auto found =
        m_audioIndexByExtractedFrom.constFind(clip.value("id").toString());
    if (found != m_audioIndexByExtractedFrom.constEnd()) {
      const QVariantMap extracted = clipAt(*found);
      if (!extracted.isEmpty())
        return extracted;
    }
  }
  // Pairs saved before extracted audio became independent.
  const QString group = clip.value("linkGroupId").toString();
  if (group.isEmpty())
    return clip;
  const auto found = m_audioIndexByLinkGroup.constFind(group);
  if (found == m_audioIndexByLinkGroup.constEnd())
    return clip;
  const QVariantMap linked = clipAt(*found);
  return linked.isEmpty() ? clip : linked;
}

void VideoPreviewHelper::resolve(bool force) {
  CUTPRO_GUI_SCOPE("VideoPreviewHelper::resolve");
  QVariantMap nextClip;
  qint64 nextBoundary = kNoBoundary;

  // Winners are picked by clip index so the outcome matches the old
  // front-to-back scan exactly, even though the search now walks each track
  // backwards from the playhead.
  int pictureIndex = -1;
  int pictureRank = -1;
  int subtitleIndex = -1;
  QVector<int> audioIndexes;

  const bool custom = !m_customPreviewClipId.isEmpty();
  if (custom) {
    const QVariantMap candidate =
        clipAt(m_clipIndexById.value(m_customPreviewClipId, -1));
    const QString kind = candidate.value("kind").toString();
    if (!candidate.isEmpty() && kind != QStringLiteral("audio") &&
        kind != QStringLiteral("subtitle"))
      nextClip = candidate;
  }

  for (const TrackIndex &track : m_tracks) {
    if (!track.enabled)
      continue;

    // The next edge on this track, in both directions the old scan looked at:
    // the first start after the playhead and the first end after it.
    const auto startAfter = std::upper_bound(track.starts.cbegin(),
                                             track.starts.cend(), m_positionMs);
    if (startAfter != track.starts.cend())
      nextBoundary = qMin(nextBoundary, *startAfter);
    const auto endAfter = std::upper_bound(track.sortedEnds.cbegin(),
                                           track.sortedEnds.cend(), m_positionMs);
    if (endAfter != track.sortedEnds.cend())
      nextBoundary = qMin(nextBoundary, *endAfter);

    // Every span that can still be open at the playhead starts at or before it,
    // so walk back from there and stop once no earlier span reaches this far.
    for (int i = int(startAfter - track.starts.cbegin()) - 1;
         i >= 0 && track.maxEnd.at(i) > m_positionMs; --i) {
      const Span &span = track.spans.at(i);
      if (m_positionMs < span.startMs || m_positionMs >= span.endMs)
        continue;

      if (span.kind == Kind::Subtitle) {
        if (subtitleIndex < 0 || span.clipIndex < subtitleIndex)
          subtitleIndex = span.clipIndex;
        continue;
      }
      // An effect bar carries no picture of its own. Its edges still count
      // toward nextBoundary above, so the preview refreshes when the effect
      // starts or ends, but it can never be the clip that gets decoded.
      if (span.kind == Kind::Effect)
        continue;
      // An overlay (logo/graphic/shape) never becomes the decoded base picture;
      // its edges still moved nextBoundary above, so the monitor refreshes when
      // it enters or leaves and the QML overlay layer draws it on top.
      if (span.kind == Kind::Overlay)
        continue;
      if (span.kind == Kind::Audio)
        audioIndexes.append(span.clipIndex);
      if (custom)
        continue;
      if (span.rank > pictureRank ||
          (span.rank == pictureRank && span.clipIndex < pictureIndex)) {
        pictureRank = span.rank;
        pictureIndex = span.clipIndex;
      }
    }
  }

  if (!custom && pictureIndex >= 0)
    nextClip = clipAt(pictureIndex);

  QVariantList nextAudioClips;
  if (!audioIndexes.isEmpty()) {
    std::sort(audioIndexes.begin(), audioIndexes.end());
    nextAudioClips.reserve(audioIndexes.size());
    for (const int index : audioIndexes)
      nextAudioClips.append(clipAt(index));
  }
  const QVariantMap nextSubtitle =
      subtitleIndex >= 0 ? clipAt(subtitleIndex) : QVariantMap{};

  if (nextBoundary == kNoBoundary)
    nextBoundary = kNoBoundary - 1;
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
