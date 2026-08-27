#include "app/timeline/timeline_editor.h"

#include <QtGlobal>
#include <algorithm>

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
} // namespace

qint64 TimelineEditor::snapTime(qint64 requestedMs, const QVariantList &clips,
                                const QVariantList &markers,
                                const QStringList &excludedClipIds,
                                qint64 playheadMs, qint64 thresholdMs,
                                qint64 durationMs) {
  const QSet<QString> excludedIds(excludedClipIds.begin(),
                                  excludedClipIds.end());
  qint64 best = qMax<qint64>(0, requestedMs);
  qint64 distance = thresholdMs + 1;
  const auto consider = [&](qint64 candidate) {
    candidate = qBound<qint64>(0, candidate, qMax<qint64>(0, durationMs));
    const qint64 d = qAbs(candidate - requestedMs);
    if (d < distance) {
      distance = d;
      best = candidate;
    }
  };
  consider(0);
  consider(durationMs);
  consider(playheadMs);
  for (const auto &value : markers)
    consider(value.toMap().value("positionMs").toLongLong());
  for (const auto &value : clips) {
    const auto clip = value.toMap();
    if (excluded(excludedIds, clip))
      continue;
    consider(clipStart(clip));
    consider(clipEnd(clip));
  }
  return distance <= thresholdMs ? best : qMax<qint64>(0, requestedMs);
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
