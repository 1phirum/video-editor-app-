#include "app/preview/large_media_preview_job.h"

#include "app/preview/audio_peak_builder.h"
#include "app/preview/filmstrip_builder.h"
#include "app/preview/media_preview_generator.h"

QVariantMap LargeMediaPreviewJob::generate(const QVariantMap &media) {
  QVariantMap result{{QStringLiteral("id"), media.value(QStringLiteral("id"))},
                     {QStringLiteral("thumbnailUrl"), QString()}};
  const QString path = media.value(QStringLiteral("path")).toString();
  const QString kind = media.value(QStringLiteral("kind")).toString();
  if (path.isEmpty())
    return result;

  const qint64 durationMs =
      media.value(QStringLiteral("durationMs")).toLongLong();
  const bool isVideo = kind == QStringLiteral("video");
  const bool isAudio = kind == QStringLiteral("audio");
  if (!isVideo && !isAudio)
    return result;

  if (isVideo) {
    // Seek-based poster, with the old filter-graph thumbnail as the fallback for
    // builds without direct FFmpeg linkage.
    QString poster = FilmstripBuilder::poster(path, durationMs);
    if (poster.isEmpty())
      poster = MediaPreviewGenerator::thumbnail(path, durationMs);
    result[QStringLiteral("thumbnailUrl")] = poster;

    const FilmstripBuilder::Result strip =
        FilmstripBuilder::build(path, durationMs);
    if (strip.valid()) {
      result[QStringLiteral("timelineThumbnailUrl")] = strip.url;
      result[QStringLiteral("filmstripFrames")] = strip.cells;
      result[QStringLiteral("filmstripFrameWidth")] = strip.cellWidth;
      result[QStringLiteral("filmstripFrameHeight")] = strip.cellHeight;
    } else if (!poster.isEmpty()) {
      // One cell is still better than none: the timeline draws the poster across
      // the clip instead of a flat rectangle.
      result[QStringLiteral("timelineThumbnailUrl")] = poster;
      result[QStringLiteral("filmstripFrames")] = 1;
    }
  }

  const bool hasAudio =
      isAudio || media.value(QStringLiteral("channels")).toInt() > 0;
  if (hasAudio) {
    const AudioPeakBuilder::Result waveform =
        AudioPeakBuilder::build(path, durationMs);
    if (waveform.valid())
      result[QStringLiteral("waveformUrl")] = waveform.url;
  }
  return result;
}
