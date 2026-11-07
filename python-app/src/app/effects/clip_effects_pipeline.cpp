#include "app/effects/clip_effects_pipeline.h"

#include "core/easing.h"

#include <QHash>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace {
double value(const QVariantMap &effects, const QString &key, double fallback) {
  return effects.value(key, fallback).toDouble();
}

// One keyframe, reduced to what the expression builder needs.
struct Frame {
  double timeSec = 0.0;
  double value = 0.0;
  bool hold = false;
  double easeOut = 0.0;
  double easeIn = 0.0;
};

QVector<Frame> usableFrames(const QVariantList &raw) {
  QVector<Frame> frames;
  frames.reserve(raw.size());
  for (const QVariant &entry : raw) {
    const QVariantMap item = entry.toMap();
    if (!item.contains(QStringLiteral("timeMs")))
      continue;
    Frame frame;
    frame.timeSec = double(item.value("timeMs").toLongLong()) / 1000.0;
    frame.value = item.value("value").toDouble();
    frame.hold = item.value("interpolation").toString() == QStringLiteral("hold");
    frame.easeOut = item.value("easeOut").toDouble();
    frame.easeIn = item.value("easeIn").toDouble();
    frames.append(frame);
  }
  std::sort(frames.begin(), frames.end(),
            [](const Frame &a, const Frame &b) { return a.timeSec < b.timeSec; });
  return frames;
}

QString number(double v) { return QString::number(v, 'f', 5); }

// A linear ramp between two points, as an FFmpeg expression in `t`.
QString rampExpression(double t0, double v0, double t1, double v1) {
  const double span = t1 - t0;
  if (span <= 1e-9)
    return number(v1);
  const double slope = (v1 - v0) / span;
  return QStringLiteral("(%1+(t-%2)*%3)")
      .arg(number(v0), number(t0), number(slope));
}
} // namespace

QString ClipEffectsPipeline::channelExpression(const QVariantList &rawFrames,
                                               double scale, double offset) {
  const QVector<Frame> frames = usableFrames(rawFrames);
  if (frames.size() < 2)
    return {};
  const auto mapped = [scale, offset](double v) { return v * scale + offset; };

  // Built from the last segment backwards, so each `if` nests inside the
  // previous one's else branch and the whole thing stays one expression.
  QString expr = number(mapped(frames.last().value));
  for (int i = frames.size() - 2; i >= 0; --i) {
    const Frame &a = frames.at(i);
    const Frame &b = frames.at(i + 1);
    QString segment;
    if (a.hold) {
      segment = number(mapped(a.value));
    } else if (a.easeOut > 0.0001 || b.easeIn > 0.0001) {
      // An eased segment has no closed form FFmpeg can evaluate, so it is
      // approximated by straight pieces whose endpoints come from the same
      // curve the monitor evaluates. Eight pieces keeps the error under a pixel
      // for the moves this is used for.
      constexpr int kPieces = 8;
      const double span = b.timeSec - a.timeSec;
      QString inner =
          number(mapped(a.value + (b.value - a.value) *
                                      core::cubicBezierEase(1.0, a.easeOut,
                                                           b.easeIn)));
      for (int piece = kPieces - 1; piece >= 0; --piece) {
        const double u0 = double(piece) / kPieces;
        const double u1 = double(piece + 1) / kPieces;
        const double e0 = core::cubicBezierEase(u0, a.easeOut, b.easeIn);
        const double e1 = core::cubicBezierEase(u1, a.easeOut, b.easeIn);
        const QString ramp = rampExpression(
            a.timeSec + u0 * span, mapped(a.value + (b.value - a.value) * e0),
            a.timeSec + u1 * span, mapped(a.value + (b.value - a.value) * e1));
        inner = QStringLiteral("if(lt(t,%1),%2,%3)")
                    .arg(number(a.timeSec + u1 * span), ramp, inner);
      }
      segment = inner;
    } else {
      segment = rampExpression(a.timeSec, mapped(a.value), b.timeSec,
                               mapped(b.value));
    }
    expr = QStringLiteral("if(lt(t,%1),%2,%3)")
               .arg(number(b.timeSec), segment, expr);
  }
  // Before the first keyframe the channel holds its first value, which is how
  // KeyframeEngine::interpolatedValue clamps too.
  return QStringLiteral("if(lt(t,%1),%2,%3)")
      .arg(number(frames.first().timeSec),
           number(mapped(frames.first().value)), expr);
}

double ClipEffectsPipeline::channelPeakAbs(const QVariantList &rawFrames,
                                           double fallback) {
  const QVector<Frame> frames = usableFrames(rawFrames);
  if (frames.isEmpty())
    return std::abs(fallback);
  double peak = 0.0;
  for (const Frame &frame : frames)
    peak = qMax(peak, std::abs(frame.value));
  return peak;
}

