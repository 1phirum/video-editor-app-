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
    } else if (effectId == QStringLiteral("parametric_eq")) {
      filters << QStringLiteral("equalizer=f=%1:t=q:w=%2:g=%3")
                     .arg(number(parameters, "frequency", 1000, 20, 20000), 0,
                          'f', 1)
                     .arg(number(parameters, "q", 1, 0.1, 10), 0, 'f', 3)
                     .arg(number(parameters, "gain", 0, -24, 24), 0, 'f', 2);
    } else if (effectId == QStringLiteral("sub_boost")) {
      filters << QStringLiteral("asubboost=dry=1:wet=%1:boost=%2:cutoff=%3:"
                                "decay=0.7:feedback=0.5:slope=0.5:delay=20")
                     .arg(number(parameters, "amount", 60, 0, 100) / 100.0, 0,
                          'f', 3)
                     .arg(number(parameters, "boost", 2, 1, 12), 0, 'f', 2)
                     .arg(number(parameters, "cutoff", 100, 50, 900), 0, 'f',
                          1);
    } else if (effectId == QStringLiteral("virtual_bass")) {
      filters << QStringLiteral("virtualbass=cutoff=%1:strength=%2")
                     .arg(number(parameters, "cutoff", 250, 100, 500), 0, 'f',
                          1)
                     .arg(number(parameters, "strength", 2, 0.5, 3), 0, 'f', 3);
    } else if (effectId == QStringLiteral("dynamic_normalize")) {
      // dynaudnorm's Gaussian window has to be an odd number of frames.
      int window = qRound(number(parameters, "filterSize", 31, 3, 301));
      if (window % 2 == 0)
        ++window;
      filters << QStringLiteral("dynaudnorm=f=%1:g=%2:p=%3:m=%4")
                     .arg(qRound(number(parameters, "frameLength", 500, 10,
                                        8000)))
                     .arg(window)
                     .arg(number(parameters, "peak", 95, 10, 100) / 100.0, 0,
                          'f', 3)
                     .arg(number(parameters, "maxGain", 10, 1, 100), 0, 'f', 2);
    } else if (effectId == QStringLiteral("speech_leveler")) {
      filters << QStringLiteral("speechnorm=p=%1:e=%2:c=%3:t=%4")
                     .arg(number(parameters, "peak", 95, 10, 100) / 100.0, 0,
                          'f', 3)
                     .arg(number(parameters, "expansion", 2, 1, 50), 0, 'f', 2)
                     .arg(number(parameters, "compression", 2, 1, 50), 0, 'f',
                          2)
                     .arg(number(parameters, "threshold", 0, 0, 100) / 100.0, 0,
                          'f', 4);
    } else if (effectId == QStringLiteral("soft_clip")) {
      filters << QStringLiteral(
                     "asoftclip=type=tanh:threshold=%1:output=%2:param=%3")
                     .arg(number(parameters, "threshold", 90, 1, 100) / 100.0, 0,
                          'f', 4)
                     .arg(number(parameters, "output", 100, 10, 400) / 100.0, 0,
                          'f', 4)
                     .arg(number(parameters, "amount", 1, 0.05, 3), 0, 'f', 3);
    } else if (effectId == QStringLiteral("audio_contrast")) {
      filters << QStringLiteral("acontrast=contrast=%1")
                     .arg(number(parameters, "contrast", 33, 0, 100), 0, 'f', 2);
    } else if (effectId == QStringLiteral("declick")) {
      filters << QStringLiteral("adeclick=window=%1:overlap=75:arorder=2:"
                                "threshold=%2:burst=%3:method=add")
                     .arg(number(parameters, "window", 55, 10, 100), 0, 'f', 1)
                     .arg(number(parameters, "threshold", 2, 1, 100), 0, 'f', 2)
                     .arg(number(parameters, "burst", 2, 0, 10), 0, 'f', 2);
    } else if (effectId == QStringLiteral("declip")) {
      filters << QStringLiteral("adeclip=window=%1:overlap=75:arorder=%2:"
                                "threshold=%3:hsize=1000:method=add")
                     .arg(number(parameters, "window", 55, 10, 100), 0, 'f', 1)
                     .arg(number(parameters, "order", 8, 0, 25), 0, 'f', 1)
                     .arg(number(parameters, "threshold", 10, 1, 100), 0, 'f',
                          2);
    } else if (effectId == QStringLiteral("tremolo")) {
      filters << QStringLiteral("tremolo=f=%1:d=%2")
                     .arg(number(parameters, "rate", 5, 0.1, 20), 0, 'f', 3)
                     .arg(number(parameters, "depth", 50, 0, 100) / 100.0, 0,
                          'f', 3);
    } else if (effectId == QStringLiteral("vibrato")) {
      filters << QStringLiteral("vibrato=f=%1:d=%2")
                     .arg(number(parameters, "rate", 5, 0.1, 20), 0, 'f', 3)
                     .arg(number(parameters, "depth", 50, 0, 100) / 100.0, 0,
                          'f', 3);
    } else if (effectId == QStringLiteral("phaser")) {
      filters << QStringLiteral("aphaser=in_gain=0.4:out_gain=0.74:delay=%1:"
                                "decay=%2:speed=%3:type=t")
                     .arg(number(parameters, "delay", 3, 0.1, 5), 0, 'f', 3)
                     .arg(number(parameters, "decay", 40, 0, 99) / 100.0, 0, 'f',
                          3)
                     .arg(number(parameters, "speed", 0.5, 0.1, 2), 0, 'f', 3);
    } else if (effectId == QStringLiteral("auto_pan") && channels >= 2) {
      filters << QStringLiteral("apulsator=mode=sine:amount=%1:width=%2:"
                                "offset_l=0:offset_r=0.5:timing=hz:hz=%3")
                     .arg(number(parameters, "amount", 100, 0, 100) / 100.0, 0,
                          'f', 3)
                     .arg(number(parameters, "width", 100, 0, 200) / 100.0, 0,
                          'f', 3)
                     .arg(number(parameters, "rate", 2, 0.05, 20), 0, 'f', 3);
    } else if (effectId == QStringLiteral("extra_stereo") && channels >= 2) {
      filters << QStringLiteral("extrastereo=m=%1:c=1")
                     .arg(number(parameters, "difference", 250, -100, 500) /
                              100.0,
                          0, 'f', 3);
    } else if (effectId == QStringLiteral("haas_stereo") && channels >= 2) {
      filters << QStringLiteral("haas=level_in=1:level_out=1:side_gain=%1:"
                                "middle_source=mid:left_delay=%2:right_delay=%3")
                     .arg(number(parameters, "sideGain", 100, 10, 400) / 100.0,
                          0, 'f', 4)
                     .arg(number(parameters, "leftDelay", 2.05, 0, 40), 0, 'f',
                          3)
                     .arg(number(parameters, "rightDelay", 2.12, 0, 40), 0, 'f',
                          3);
    } else if (effectId == QStringLiteral("headphone_widen") && channels >= 2) {
      // earwax is tuned for 44.1 kHz material; it still widens at other rates,
      // just with a slightly different crossfeed curve.
      filters << QStringLiteral("earwax");
    } else if (effectId == QStringLiteral("pitch_shift")) {
      const double semitones = number(parameters, "semitones", 0, -12, 12);
      if (std::abs(semitones) > 0.001)
        filters << QStringLiteral("rubberband=pitch=%1")
                       .arg(std::pow(2.0, semitones / 12.0), 0, 'f', 6);
    } else if (effectId == QStringLiteral("bit_crusher")) {
      filters << QStringLiteral("acrusher=level_in=1:level_out=1:bits=%1:"
                                "mode=log:dc=1:aa=0.5:samples=%2:mix=%3")
                     .arg(number(parameters, "bits", 8, 1, 16), 0, 'f', 2)
                     .arg(qRound(number(parameters, "sampleReduction", 1, 1,
                                        64)))
                     .arg(number(parameters, "mix", 50, 0, 100) / 100.0, 0, 'f',
                          3);
    } else if (effectId == QStringLiteral("exciter")) {
      filters << QStringLiteral("aexciter=level_in=1:level_out=1:amount=%1:"
                                "drive=%2:blend=%3:freq=%4")
                     .arg(number(parameters, "amount", 1, 0, 20), 0, 'f', 3)
                     .arg(number(parameters, "drive", 8.5, 0.1, 10), 0, 'f', 3)
                     .arg(number(parameters, "blend", 0, -10, 10), 0, 'f', 3)
                     .arg(number(parameters, "frequency", 7500, 2000, 12000), 0,
                          'f', 1);
    } else if (effectId == QStringLiteral("clarity")) {
      filters << QStringLiteral("crystalizer=i=%1:c=%2")
                     .arg(number(parameters, "intensity", 2, -10, 10), 0, 'f', 3)
                     .arg(parameters.value("clip", true).toBool() ? 1 : 0);
    } else if (effectId == QStringLiteral("reverb")) {
      // There is no convolution reverb in the linear chain, so the room is
      // approximated with four decaying taps of aecho. Longer rooms push the
      // taps further apart; damping steepens the decay between them.
      const double roomSize = number(parameters, "roomSize", 45, 5, 100);
      const double damping = number(parameters, "damping", 50, 10, 90) / 100.0;
      const double mix = number(parameters, "mix", 35, 0, 100) / 100.0;
      const double spread = 20.0 + roomSize * 1.8;
      static const double taps[4] = {0.37, 0.61, 0.83, 1.0};
      QStringList delays;
      QStringList decays;
      for (int i = 0; i < 4; ++i) {
        delays << QString::number(qMax(1, qRound(spread * taps[i])));
        const double decay =
            qBound(0.01, mix * 0.7 * std::pow(1.0 - damping * 0.85, i), 0.9);
        decays << QString::number(decay, 'f', 4);
      }
      if (mix > 0.001)
        filters << QStringLiteral("aecho=0.8:0.9:%1:%2")
                       .arg(delays.join('|'), decays.join('|'));
    }
  }
  return filters.join(',');
}
