#include "app/timeline/long_media_timeline_handler.h"

#include "app/timeline/large_media_policy.h"

#include <QtGlobal>

bool LongMediaTimelineHandler::isLongForm(qint64 durationMs) {
  return durationMs >= kLongFormThresholdMs;
}

QVariantMap LongMediaTimelineHandler::placementRequest(
    const QVariantMap &media, qint64 startMs, const QString &track) {
  qint64 durationMs = media.value("durationMs").toLongLong();
  if (durationMs <= 0)
    durationMs = media.value("kind").toString() == QStringLiteral("image")
                     ? 5000
                     : 1000;
  const bool lightweight =
      LargeMediaPolicy::requiresLightweightHandling(media);
  return QVariantMap{{"mediaId", media.value("id")},
                     {"startMs", qMax<qint64>(0, startMs)},
                     {"track", track},
                     {"durationMs", durationMs},
                     {"longFormMedia", isLongForm(durationMs)},
                     {"lightweightMedia", lightweight}};
}

QVariantMap LongMediaTimelineHandler::timelineClip(
    const QVariantMap &media, const QVariantMap &request,
    const QString &clipId) {
  const QString kind = media.value("kind").toString();
  qint64 durationMs = request.value("durationMs").toLongLong();
  if (durationMs <= 0)
    durationMs = media.value("durationMs").toLongLong();
  durationMs = qMax<qint64>(1, durationMs);

  const bool lightweight =
      request.value(QStringLiteral("lightweightMedia")).toBool() ||
      LargeMediaPolicy::requiresLightweightHandling(media);

  QVariantMap clip{{"id", clipId},
                   {"mediaId", media.value("id")},
                   {"name", media.value("name")},
                   {"kind", kind},
                   {"track", request.value("track")},
                   {"startMs", qMax<qint64>(0, request.value("startMs").toLongLong())},
                   {"sourceInMs", 0},
                   {"sourceDurationMs", durationMs},
                   {"durationMs", durationMs},
                   {"enabled", true},
                   {"longFormMedia", isLongForm(durationMs)},
                   // QML uses this to avoid duration-sized visual work. The
                   // actual source remains full quality for playback/export.
                   {"timelineRenderMode",
                    lightweight ? QStringLiteral("lightweight")
                                : QStringLiteral("normal")}};
  if (kind == QStringLiteral("video"))
    clip["embeddedAudio"] = media.value("channels").toInt() > 0;
  return clip;
}
