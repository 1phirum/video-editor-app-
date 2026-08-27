#include "app/effects/clip_effects_pipeline.h"

#include <QStringList>
#include <QtGlobal>
#include <cmath>

namespace {
double value(const QVariantMap &effects, const QString &key, double fallback) {
  return effects.value(key, fallback).toDouble();
}
} // namespace

QString ClipEffectsPipeline::videoFilters(const QVariantMap &effects) {
  QStringList filters;
  const double left = qBound(0.0, value(effects, "cropLeft", 0.0), 49.0);
  const double right = qBound(0.0, value(effects, "cropRight", 0.0), 49.0);
  const double top = qBound(0.0, value(effects, "cropTop", 0.0), 49.0);
  const double bottom = qBound(0.0, value(effects, "cropBottom", 0.0), 49.0);
  if (left + right > 0.001 || top + bottom > 0.001) {
    const double width = qMax(0.02, 1.0 - (left + right) / 100.0);
    const double height = qMax(0.02, 1.0 - (top + bottom) / 100.0);
    filters << QStringLiteral("crop=iw*%1:ih*%2:iw*%3:ih*%4")
                   .arg(width, 0, 'f', 5)
                   .arg(height, 0, 'f', 5)
                   .arg(left / 100.0, 0, 'f', 5)
                   .arg(top / 100.0, 0, 'f', 5);
  }

  const bool uniform = effects.value("uniformScale", true).toBool();
  const double scale = qBound(10.0, value(effects, "scale", 100.0), 400.0);
  const double scaleWidth = qBound(
      10.0, value(effects, "scaleWidth", scale), 600.0);
  const double scaleHeight = qBound(
      10.0, value(effects, "scaleHeight", scale), 600.0);
  const double sx = (uniform ? scale : scaleWidth) / 100.0;
  const double sy = (uniform ? scale : scaleHeight) / 100.0;
  if (std::abs(sx - 1.0) > 0.001 || std::abs(sy - 1.0) > 0.001)
    filters << QStringLiteral("scale=iw*%1:ih*%2")
                   .arg(sx, 0, 'f', 5)
                   .arg(sy, 0, 'f', 5);
  if (effects.value("horizontalFlip").toBool())
    filters << QStringLiteral("hflip");
  if (effects.value("verticalFlip").toBool())
    filters << QStringLiteral("vflip");

  const double rotation =
      qBound(-180.0, value(effects, "rotation", 0.0), 180.0);
  const double opacity = qBound(0.0, value(effects, "opacity", 100.0), 100.0);
  if (std::abs(rotation) > 0.001 || opacity < 99.999)
    filters << QStringLiteral("format=rgba");
  if (std::abs(rotation) > 0.001)
    filters
        << QStringLiteral(
               "rotate=%1*PI/180:ow=rotw(%1*PI/180):oh=roth(%1*PI/180):c=none")
               .arg(rotation, 0, 'f', 4);
  const double blur = qBound(0.0, value(effects, "blur", 0.0), 100.0);
  if (blur > 0.001)
    filters << QStringLiteral("gblur=sigma=%1")
                   .arg(qMax(0.1, blur / 12.0), 0, 'f', 3);
  if (opacity < 99.999)
    filters << QStringLiteral("colorchannelmixer=aa=%1")
                   .arg(opacity / 100.0, 0, 'f', 4);
  const double antiFlicker = qBound(0.0, value(effects, "antiFlicker", 0.0), 1.0);
  if (antiFlicker > 0.001)
    filters << QStringLiteral("smartblur=lr=%1:ls=0.1:lt=0")
                   .arg(1.0 + antiFlicker * 2.0, 0, 'f', 3);
  return filters.join(',');
}

QString ClipEffectsPipeline::overlayX(const QVariantMap &effects) {
  const double x =
      qBound(-100.0, value(effects, "positionX", 0.0), 100.0) / 100.0;
  return QStringLiteral("(main_w-overlay_w)/2+main_w*%1").arg(x, 0, 'f', 5);
}

QString ClipEffectsPipeline::overlayY(const QVariantMap &effects) {
  const double y =
      qBound(-100.0, value(effects, "positionY", 0.0), 100.0) / 100.0;
  return QStringLiteral("(main_h-overlay_h)/2+main_h*%1").arg(y, 0, 'f', 5);
}

QString ClipEffectsPipeline::audioFilters(const QVariantMap &effects,
                                          int channels) {
  QStringList filters;
  // Vocal removal is performed by the Demucs accompaniment stem. Do not use
  // center cancellation as a fallback: it also removes centered music,
  // ambience, bass, and effects. Until Demucs has produced its WAV, callers
  // should keep the original audio unchanged.
  const double volumeDb = qBound(-60.0, value(effects, "volumeDb", 0.0), 15.0);
  if (!effects.value("volumeBypass", false).toBool() && std::abs(volumeDb) > 0.001)
    filters << QStringLiteral("volume=%1dB").arg(volumeDb, 0, 'f', 2);
  if (channels >= 2) {
    const double leftDb = qBound(-60.0, value(effects, "channelVolumeLeft", 0.0), 15.0);
    const double rightDb = qBound(-60.0, value(effects, "channelVolumeRight", 0.0), 15.0);
    if (std::abs(leftDb) > 0.001 || std::abs(rightDb) > 0.001) {
      const double left = std::pow(10.0, leftDb / 20.0);
      const double right = std::pow(10.0, rightDb / 20.0);
      filters << QStringLiteral("pan=stereo|c0=c0*%1|c1=c1*%2")
                     .arg(left, 0, 'f', 5)
                     .arg(right, 0, 'f', 5);
    }
    const double balance = qBound(-100.0, value(effects, "balance",
                                                  value(effects, "pan", 0.0) * 100.0),
                                  100.0) / 100.0;
    if (std::abs(balance) > 0.001)
      filters << QStringLiteral("stereotools=balance_out=%1")
                     .arg(balance, 0, 'f', 4);
  }
  const double denoise =
      qBound(0.0, value(effects, "noiseReduction", 0.0), 100.0);
  if (denoise > 0.001)
    filters
        << QStringLiteral("afftdn=nf=%1").arg(-50.0 + denoise * 0.3, 0, 'f', 2);
  const double highPass =
      qBound(0.0, value(effects, "highPassHz", 0.0), 1000.0);
  if (highPass >= 20.0)
    filters << QStringLiteral("highpass=f=%1").arg(highPass, 0, 'f', 1);
  const double lowPass = qBound(0.0, value(effects, "lowPassHz", 0.0), 22000.0);
  if (lowPass >= 1000.0)
    filters << QStringLiteral("lowpass=f=%1").arg(lowPass, 0, 'f', 1);
  if (effects.value("compressor").toBool())
    filters << QStringLiteral(
        "acompressor=threshold=-18dB:ratio=3:attack=20:release=250:makeup=2dB");
  return filters.join(',');
}
