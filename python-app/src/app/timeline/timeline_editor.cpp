#include "app/timeline/timeline_editor.h"

#include <QtGlobal>
#include <algorithm>
#include <functional>

namespace {
qint64 clipStart(const QVariantMap &clip) {
  return clip.value("startMs").toLongLong();
}
qint64 clipEnd(const QVariantMap &clip) {
  return clipStart(clip) + clip.value("durationMs").toLongLong();
}
bool excluded(const QSet<QString> &ids, const QVariantMap &clip) {
  return ids.contains(clip.value("id").toString());
}

// Every time a moving edge may lock onto, named. Kept in one place so the drag
// guide and the trim path can never end up snapping to different target sets:
// an editor that snaps a trim to a marker but ignores it on a move is worse
// than one that ignores markers everywhere.
void forEachSnapTarget(
    const QVariantList &clips, const QVariantList &markers,
    const QSet<QString> &excludedIds, qint64 playheadMs,
    qint64 sequenceDurationMs,
    const std::function<void(qint64, const QString &)> &visit) {
  visit(0, QStringLiteral("sequenceStart"));
  visit(sequenceDurationMs, QStringLiteral("sequenceEnd"));
  visit(playheadMs, QStringLiteral("playhead"));
  for (const auto &value : markers)
    visit(value.toMap().value("positionMs").toLongLong(),
          QStringLiteral("marker"));
  for (const auto &value : clips) {
    const auto clip = value.toMap();
    if (excluded(excludedIds, clip))
      continue;
    visit(clipStart(clip), QStringLiteral("clip"));
    visit(clipEnd(clip), QStringLiteral("clip"));
  }
}
} // namespace

TimelineEditor::SnapResult TimelineEditor::snapEdge(
    qint64 requestedMs, const QVariantList &clips, const QVariantList &markers,
    const QStringList &excludedClipIds, qint64 playheadMs, qint64 thresholdMs,
    qint64 sequenceDurationMs) {
  const QSet<QString> excludedIds(excludedClipIds.begin(),
                                  excludedClipIds.end());
  const qint64 limit = qMax<qint64>(0, sequenceDurationMs);
  SnapResult result;
  result.guideMs = qMax<qint64>(0, requestedMs);
  result.edge = QStringLiteral("start");
  qint64 nearest = thresholdMs + 1;
  forEachSnapTarget(clips, markers, excludedIds, playheadMs, limit,
                    [&](qint64 candidate, const QString &kind) {
                      candidate = qBound<qint64>(0, candidate, limit);
                      const qint64 d = qAbs(candidate - requestedMs);
                      // Strictly nearer, so a tie goes to the earlier target in
                      // visit order and the winner cannot flicker between two
                      // equidistant joins while the pointer sits still.
                      if (d > thresholdMs || d >= nearest)
                        return;
                      nearest = d;
                      result.snapped = true;
                      result.deltaMs = candidate - requestedMs;
                      result.guideMs = candidate;
                      result.distanceMs = d;
                      result.target = kind;
                    });
  return result;
}

TimelineEditor::SnapResult TimelineEditor::snapClip(
    qint64 startMs, qint64 clipDurationMs, const QVariantList &clips,
    const QVariantList &markers, const QStringList &excludedClipIds,
    qint64 playheadMs, qint64 thresholdMs, qint64 sequenceDurationMs) {
  const qint64 span = qMax<qint64>(0, clipDurationMs);
  const SnapResult head =
      snapEdge(startMs, clips, markers, excludedClipIds, playheadMs,
               thresholdMs, sequenceDurationMs);
  SnapResult tail =
      snapEdge(startMs + span, clips, markers, excludedClipIds, playheadMs,
               thresholdMs, sequenceDurationMs);
  tail.edge = QStringLiteral("end");
  // A tail snap that would push the head behind zero is not reachable: the
  // start clamp would eat part of the move and leave the guide standing at a
  // join the edge never actually met.
  if (tail.snapped && startMs + tail.deltaMs < 0)
    tail = SnapResult{};
  if (!tail.snapped)
    return head;
  if (!head.snapped)
    return tail;
  // The head wins a tie. Butting a clip against the one on its left is the more
  // common intent, and it is the edge the pointer usually leads with.
  return tail.distanceMs < head.distanceMs ? tail : head;
}

qint64 TimelineEditor::snapTime(qint64 requestedMs, const QVariantList &clips,
                                const QVariantList &markers,
                                const QStringList &excludedClipIds,
                                qint64 playheadMs, qint64 thresholdMs,
                                qint64 durationMs) {
  const SnapResult result = snapEdge(requestedMs, clips, markers,
                                     excludedClipIds, playheadMs, thresholdMs,
                                     durationMs);
  return result.snapped ? result.guideMs : qMax<qint64>(0, requestedMs);
}

