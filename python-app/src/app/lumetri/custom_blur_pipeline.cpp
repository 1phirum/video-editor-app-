
#include "app/lumetri/custom_blur_pipeline.h"

#include <QVariantMap>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {

// Ceiling on the gblur stages one mask may emit. A ramp is approximated by a run
// of time-gated stages, and past a hundred of them the filtergraph costs more to
// set up than the smoothness is worth.
constexpr int kMaxStages = 96;
// Samples per second inside an animated interval. Blurriness is a soft quantity,
// so two steps a second stays under the point where a ramp reads as a staircase.
constexpr double kSamplesPerSecond = 2.0;
// Below this, gblur is doing nothing an eye can see, so the stage is dropped and
// the region is composited back untouched.
constexpr double kMinVisibleSigma = 0.05;
constexpr double kMaxAmount = 30.0;

double bounded(const QVariantMap &values, const QString &key, double fallback,
               double minimum, double maximum) {
  return qBound(minimum, values.value(key, fallback).toDouble(), maximum);
}

QString number(double value) { return QString::number(value, 'f', 3); }

qint64 frameTime(const QVariant &frame) {
  return frame.toMap().value("timeMs").toLongLong();
}
double frameValue(const QVariant &frame) {
  return frame.toMap().value("value").toDouble();
}
bool frameHolds(const QVariant &frame) {
  return frame.toMap().value("hold").toBool();
}

// The channel the Effect Controls panel animates. Spelled out here rather than
// called from KeyframeEngine::instanceChannel because the export modules do not
// link the timeline module - if one of the two ever changes, the other has to
// change with it or a keyframed blur silently exports flat.
QString amountChannel(const QString &instanceId) {
  if (instanceId.isEmpty())
    return QStringLiteral("amount");
  return QStringLiteral("fx:%1:amount").arg(instanceId);
}

// Frames as the keyframe engine stores them, sorted and clamped to the range the
// Blurriness parameter accepts. A frame with no time of its own is dropped
// rather than defaulted: a bogus 0 would drag the whole curve to the start of the
// sequence, which is worse than ignoring one keyframe.
QVariantList normalizedFrames(const QVariant &value) {
  QVariantList frames;
  const QVariantList raw = value.toList();
  frames.reserve(raw.size());
  for (const QVariant &entry : raw) {
    const QVariantMap frame = entry.toMap();
    if (!frame.contains(QStringLiteral("timeMs")))
      continue;
    bool ok = false;
    const qint64 timeMs = frame.value("timeMs").toLongLong(&ok);
    if (!ok || timeMs < 0)
      continue;
    frames.append(QVariantMap{
        {"timeMs", timeMs},
        {"value", qBound(0.0, frame.value("value").toDouble(), kMaxAmount)},
        {"hold", frame.value("interpolation").toString() ==
                     QStringLiteral("hold")}});
  }
  std::sort(frames.begin(), frames.end(),
            [](const QVariant &left, const QVariant &right) {
              return frameTime(left) < frameTime(right);
            });
  return frames;
}

// Largest value anywhere on the curve. This is what decides whether the mask is
// worth emitting at all: sampling the parameter's stored value instead would drop
// a blur that starts at 0 and ramps up, which is exactly the shape a user builds
// when they want the blur to arrive partway through a shot.
double peakValue(const QVariantList &frames, double fallback) {
  if (frames.isEmpty())
    return fallback;
  double peak = 0.0;
  for (const QVariant &frame : frames)
    peak = qMax(peak, frameValue(frame));
  return peak;
}

// One gblur, optionally gated to a stretch of the timeline. The window is an
// `enable` expression rather than a sendcmd script because `enable` is understood
// by every gblur build and needs no second filter in the chain; the cost is that
// a ramp becomes a series of steps instead of a continuous slide.
QString blurStage(double sigma, const QString &window) {
  const QString value = number(qBound(0.0, sigma, kMaxAmount));
  if (window.isEmpty())
    return QStringLiteral("gblur=sigma=%1").arg(value);
  return QStringLiteral("gblur=sigma=%1:enable='%2'").arg(value, window);
}

