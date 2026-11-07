#pragma once

#include <QHash>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

// Split non-overlapping video sequences at output-frame boundaries. Each job
// opens only the sources intersecting its window, rather than every episode.
// Include overlapping audio without opening the entire narration at once.
// Subtitles and animation retain the existing general exporter.
class SequentialExportPlan {
public:
  static QVariantList build(const QVariantList &clips,
                            const QVariantList &media,
                            const QVariantList &trackStates,
                            double frameRate) {
    QHash<QString, QVariantMap> tracks;
    bool videoSolo = false;
    for (const QVariant &value : trackStates) {
      const auto state = value.toMap();
      tracks.insert(state.value("id").toString(), state);
      videoSolo = videoSolo || (state.value("id").toString().startsWith('V') &&
                                state.value("solo").toBool());
    }
    QVariantList video;
    QVariantList effects;
    QVariantList audio;
    QString track;
    for (const QVariant &value : clips) {
      const auto clip = value.toMap();
      const QString row = clip.value("track").toString();
      const auto state = tracks.value(row);
      if (!clip.value("enabled", true).toBool() ||
          !state.value("visible", true).toBool() ||
          (row.startsWith('V') && videoSolo && !state.value("solo").toBool()))
        continue;
      if (!clip.value("keyframes").toMap().isEmpty())
        return {};
      const QString kind = clip.value("kind").toString();
      if (kind == "effect") {
        effects.append(value);
        continue;
      }
      // Retiming has buffered audio/video stages whose draining depends on
      // source-frame boundaries. Keep those on the complete sequence graph.
      const double speed = clip.value("effects").toMap().value("speed", 100).toDouble();
      if (!std::isfinite(speed) || qAbs(speed - 100.0) > 0.0001)
        return {};
      if (kind == "audio" && row.startsWith('A')) {
        audio.append(value);
        continue;
      }
      if (kind != "video" || !row.startsWith('V') ||
          (!track.isEmpty() && row != track))
        return {};
      track = row;
      video.append(value);
    }
    if (video.size() <= 8)
      return {};
    std::sort(video.begin(), video.end(), [](const QVariant &a, const QVariant &b) {
      return a.toMap().value("startMs").toLongLong() <
             b.toMap().value("startMs").toLongLong();
    });
    qint64 endMs = 0;
    for (const QVariant &value : video) {
      const auto clip = value.toMap();
      const qint64 start = clip.value("startMs").toLongLong();
      const qint64 duration = clip.value("durationMs").toLongLong();
      if (start != endMs || duration <= 0)
        return {};
      endMs = start + duration;
    }
    int width = 0, height = 0;
    const QString firstMedia = video.first().toMap().value("mediaId").toString();
    for (const QVariant &value : media) {
      const auto item = value.toMap();
      if (item.value("id").toString() == firstMedia) {
        width = item.value("width").toInt();
        height = item.value("height").toInt();
        if (frameRate <= 0)
          frameRate = item.value("frameRate", 30).toDouble();
        break;
      }
    }
    if (!std::isfinite(frameRate) || frameRate <= 0)
      return {};
    QVariantList jobs;
    qint64 firstFrame = 0;
    for (const QVariant &value : video) {
      const auto original = value.toMap();
      const qint64 end = original.value("startMs").toLongLong() +
                         original.value("durationMs").toLongLong();
      const qint64 lastFrame = qint64(std::ceil(end * frameRate / 1000.0));
      if (lastFrame <= firstFrame)
        continue;
      const qint64 windowStart = qRound64(firstFrame * 1000.0 / frameRate);
      const qint64 windowEnd = qRound64(lastFrame * 1000.0 / frameRate);
      QVariantList window;
      for (const QVariant &candidate : video + effects + audio) {
        auto clip = candidate.toMap();
        const qint64 start = clip.value("startMs").toLongLong();
        const qint64 finish = start + clip.value("durationMs").toLongLong();
        if (start >= windowEnd || finish <= windowStart)
          continue;
        // Keep the whole clip and its source trim. Negative placement preserves
        // the phase of frames/clip effects that began just before this window.
        clip["startMs"] = start - windowStart;
        window.append(clip);
      }
      jobs.append(QVariantMap{{"startMs", windowStart},
                              {"durationMs", windowEnd - windowStart},
                              {"frameCount", lastFrame - firstFrame},
                              {"frameRate", frameRate},
                              {"width", width}, {"height", height},
                              {"clips", window}});
      firstFrame = lastFrame;
    }
    return jobs;
  }
};