bool TimelineEditor::rippleDelete(QVariantList &clips,
                                  const QStringList &clipIds,
                                  const QStringList &syncLockedTracks,
                                  QStringList *removedIds) {
  QSet<QString> ids(clipIds.begin(), clipIds.end());
  if (ids.isEmpty())
    return false;
  struct RemovedInterval {
    QString track;
    qint64 start;
    qint64 end;
  };
  QVector<RemovedInterval> intervals;
  bool changed = false;
  for (int i = clips.size() - 1; i >= 0; --i) {
    const auto clip = clips.at(i).toMap();
    const QString id = clip.value("id").toString();
    if (!ids.contains(id))
      continue;
    intervals.append(
        {clip.value("track").toString(), clipStart(clip), clipEnd(clip)});
    if (removedIds)
      removedIds->append(id);
    clips.removeAt(i);
    changed = true;
  }
  if (!changed)
    return false;
  for (auto &value : clips) {
    auto clip = value.toMap();
    const QString track = clip.value("track").toString();
    const qint64 start = clipStart(clip);
    QVector<QPair<qint64, qint64>> applicable;
    for (const auto &interval : intervals) {
      if ((interval.track == track || syncLockedTracks.contains(track)) &&
          start >= interval.end)
        applicable.append({interval.start, interval.end});
    }
    std::sort(applicable.begin(), applicable.end());
    qint64 shift = 0;
    qint64 mergedStart = -1;
    qint64 mergedEnd = -1;
    for (const auto &interval : applicable) {
      if (mergedStart < 0) {
        mergedStart = interval.first;
        mergedEnd = interval.second;
      } else if (interval.first <= mergedEnd) {
        mergedEnd = qMax(mergedEnd, interval.second);
      } else {
        shift += mergedEnd - mergedStart;
        mergedStart = interval.first;
        mergedEnd = interval.second;
      }
    }
    if (mergedStart >= 0)
      shift += mergedEnd - mergedStart;
    if (shift > 0) {
      clip["startMs"] = start - shift;
      value = clip;
    }
  }
  return true;
}

bool TimelineEditor::rippleTrimEnd(QVariantList &clips, const QString &clipId,
                                   qint64 requestedEnd,
                                   const QStringList &syncLockedTracks,
                                   qint64 *deltaMs) {
  int index = -1;
  for (int i = 0; i < clips.size(); ++i)
    if (clips.at(i).toMap().value("id").toString() == clipId) {
      index = i;
      break;
    }
  if (index < 0)
    return false;
  auto clip = clips.at(index).toMap();
  const qint64 start = clipStart(clip);
  const qint64 oldEnd = clipEnd(clip);
  const qint64 sourceIn = clip.value("sourceInMs").toLongLong();
  const qint64 sourceDuration =
      qMax(sourceIn + 1, clip.value("sourceDurationMs").toLongLong());
  const qint64 maxEnd = start + qMax<qint64>(1, sourceDuration - sourceIn);
  const qint64 end = qBound(start + 1, requestedEnd, maxEnd);
  if (end == oldEnd)
    return false;
  const qint64 delta = end - oldEnd;
  clip["durationMs"] = end - start;
  clips[index] = clip;
  const QString track = clip.value("track").toString();
  for (int i = 0; i < clips.size(); ++i) {
    if (i == index)
      continue;
    auto other = clips.at(i).toMap();
    const QString otherTrack = other.value("track").toString();
    if ((otherTrack == track || syncLockedTracks.contains(otherTrack)) &&
        clipStart(other) >= oldEnd)
      other["startMs"] = qMax<qint64>(0, clipStart(other) + delta);
    clips[i] = other;
  }
  if (deltaMs)
    *deltaMs = delta;
  return true;
}

bool TimelineEditor::closeGap(QVariantList &clips, const QString &track,
                              qint64 gapStartMs, qint64 gapEndMs) {
  if (gapEndMs <= gapStartMs)
    return false;
  const qint64 shift = gapEndMs - gapStartMs;
  bool changed = false;
  for (auto &value : clips) {
    auto clip = value.toMap();
    if (clip.value("track").toString() == track &&
        clipStart(clip) >= gapEndMs) {
      clip["startMs"] = clipStart(clip) - shift;
      value = clip;
      changed = true;
    }
  }
  return changed;
}
