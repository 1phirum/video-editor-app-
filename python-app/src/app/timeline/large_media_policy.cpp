#include "app/timeline/large_media_policy.h"

#include <QString>

bool LargeMediaPolicy::requiresLightweightHandling(const QVariantMap &media) {
  if (media.value(QStringLiteral("kind")).toString() == QStringLiteral("image"))
    return false;

  const qint64 durationMs = media.value(QStringLiteral("durationMs")).toLongLong();
  const qint64 sizeBytes = media.value(QStringLiteral("sizeBytes")).toLongLong();
  return durationMs >= kLongDurationMs || sizeBytes >= kLargeFileBytes;
}

void LargeMediaPolicy::applyPresentationFlags(QVariantMap *media) {
  if (!media)
    return;

  const bool lightweight = requiresLightweightHandling(*media);
  (*media)[QStringLiteral("largeMedia")] = lightweight;
  (*media)[QStringLiteral("deferMonitorLoad")] = lightweight;
  (*media)[QStringLiteral("timelineRenderMode")] =
      lightweight ? QStringLiteral("lightweight") : QStringLiteral("normal");
  (*media)[QStringLiteral("previewDecodeMode")] =
      lightweight ? QStringLiteral("bounded") : QStringLiteral("normal");
}