// The gblur run for one mask. `t` here is sequence time: these filters are
// appended after the clip's setpts has moved it to its timeline position, which
// is the same clock the keyframe times are on.
QString blurChain(const QVariantList &frames, double staticAmount) {
  if (frames.isEmpty())
    return blurStage(staticAmount, QString());
  // A single keyframe is a constant - the engine clamps to the end values outside
  // the curve, and with one frame every time is outside it.
  if (frames.size() == 1)
    return blurStage(frameValue(frames.first()), QString());

  QStringList stages;
  const double firstTime = double(frameTime(frames.first())) / 1000.0;
  const double firstValue = frameValue(frames.first());
  const double lastTime = double(frameTime(frames.last())) / 1000.0;
  const double lastValue = frameValue(frames.last());

  // Held flat before the first keyframe and after the last one, matching
  // KeyframeEngine::interpolatedValue.
  if (firstTime > 0.0 && firstValue >= kMinVisibleSigma)
    stages << blurStage(firstValue,
                        QStringLiteral("lt(t,%1)").arg(number(firstTime)));

  int budget = kMaxStages;
  for (int i = 1; i < frames.size() && budget > 0; ++i) {
    const double from = double(frameTime(frames.at(i - 1))) / 1000.0;
    const double to = double(frameTime(frames.at(i))) / 1000.0;
    if (to <= from)
      continue;
    const double startValue = frameValue(frames.at(i - 1));
    const double endValue = frameValue(frames.at(i));
    const bool holds = frameHolds(frames.at(i - 1));
    int steps = 1;
    if (!holds && std::abs(endValue - startValue) > kMinVisibleSigma)
      steps = qBound(1, int(std::ceil((to - from) * kSamplesPerSecond)), 40);
    steps = qMin(steps, budget);
    budget -= steps;
    for (int step = 0; step < steps; ++step) {
      const double stepStart = from + (to - from) * double(step) / steps;
      const double stepEnd = from + (to - from) * double(step + 1) / steps;
      const double sigma =
          holds ? startValue
                : startValue + (endValue - startValue) *
                                   ((double(step) + 0.5) / double(steps));
      if (sigma < kMinVisibleSigma)
        continue;
      // Half-open, so two neighbouring stages never both fire on one frame.
      stages << blurStage(sigma, QStringLiteral("gte(t,%1)*lt(t,%2)")
                                     .arg(number(stepStart), number(stepEnd)));
    }
  }

  if (lastValue >= kMinVisibleSigma)
    stages << blurStage(lastValue,
                        QStringLiteral("gte(t,%1)").arg(number(lastTime)));

  // A curve that never rises above the visible threshold still has to leave a
  // filter behind: the surrounding crop and overlay are already in the chain and
  // need something between them.
  if (stages.isEmpty())
    return blurStage(0.0, QString());
  return stages.join(QLatin1Char(','));
}

// The curve's value at one instant, with the same rules the keyframe engine
// uses: flat outside the end frames, linear between, and a "hold" left frame
// freezing its interval.
double curveValueAt(const QVariantList &frames, double fallback,
                    qint64 timeMs) {
  if (frames.isEmpty())
    return fallback;
  if (timeMs <= frameTime(frames.first()))
    return frameValue(frames.first());
  if (timeMs >= frameTime(frames.last()))
    return frameValue(frames.last());
  for (int i = 1; i < frames.size(); ++i) {
    const qint64 to = frameTime(frames.at(i));
    if (timeMs > to)
      continue;
    const qint64 from = frameTime(frames.at(i - 1));
    const double a = frameValue(frames.at(i - 1));
    const double b = frameValue(frames.at(i));
    if (to <= from || frameHolds(frames.at(i - 1)))
      return a;
    return a + (b - a) * double(timeMs - from) / double(to - from);
  }
  return fallback;
}

QVariant holdFrame(qint64 timeMs, double value) {
  return QVariant(QVariantMap{
      {"timeMs", timeMs}, {"value", value}, {"hold", true}});
}

// The curve an effect-track item implies: zero outside its bar, its own value
// inside. The frame one millisecond before the end holds, so the blur is cut at
// the item's edge rather than ramped down into it - a bare zero at the end would
// read as "fade out over the whole last interval".
QVariantList windowedFrames(const QVariantList &frames, double amount,
                            qint64 startMs, qint64 endMs) {
  if (endMs <= startMs + 1)
    return {};
  QVariantList out;
  if (startMs > 0)
    out.append(holdFrame(0, 0.0));
  if (frames.isEmpty()) {
    out.append(holdFrame(startMs, amount));
  } else {
    out.append(QVariantMap{{"timeMs", startMs},
                           {"value", curveValueAt(frames, amount, startMs)},
                           {"hold", false}});
    for (const QVariant &frame : frames) {
      const qint64 timeMs = frameTime(frame);
      if (timeMs > startMs && timeMs < endMs - 1)
        out.append(frame);
    }
    out.append(holdFrame(endMs - 1, curveValueAt(frames, amount, endMs - 1)));
  }
  out.append(holdFrame(endMs, 0.0));
  return out;
}

} // namespace

QVariantList CustomBlurPipeline::enabledMasks(const QVariantList &effectStack,
                                              const QVariantMap &keyframes) {
  return collectMasks(effectStack, keyframes, false, 0, 0);
}

