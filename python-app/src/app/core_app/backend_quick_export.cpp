// Quick Export support: what the sequence actually is, and what a render of it
// would cost.
//
// The size model below is the same one the Export page computes inline in QML,
// transcribed with identical constants. That duplication is deliberate and
// worth stating plainly: the QML version is a reactive binding over a dozen
// view properties, and routing it through a Q_INVOKABLE would make it go stale
// whenever one of those changed unless every dependency were touched by hand -
// a real regression risk in a page that currently works, for no user-visible
// gain. The two must be changed together; if they ever disagree, the bug is
// that there are two of them.
//
// Both numbers live here rather than only in the Export page because Quick
// Export needs them too, and a popover that quoted a different file size from
// the Export page for the same render would make both look wrong.

#include "app/core_app/backend.h"

#include <algorithm>
#include <cmath>

QVariantMap Backend::sequenceSourceInfo() const {
  // "Match Source" means the earliest visual clip: audio has no picture, and
  // the first thing on the timeline is what sets the sequence's format.
  QVariantMap earliest;
  qint64 earliestStart = std::numeric_limits<qint64>::max();
  for (const QVariant &value : m_clips) {
    const QVariantMap clip = value.toMap();
    const QString kind = clip.value("kind").toString();
    if (kind == QStringLiteral("audio") || kind == QStringLiteral("subtitle") ||
        kind == QStringLiteral("effect"))
      continue;
    const qint64 start = clip.value("startMs").toLongLong();
    if (start < earliestStart) {
      earliestStart = start;
      earliest = clip;
    }
  }

  // Fall back to 1080p30 rather than to zero: an audio-only sequence still has
  // to export something, and a zero-sized frame would be refused by ffmpeg
  // with an error nobody could act on.
  int width = 1920;
  int height = 1080;
  double frameRate = 30.0;
  bool hasVideo = false;
  if (!earliest.isEmpty()) {
    const QVariantMap media = mediaById(earliest.value("mediaId").toString());
    const int mediaWidth = media.value("width").toInt();
    const int mediaHeight = media.value("height").toInt();
    const double mediaFps = media.value("frameRate").toDouble();
    if (mediaWidth > 0 && mediaHeight > 0) {
      width = mediaWidth;
      height = mediaHeight;
      hasVideo = true;
    }
    if (mediaFps > 0.0)
      frameRate = mediaFps;
  }

  return QVariantMap{{QStringLiteral("width"), width},
                     {QStringLiteral("height"), height},
                     {QStringLiteral("frameRate"), frameRate},
                     {QStringLiteral("durationMs"), durationMs()},
                     {QStringLiteral("hasVideo"), hasVideo}};
}

QVariantMap Backend::exportEstimate(const QVariantMap &settings) const {
  const QVariantMap source = sequenceSourceInfo();
  const int sourceWidth = qMax(1, source.value("width").toInt());
  const int sourceHeight = qMax(1, source.value("height").toInt());

  const int width =
      settings.value("width", sourceWidth).toInt() > 0
          ? settings.value("width", sourceWidth).toInt()
          : sourceWidth;
  const int height =
      settings.value("height", sourceHeight).toInt() > 0
          ? settings.value("height", sourceHeight).toInt()
          : sourceHeight;
  const double frameRate =
      settings.value("frameRate", source.value("frameRate")).toDouble() > 0.0
          ? settings.value("frameRate", source.value("frameRate")).toDouble()
          : 30.0;
  const QString quality =
      settings.value("quality", QStringLiteral("high")).toString();
  const double ceilingMbps = settings.value("videoBitrateMbps", 0.0).toDouble();
  const bool audioEnabled = settings.value("audioEnabled", true).toBool();
  const int audioBitrateKbps =
      qMax(0, settings.value("audioBitrateKbps", 192).toInt());

  // The encoder runs at constant quality and treats the max bitrate as a
  // ceiling, so multiplying that ceiling by the duration describes a file it
  // will almost never write. What the size actually follows is how much picture
  // there is to describe: the footage is fitted inside the output frame, so
  // letterbox bars cost close to nothing, and upscaling cannot invent detail
  // the source never had - which is why the same clip at 8K is nowhere near
  // four times its 4K size. Estimate from the fitted picture, allow enlargement
  // to cost up to four times the source's own pixels, and stop at the ceiling
  // the encoder would clamp to anyway. The bits-per-pixel figures are measured
  // off this encoder at each CRF, not guessed.
  const double bitsPerPixel = quality == QStringLiteral("low")
                                  ? 0.08
                                  : quality == QStringLiteral("medium") ? 0.13
                                                                        : 0.20;
  const double fittedScale = std::min(double(width) / double(sourceWidth),
                                      double(height) / double(sourceHeight));
  const double codedPixels =
      std::min(double(sourceWidth) * double(sourceHeight) *
                   std::min(fittedScale * fittedScale, 4.0),
               double(width) * double(height));
  double videoMbps =
      std::max(0.3, codedPixels * frameRate * bitsPerPixel / 1000000.0);
  if (ceilingMbps > 0.0)
    videoMbps = std::min(videoMbps, ceilingMbps);

  const double seconds = std::max(0.0, durationMs() / 1000.0);
  const double audioMbps = audioEnabled ? audioBitrateKbps / 1000.0 : 0.0;
  const double sizeMb = seconds * (videoMbps + audioMbps) / 8.0;

  return QVariantMap{{QStringLiteral("videoMbps"), videoMbps},
                     {QStringLiteral("sizeMb"), sizeMb},
                     {QStringLiteral("width"), width},
                     {QStringLiteral("height"), height},
                     {QStringLiteral("frameRate"), frameRate}};
}
