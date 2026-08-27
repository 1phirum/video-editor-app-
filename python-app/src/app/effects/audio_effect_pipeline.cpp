#include "app/effects/audio_effect_pipeline.h"
#include <QStringList>
#include <QtGlobal>
#include <cmath>

namespace {
double number(const QVariantMap &parameters, const QString &key,
              double fallback, double minimum, double maximum) {
  return qBound(minimum, parameters.value(key, fallback).toDouble(), maximum);
}
} // namespace

QString AudioEffectPipeline::filters(const QVariantList &effectStack,
                                     int channels) {
  QStringList filters;
  for (const auto &value : effectStack) {
    const QVariantMap instance = value.toMap();
    if (!instance.value("enabled", true).toBool())
      continue;
    const QString effectId = instance.value("definitionId").toString();
    const QVariantMap parameters = instance.value("parameters").toMap();

    if (effectId == QStringLiteral("audio_denoise")) {
      const double strength = number(parameters, "strength", 35, 0, 100);
      if (strength > 0.001)
        filters << QStringLiteral("afftdn=nf=%1")
                       .arg(-50.0 + strength * 0.3, 0, 'f', 2);
    } else if (effectId == QStringLiteral("high_pass")) {
      filters << QStringLiteral("highpass=f=%1")
                     .arg(number(parameters, "frequency", 80, 20, 1000), 0,
                          'f', 1);
    } else if (effectId == QStringLiteral("low_pass")) {
      filters << QStringLiteral("lowpass=f=%1")
                     .arg(number(parameters, "frequency", 16000, 1000, 22000),
                          0, 'f', 1);
    } else if (effectId == QStringLiteral("compressor")) {
      const double threshold =
          number(parameters, "threshold", -18, -40, -6);
      const double ratio = number(parameters, "ratio", 3, 1, 10);
      const double makeup = number(parameters, "makeup", 2, 0, 12);
      filters << QStringLiteral(
                     "acompressor=threshold=%1dB:ratio=%2:attack=20:release=250:makeup=%3dB")
                     .arg(threshold, 0, 'f', 1)
                     .arg(ratio, 0, 'f', 2)
                     .arg(makeup, 0, 'f', 1);
    } else if (effectId == QStringLiteral("limiter")) {
      const double ceiling = number(parameters, "ceiling", -1, -12, 0);
      const double linear = std::pow(10.0, ceiling / 20.0);
      filters << QStringLiteral("alimiter=limit=%1:attack=5:release=50")
                     .arg(linear, 0, 'f', 5);
    } else if (effectId == QStringLiteral("echo")) {
      const double delay = number(parameters, "delay", 180, 10, 1000);
      const double decay = number(parameters, "decay", 0.35, 0.1, 0.9);
      filters << QStringLiteral("aecho=0.8:0.9:%1:%2")
                     .arg(delay, 0, 'f', 0)
                     .arg(decay, 0, 'f', 3);
    } else if (effectId == QStringLiteral("vocal_reducer") && channels >= 2) {
      filters << QStringLiteral("pan=stereo|c0=c0-c1|c1=c1-c0")
              << QStringLiteral("volume=1.35");
    } else if (effectId == QStringLiteral("bass")) {
      const double gain = number(parameters, "gain", 3, -24, 24);
      const double frequency = number(parameters, "frequency", 100, 40, 500);
      filters << QStringLiteral("bass=g=%1:f=%2:t=q:w=0.7")
                     .arg(gain, 0, 'f', 2)
                     .arg(frequency, 0, 'f', 1);
    } else if (effectId == QStringLiteral("treble")) {
      const double gain = number(parameters, "gain", 3, -24, 24);
      const double frequency =
          number(parameters, "frequency", 3000, 1000, 16000);
      filters << QStringLiteral("treble=g=%1:f=%2:t=q:w=0.7")
                     .arg(gain, 0, 'f', 2)
                     .arg(frequency, 0, 'f', 1);
    } else if (effectId == QStringLiteral("deesser")) {
      const double intensity =
          number(parameters, "intensity", 45, 0, 100) / 100.0;
      const double maximum =
          number(parameters, "maximum", 60, 0, 100) / 100.0;
      const double focus = number(parameters, "focus", 50, 0, 100) / 100.0;
      filters << QStringLiteral("deesser=i=%1:m=%2:f=%3:s=o")
                     .arg(intensity, 0, 'f', 3)
                     .arg(maximum, 0, 'f', 3)
                     .arg(focus, 0, 'f', 3);
    } else if (effectId == QStringLiteral("noise_gate")) {
      const double thresholdDb =
          number(parameters, "threshold", -36, -36, -6);
      const double threshold = std::pow(10.0, thresholdDb / 20.0);
      const double ratio = number(parameters, "ratio", 4, 1, 20);
      const double attack = number(parameters, "attack", 15, 1, 250);
      const double release = number(parameters, "release", 180, 10, 1000);
      filters << QStringLiteral(
                     "agate=threshold=%1:ratio=%2:attack=%3:release=%4:"
                     "range=0.06125:detection=rms")
                     .arg(threshold, 0, 'f', 6)
                     .arg(ratio, 0, 'f', 2)
                     .arg(attack, 0, 'f', 1)
                     .arg(release, 0, 'f', 1);
    } else if (effectId == QStringLiteral("loudness_normalize")) {
      const double target = number(parameters, "target", -16, -30, -5);
      const double truePeak = number(parameters, "truePeak", -1.5, -9, 0);
      filters << QStringLiteral("loudnorm=I=%1:LRA=7:TP=%2:linear=true")
                     .arg(target, 0, 'f', 1)
                     .arg(truePeak, 0, 'f', 1);
    } else if (effectId == QStringLiteral("chorus")) {
      const double speed = number(parameters, "speed", 0.8, 0.1, 5);
      const double depth = number(parameters, "depth", 2, 0.1, 10);
      filters << QStringLiteral("chorus=0.5:0.9:50:0.4:%1:%2")
                     .arg(speed, 0, 'f', 2)
                     .arg(depth, 0, 'f', 2);
    } else if (effectId == QStringLiteral("flanger")) {
      const double speed = number(parameters, "speed", 0.5, 0.1, 10);
      const double depth = number(parameters, "depth", 2, 0, 10);
      const double feedback = number(parameters, "feedback", 0, -90, 90);
      const double mix = number(parameters, "mix", 70, 0, 100);
      filters << QStringLiteral(
                     "flanger=delay=0:depth=%1:regen=%2:width=%3:speed=%4:"
                     "shape=sinusoidal:interp=linear")
                     .arg(depth, 0, 'f', 2)
                     .arg(feedback, 0, 'f', 1)
                     .arg(mix, 0, 'f', 1)
                     .arg(speed, 0, 'f', 2);
    } else if (effectId == QStringLiteral("stereo_widener") && channels >= 2) {
      const double delay = number(parameters, "delay", 20, 1, 100);
      const double feedback =
          number(parameters, "feedback", 30, 0, 90) / 100.0;
      const double crossfeed =
          number(parameters, "crossfeed", 30, 0, 80) / 100.0;
      const double dryMix = number(parameters, "dryMix", 80, 0, 100) / 100.0;
      filters << QStringLiteral(
                     "stereowiden=delay=%1:feedback=%2:crossfeed=%3:drymix=%4")
                     .arg(delay, 0, 'f', 1)
                     .arg(feedback, 0, 'f', 3)
                     .arg(crossfeed, 0, 'f', 3)
                     .arg(dryMix, 0, 'f', 3);
    }
  }
  return filters.join(',');
}