QVariantList CustomBlurPipeline::windowedMasks(const QVariantList &effectStack,
                                               const QVariantMap &keyframes,
                                               qint64 startMs, qint64 endMs) {
  return collectMasks(effectStack, keyframes, true, startMs, endMs);
}

QVariantList CustomBlurPipeline::collectMasks(const QVariantList &effectStack,
                                              const QVariantMap &keyframes,
                                              bool windowed, qint64 startMs,
                                              qint64 endMs) {
  QVariantList result;
  for (const QVariant &value : effectStack) {
    const QVariantMap instance = value.toMap();
    if (!instance.value("enabled", true).toBool() ||
        instance.value("definitionId").toString() !=
            QStringLiteral("custom_blur"))
      continue;

    const QVariantMap parameters = instance.value("parameters").toMap();
    const double amount = bounded(parameters, "amount", 12.0, 0.0, kMaxAmount);
    QVariantList frames = keyframes.isEmpty()
                              ? QVariantList()
                              : normalizedFrames(keyframes.value(
                                    amountChannel(instance.value("id")
                                                      .toString())));
    const QVariantMap mask = parameters.value("mask").toMap();
    const double x = bounded(mask, "x", 0.30, 0.0, 0.98);
    const double y = bounded(mask, "y", 0.35, 0.0, 0.98);
    const double width =
        qMin(bounded(mask, "width", 0.40, 0.02, 1.0), 1.0 - x);
    const double height =
        qMin(bounded(mask, "height", 0.30, 0.02, 1.0), 1.0 - y);
    if (peakValue(frames, amount) <= 0.001 || width < 0.02 || height < 0.02)
      continue;
    if (windowed) {
      frames = windowedFrames(frames, amount, startMs, endMs);
      if (frames.isEmpty())
        continue;
    }
    QVariantMap entry{{"amount", amount},
                      {"x", x},
                      {"y", y},
                      {"width", width},
                      {"height", height}};
    // Only when the parameter is actually animated, so an un-keyframed blur
    // produces the exact filter string it produced before keyframes existed.
    if (!frames.isEmpty())
      entry.insert(QStringLiteral("amountFrames"), frames);
    result.append(entry);
  }
  return result;
}

QString CustomBlurPipeline::appendFilters(QStringList *filters,
                                          const QString &inputLabel,
                                          const QString &labelPrefix,
                                          const QVariantList &masks) {
  if (!filters || masks.isEmpty())
    return inputLabel;

  QString current = inputLabel;
  for (int i = 0; i < masks.size(); ++i) {
    const QVariantMap mask = masks.at(i).toMap();
    const QString keep = QStringLiteral("%1_keep%2").arg(labelPrefix).arg(i);
    const QString blurSource =
        QStringLiteral("%1_blursrc%2").arg(labelPrefix).arg(i);
    const QString region =
        QStringLiteral("%1_region%2").arg(labelPrefix).arg(i);
    const QString output =
        QStringLiteral("%1_masked%2").arg(labelPrefix).arg(i);
    const double x = mask.value("x").toDouble();
    const double y = mask.value("y").toDouble();
    const double width = mask.value("width").toDouble();
    const double height = mask.value("height").toDouble();
    const double amount = mask.value("amount").toDouble();
    // The mask rectangle itself stays where it is for the whole clip: crop reads
    // w and h once, when the filter is configured, so a moving mask would need a
    // different construction than this one. Only the strength follows the
    // keyframes - which is what turns the blur off for the rest of the shot.
    const QString blur =
        blurChain(mask.value("amountFrames").toList(), amount);

    filters->append(QStringLiteral("[%1]split=2[%2][%3]")
                        .arg(current, keep, blurSource));
    filters->append(
        QStringLiteral(
            "[%1]crop=w='max(2,min(iw,trunc(iw*%2/2)*2))':"
            "h='max(2,min(ih,trunc(ih*%3/2)*2))':"
            "x='min(iw-ow,max(0,iw*%4))':y='min(ih-oh,max(0,ih*%5))',"
            "%6[%7]")
            .arg(blurSource)
            .arg(width, 0, 'f', 6)
            .arg(height, 0, 'f', 6)
            .arg(x, 0, 'f', 6)
            .arg(y, 0, 'f', 6)
            .arg(blur, region));
    filters->append(
        QStringLiteral("[%1][%2]overlay=x='main_w*%3':y='main_h*%4':"
                       "eof_action=pass:shortest=1[%5]")
            .arg(keep, region)
            .arg(x, 0, 'f', 6)
            .arg(y, 0, 'f', 6)
            .arg(output));
    current = output;
  }
  return current;
}