QString ClipEffectsPipeline::videoFilters(const QVariantMap &effects,
                                          const QVariantMap &channels) {
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
  const QVariantList rotationFrames =
      channels.value(QStringLiteral("rotation")).toList();
  const QString rotationExpr = ClipEffectsPipeline::channelExpression(
      rotationFrames, M_PI / 180.0);
  const bool rotationAnimated = !rotationExpr.isEmpty();
  if (std::abs(rotation) > 0.001 || opacity < 99.999 || rotationAnimated)
    filters << QStringLiteral("format=rgba");
  if (rotationAnimated) {
    // The angle varies per frame, but a filter's output size cannot, so the
    // canvas is sized for the widest angle the channel ever reaches. Sizing it
    // from the current angle instead would crop the picture at every other
    // angle in the move.
    const double peak =
        qBound(0.0, ClipEffectsPipeline::channelPeakAbs(rotationFrames, rotation),
               180.0);
    filters << QStringLiteral("rotate=%1:ow=rotw(%2):oh=roth(%2):c=none")
                   .arg(rotationExpr)
                   .arg(peak * M_PI / 180.0, 0, 'f', 6);
  } else if (std::abs(rotation) > 0.001) {
    filters
        << QStringLiteral(
               "rotate=%1*PI/180:ow=rotw(%1*PI/180):oh=roth(%1*PI/180):c=none")
               .arg(rotation, 0, 'f', 4);
  }
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

QString ClipEffectsPipeline::overlayX(const QVariantMap &effects,
                                      const QVariantMap &channels) {
  const double x =
      qBound(-100.0, value(effects, "positionX", 0.0), 100.0) / 100.0;
  // Position places the clip's Anchor Point, so the overlay's left edge sits an
  // anchor's worth of width to the left of it. At the 0.5 default this is the
  // old (main_w-overlay_w)/2 exactly; off-centre it is what makes scaling grow
  // away from the anchor instead of away from the middle. Before this the two
  // anchor sliders were stored, clamped, saved - and read by nothing.
  const double anchor =
      qBound(0.0, value(effects, "anchorPointX", 0.5), 1.0);
  // overlay's x accepts an expression in `t`, so a keyframed pan is a real
  // animated move in the file rather than one frozen value.
  const QString animated = channelExpression(
      channels.value(QStringLiteral("positionX")).toList(), 0.01);
  const QString position = animated.isEmpty()
                               ? QStringLiteral("main_w*%1").arg(x, 0, 'f', 5)
                               : QStringLiteral("main_w*(%1)").arg(animated);
  return QStringLiteral("(main_w/2+%1-overlay_w*%2)")
      .arg(position)
      .arg(anchor, 0, 'f', 5);
}

QString ClipEffectsPipeline::overlayY(const QVariantMap &effects,
                                      const QVariantMap &channels) {
  const double y =
      qBound(-100.0, value(effects, "positionY", 0.0), 100.0) / 100.0;
  const double anchor =
      qBound(0.0, value(effects, "anchorPointY", 0.5), 1.0);
  const QString animated = channelExpression(
      channels.value(QStringLiteral("positionY")).toList(), 0.01);
  const QString position = animated.isEmpty()
                               ? QStringLiteral("main_h*%1").arg(y, 0, 'f', 5)
                               : QStringLiteral("main_h*(%1)").arg(animated);
  return QStringLiteral("(main_h/2+%1-overlay_h*%2)")
      .arg(position)
      .arg(anchor, 0, 'f', 5);
}

QString ClipEffectsPipeline::blendMode(const QVariantMap &effects) {
  const QString mode = effects.value("blendMode", "normal").toString();
  // FFmpeg's `blend` filter supplies most Photoshop-style modes by name. A few
  // app modes have no per-pixel expression FFmpeg can evaluate in a
  // format-independent way, so they degrade to the closest valid mode (noted
  // per line) or to a plain source-over composite (empty string):
  //   - linearBurn would need A+B-max, which depends on bit depth; approximated
  //     by multiply (both darken toward black);
  //   - darkerColor/lighterColor are luminance decisions; approximated by the
  //     per-channel darken/lighten;
  //   - dissolve and the four HSL modes (hue/saturation/color/luminosity) have
  //     no blend expression at all and fall back to normal.
  static const QHash<QString, QString> named{
      {"darken", QStringLiteral("all_mode=darken")},
      {"multiply", QStringLiteral("all_mode=multiply")},
      {"colorBurn", QStringLiteral("all_mode=burn")},
      {"linearBurn", QStringLiteral("all_mode=multiply")},   // approx
      {"darkerColor", QStringLiteral("all_mode=darken")},    // approx
      {"lighten", QStringLiteral("all_mode=lighten")},
      {"screen", QStringLiteral("all_mode=screen")},
      {"colorDodge", QStringLiteral("all_mode=dodge")},
      {"linearDodge", QStringLiteral("all_mode=addition")},
      {"lighterColor", QStringLiteral("all_mode=lighten")},  // approx
      {"overlay", QStringLiteral("all_mode=overlay")},
      {"softLight", QStringLiteral("all_mode=softlight")},
      {"hardLight", QStringLiteral("all_mode=hardlight")},
      {"vividLight", QStringLiteral("all_mode=vividlight")},
      {"linearLight", QStringLiteral("all_mode=linearlight")},
      {"pinLight", QStringLiteral("all_mode=pinlight")},
      {"hardMix", QStringLiteral("all_mode=hardmix")},
      {"difference", QStringLiteral("all_mode=difference")},
      {"exclusion", QStringLiteral("all_mode=exclusion")},
      {"subtract", QStringLiteral("all_mode=subtract")},
      {"divide", QStringLiteral("all_mode=divide")},
  };
  return named.value(mode);  // empty for normal/dissolve/hue/saturation/color/luminosity
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
