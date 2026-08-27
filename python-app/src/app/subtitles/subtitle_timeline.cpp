#include "app/subtitles/subtitle_timeline.h"

#include <QtMath>

QVariantList
SubtitleTimeline::clipsFromTranscript(const QVariantList &transcript) {
  QVariantList clips;
  clips.reserve(transcript.size());
  qint64 previousEndMs = 0;
  for (int index = 0; index < transcript.size(); ++index) {
    const QVariantMap segment = transcript[index].toMap();
    const qint64 startMs =
        qMax<qint64>(0, qRound64(segment.value("start").toDouble() * 1000.0));
    const qint64 safeStartMs = qMax(previousEndMs, startMs);
    const qint64 endMs = qMax<qint64>(
        safeStartMs + 1, qRound64(segment.value("end").toDouble() * 1000.0));
    const QString text = segment.value("text").toString().trimmed();
    if (text.isEmpty())
      continue;
    clips.append(QVariantMap{{"name", text},
                             {"text", text},
                             {"kind", QStringLiteral("subtitle")},
                             {"track", QStringLiteral("S1")},
                             {"startMs", safeStartMs},
                             {"sourceInMs", 0},
                             {"sourceDurationMs", endMs - safeStartMs},
                             {"durationMs", endMs - safeStartMs},
                             {"enabled", true},
                             {"transcriptIndex", index}});
    previousEndMs = endMs;
  }
  return clips;
}
