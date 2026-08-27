#pragma once

#include <QString>
#include <QVariantMap>

// Central policy for placing very long media on the timeline. Keeping this
// outside Backend prevents duration-specific rendering/preparation rules from
// growing inside the main application service.
class LongMediaTimelineHandler final {
public:
  static constexpr qint64 kLongFormThresholdMs = 2LL * 60 * 60 * 1000;

  static bool isLongForm(qint64 durationMs);
  static QVariantMap placementRequest(const QVariantMap &media, qint64 startMs,
                                      const QString &track);
  static QVariantMap timelineClip(const QVariantMap &media,
                                  const QVariantMap &request,
                                  const QString &clipId);
};
