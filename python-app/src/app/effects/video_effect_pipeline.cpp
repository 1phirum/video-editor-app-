#include "app/effects/video_effect_pipeline.h"

#include <QStringList>
#include <QtGlobal>
#include <cmath>

namespace {
double number(const QVariantMap &parameters, const QString &key,
              double fallback, double minimum, double maximum) {
  return qBound(minimum, parameters.value(key, fallback).toDouble(), maximum);
}

// The UI has no color parameter type, so keying effects expose a hue slider
// instead and this turns it into the 0xRRGGBB literal FFmpeg expects. The hue
// is taken at full saturation and value, which is what a lit screen looks like.
QString hueToHexColor(double hueDegrees) {
  const double wrapped =
      std::fmod(std::fmod(hueDegrees, 360.0) + 360.0, 360.0) / 60.0;
  const double f = wrapped - std::floor(wrapped);
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  switch (static_cast<int>(std::floor(wrapped)) % 6) {
  case 0:  r = 1.0;     g = f;                    break;
  case 1:  r = 1.0 - f; g = 1.0;                  break;
  case 2:               g = 1.0;      b = f;      break;
  case 3:               g = 1.0 - f;  b = 1.0;    break;
  case 4:  r = f;                     b = 1.0;    break;
  default: r = 1.0;                   b = 1.0 - f; break;
  }
  const int packed = (qRound(r * 255.0) << 16) | (qRound(g * 255.0) << 8) |
                     qRound(b * 255.0);
  return QStringLiteral("0x%1").arg(packed, 6, 16, QLatin1Char('0'));
}
} // namespace

QString VideoEffectPipeline::filters(const QVariantList &effectStack,
                                     qint64 clipStartMs,
                                     qint64 clipDurationMs) {
  QStringList filters;
  for (const auto &value : effectStack) {
    const QVariantMap instance = value.toMap();
    if (!instance.value("enabled", true).toBool())
      continue;
    const QString effectId = instance.value("definitionId").toString();
    const QVariantMap parameters = instance.value("parameters").toMap();

    if (effectId == QStringLiteral("brightness_contrast")) {
      const double brightness =
          number(parameters, "brightness", 0, -100, 100) / 100.0;
      const double contrast =
          1.0 + number(parameters, "contrast", 0, -100, 100) / 100.0;
      const double saturation =
          number(parameters, "saturation", 100, 0, 200) / 100.0;
      filters << QStringLiteral("eq=brightness=%1:contrast=%2:saturation=%3")
                     .arg(brightness, 0, 'f', 4)
                     .arg(contrast, 0, 'f', 4)
                     .arg(saturation, 0, 'f', 4);
    } else if (effectId == QStringLiteral("monochrome")) {
      const double contrast =
          1.0 + number(parameters, "contrast", 0, -100, 100) / 100.0;
      const double brightness =
          number(parameters, "brightness", 0, -100, 100) / 100.0;
      filters << QStringLiteral("hue=s=0");
      if (std::abs(contrast - 1.0) > 0.0001 || std::abs(brightness) > 0.0001)
        filters << QStringLiteral("eq=contrast=%1:brightness=%2")
                       .arg(contrast, 0, 'f', 4)
                       .arg(brightness, 0, 'f', 4);
    } else if (effectId == QStringLiteral("gaussian_blur")) {
      const double amount = number(parameters, "amount", 6, 0, 30);
      if (amount > 0.001)
        filters << QStringLiteral("gblur=sigma=%1").arg(amount, 0, 'f', 2);
    } else if (effectId == QStringLiteral("box_blur")) {
      const double radius = number(parameters, "radius", 4, 0, 30);
      if (radius > 0.001)
        filters << QStringLiteral("boxblur=luma_radius=%1:luma_power=1")
                       .arg(radius, 0, 'f', 1);
    } else if (effectId == QStringLiteral("sharpen")) {
      const double amount = number(parameters, "amount", 0.8, 0, 3);
      if (amount > 0.001)
        filters << QStringLiteral("unsharp=5:5:%1:5:5:0")
                       .arg(amount, 0, 'f', 2);
    } else if (effectId == QStringLiteral("vignette")) {
      const double strength = number(parameters, "strength", 35, 0, 100);
      if (strength > 0.001) {
        const double divisor = 2.0 + 6.0 * strength / 100.0;
        filters << QStringLiteral("vignette=angle=PI/%1:eval=frame")
                       .arg(divisor, 0, 'f', 3);
      }
    } else if (effectId == QStringLiteral("invert")) {
      const double amount = number(parameters, "amount", 100, 0, 100) / 100.0;
      if (amount >= 0.999) {
        filters << QStringLiteral("negate");
      } else if (amount > 0.001) {
        // Blend toward the negative without a split/blend: for a in [0,1] the
        // value a*maxval + (1-2a)*val stays inside [0,maxval], and it carries no
        // comma so the filtergraph splitter leaves it intact (see posterize).
        const double slope = 1.0 - 2.0 * amount;
        QString expr = QStringLiteral("%1*maxval").arg(amount, 0, 'f', 4);
        expr += slope >= 0
                    ? QStringLiteral("+%1*val").arg(slope, 0, 'f', 4)
                    : QStringLiteral("-%1*val").arg(-slope, 0, 'f', 4);
        filters << QStringLiteral("lutrgb=r=%1:g=%1:b=%1").arg(expr);
      }
    } else if (effectId == QStringLiteral("film_grain")) {
      const double amount = number(parameters, "amount", 18, 0, 100);
      if (amount > 0.001)
        filters << QStringLiteral("noise=alls=%1:allf=t+u")
                       .arg(qMax(1.0, amount / 3.0), 0, 'f', 2);
    } else if (effectId == QStringLiteral("color_temperature")) {
      const double temperature =
          number(parameters, "temperature", 6500, 1000, 40000);
      const double mix = number(parameters, "mix", 100, 0, 100) / 100.0;
      filters << QStringLiteral("colortemperature=temperature=%1:mix=%2:pl=0.2")
                     .arg(temperature, 0, 'f', 0)
                     .arg(mix, 0, 'f', 3);
    } else if (effectId == QStringLiteral("pixelate")) {
      const int blockSize = qRound(number(parameters, "blockSize", 16, 2, 128));
      filters << QStringLiteral("pixelize=w=%1:h=%1:mode=avg").arg(blockSize);
    } else if (effectId == QStringLiteral("edge_detect")) {
      const double threshold =
          number(parameters, "threshold", 20, 1, 80) / 100.0;
      filters << QStringLiteral("edgedetect=low=%1:high=%2:mode=colormix")
                     .arg(threshold * 0.4, 0, 'f', 4)
                     .arg(qMin(1.0, threshold), 0, 'f', 4);
    } else if (effectId == QStringLiteral("sepia")) {
      // Lerp the sepia channel matrix toward identity so the strength slider
      // fades the tone in without a second stream to blend against.
      const double a = number(parameters, "intensity", 100, 0, 100) / 100.0;
      const auto mix = [a](double sepiaCoef, double identityCoef) {
        return QString::number(identityCoef * (1.0 - a) + sepiaCoef * a, 'f', 4);
      };
      filters << QStringLiteral("colorchannelmixer=rr=%1:rg=%2:rb=%3:gr=%4:"
                                "gg=%5:gb=%6:br=%7:bg=%8:bb=%9:pc=lum")
                     .arg(mix(0.393, 1.0))
                     .arg(mix(0.769, 0.0))
                     .arg(mix(0.189, 0.0))
                     .arg(mix(0.349, 0.0))
                     .arg(mix(0.686, 1.0))
                     .arg(mix(0.168, 0.0))
                     .arg(mix(0.272, 0.0))
                     .arg(mix(0.534, 0.0))
                     .arg(mix(0.131, 1.0));
    } else if (effectId == QStringLiteral("lens_correction")) {
      const double k1 = number(parameters, "k1", 0, -100, 100) / 100.0;
      const double k2 = number(parameters, "k2", 0, -100, 100) / 100.0;
      if (std::abs(k1) > 0.0001 || std::abs(k2) > 0.0001)
        filters << QStringLiteral("lenscorrection=k1=%1:k2=%2:i=bilinear")
                       .arg(k1, 0, 'f', 4)
                       .arg(k2, 0, 'f', 4);
    } else if (effectId == QStringLiteral("deinterlace")) {
      const bool doubleRate =
          parameters.value("doubleFrameRate", false).toBool();
      const bool interlacedOnly =
          parameters.value("interlacedOnly", false).toBool();
      filters << QStringLiteral("yadif=mode=%1:parity=auto:deint=%2")
                     .arg(doubleRate ? QStringLiteral("send_field")
                                     : QStringLiteral("send_frame"))
                     .arg(interlacedOnly ? QStringLiteral("interlaced")
                                         : QStringLiteral("all"));
    } else if (effectId == QStringLiteral("hue_saturation")) {
      const double hue = number(parameters, "hue", 0, -180, 180);
      const double saturation =
          number(parameters, "saturation", 100, 0, 300) / 100.0;
      const double brightness = number(parameters, "brightness", 0, -3, 3);
      filters << QStringLiteral("hue=h=%1:s=%2:b=%3")
                     .arg(hue, 0, 'f', 3)
                     .arg(saturation, 0, 'f', 4)
                     .arg(brightness, 0, 'f', 3);
    } else if (effectId == QStringLiteral("exposure")) {
      const double exposure = number(parameters, "exposure", 0, -3, 3);
      const double black = number(parameters, "black", 0, -0.5, 0.5);
      if (std::abs(exposure) > 0.0001 || std::abs(black) > 0.0001)
        filters << QStringLiteral("exposure=exposure=%1:black=%2")
                       .arg(exposure, 0, 'f', 4)
                       .arg(black, 0, 'f', 4);
    } else if (effectId == QStringLiteral("levels")) {
      const double inputBlack =
          number(parameters, "inputBlack", 0, 0, 90) / 100.0;
      const double inputWhite =
          number(parameters, "inputWhite", 100, 10, 100) / 100.0;
      const double outputBlack =
          number(parameters, "outputBlack", 0, 0, 90) / 100.0;
      const double outputWhite =
          number(parameters, "outputWhite", 100, 10, 100) / 100.0;
      // colorlevels defaults are imin=0 / imax=1 / omin=0 / omax=1, so the
      // percentages map straight across; imax is nudged above imin to keep the
      // remap from collapsing to a single level.
      const double inMax = qMax(inputBlack + 0.01, inputWhite);
      filters << QStringLiteral("colorlevels=rimin=%1:gimin=%1:bimin=%1:"
                                "rimax=%2:gimax=%2:bimax=%2:"
                                "romin=%3:gomin=%3:bomin=%3:"
                                "romax=%4:gomax=%4:bomax=%4")
                     .arg(inputBlack, 0, 'f', 4)
                     .arg(inMax, 0, 'f', 4)
                     .arg(outputBlack, 0, 'f', 4)
                     .arg(outputWhite, 0, 'f', 4);
    } else if (effectId == QStringLiteral("gamma")) {
      const double gamma = number(parameters, "gamma", 1, 0.1, 4);
      const double weight = number(parameters, "weight", 1, 0, 1);
      filters << QStringLiteral("eq=gamma=%1:gamma_weight=%2")
                     .arg(gamma, 0, 'f', 4)
                     .arg(weight, 0, 'f', 4);
    } else if (effectId == QStringLiteral("color_balance")) {
      const auto shift = [&parameters](const char *key) {
        return number(parameters, QString::fromLatin1(key), 0, -100, 100) /
               100.0;
      };
      const bool preserve =
          parameters.value("preserveLightness", true).toBool();
      filters << QStringLiteral("colorbalance=rs=%1:gs=%2:bs=%3:rm=%4:gm=%5:"
                                "bm=%6:rh=%7:gh=%8:bh=%9:pl=%10")
                     .arg(shift("shadowsRed"), 0, 'f', 4)
                     .arg(shift("shadowsGreen"), 0, 'f', 4)
                     .arg(shift("shadowsBlue"), 0, 'f', 4)
                     .arg(shift("midtonesRed"), 0, 'f', 4)
                     .arg(shift("midtonesGreen"), 0, 'f', 4)
                     .arg(shift("midtonesBlue"), 0, 'f', 4)
                     .arg(shift("highlightsRed"), 0, 'f', 4)
                     .arg(shift("highlightsGreen"), 0, 'f', 4)
                     .arg(shift("highlightsBlue"), 0, 'f', 4)
                     .arg(preserve ? 1 : 0);
    } else if (effectId == QStringLiteral("vibrance")) {
      const double intensity =
          number(parameters, "intensity", 0, -100, 100) / 100.0;
      if (std::abs(intensity) > 0.0001)
        filters << QStringLiteral("vibrance=intensity=%1")
                       .arg(intensity, 0, 'f', 4);
    } else if (effectId == QStringLiteral("tint")) {
      const double hue = number(parameters, "hue", 30, 0, 360);
      const double saturation =
          number(parameters, "saturation", 40, 0, 100) / 100.0;
      const double mix = number(parameters, "mix", 80, 0, 100) / 100.0;
      filters << QStringLiteral("colorize=hue=%1:saturation=%2:mix=%3")
                     .arg(hue, 0, 'f', 2)
                     .arg(saturation, 0, 'f', 4)
                     .arg(mix, 0, 'f', 4);
    } else if (effectId == QStringLiteral("directional_blur")) {
      const double radius = number(parameters, "radius", 8, 0, 40);
      if (radius > 0.001)
        filters << QStringLiteral("dblur=angle=%1:radius=%2")
                       .arg(number(parameters, "angle", 45, 0, 360), 0, 'f', 2)
                       .arg(radius, 0, 'f', 2);
    } else if (effectId == QStringLiteral("motion_blur")) {
      const int frames = qRound(number(parameters, "frames", 4, 2, 16));
      // tmix needs one weight per frame; equal weights give a plain average.
      QStringList weights;
      for (int i = 0; i < frames; ++i)
        weights << QStringLiteral("1");
      filters << QStringLiteral("tmix=frames=%1:weights=%2")
                     .arg(frames)
                     .arg(weights.join(' '));
    } else if (effectId == QStringLiteral("smart_blur")) {
      filters << QStringLiteral("smartblur=lr=%1:ls=%2:lt=%3")
                     .arg(number(parameters, "radius", 2, 0.1, 5), 0, 'f', 2)
                     .arg(number(parameters, "strength", 0.8, -1, 1), 0, 'f', 3)
                     .arg(qRound(number(parameters, "threshold", 0, -30, 30)));
    } else if (effectId == QStringLiteral("unsharp_mask")) {
      // unsharp only accepts an odd matrix size in [3, 23]; radius=1 would
      // otherwise emit size=1 and make ffmpeg reject the whole filtergraph.
      int size = qRound(number(parameters, "radius", 2, 1, 11));
      if (size % 2 == 0)
        ++size;
      size = qBound(3, size, 23);
      filters << QStringLiteral("unsharp=lx=%1:ly=%1:la=%2:cx=%1:cy=%1:ca=%3")
                     .arg(size)
                     .arg(number(parameters, "amount", 1.2, -2, 5), 0, 'f', 3)
                     .arg(number(parameters, "colorAmount", 0, -2, 5), 0, 'f',
                          3);
    } else if (effectId == QStringLiteral("video_denoise")) {
      const double luma = number(parameters, "luma", 4, 0, 30);
      const double chroma = number(parameters, "chroma", 3, 0, 30);
      const double temporal = number(parameters, "temporal", 6, 0, 30);
      filters << QStringLiteral("hqdn3d=luma_spatial=%1:chroma_spatial=%2:"
                                "luma_tmp=%3:chroma_tmp=%4")
                     .arg(luma, 0, 'f', 2)
                     .arg(chroma, 0, 'f', 2)
                     .arg(temporal, 0, 'f', 2)
                     .arg(temporal * 0.75, 0, 'f', 2);
    } else if (effectId == QStringLiteral("deband")) {
      const double threshold =
          number(parameters, "threshold", 4, 0.1, 50) / 100.0;
      filters << QStringLiteral(
                     "deband=1thr=%1:2thr=%1:3thr=%1:4thr=%1:range=%2")
                     .arg(threshold, 0, 'f', 5)
                     .arg(qRound(number(parameters, "range", 16, 1, 64)));
    } else if (effectId == QStringLiteral("deflicker")) {
      filters << QStringLiteral("deflicker=size=%1:mode=am")
                     .arg(qRound(number(parameters, "size", 5, 2, 129)));
    } else if (effectId == QStringLiteral("posterize")) {
      const int levels = qRound(number(parameters, "levels", 8, 2, 32));
      // Quantize with an expression that stays comma-free: filter option values
      // are separated from the rest of the chain by commas, so anything like
      // if(gt(val,x),..) would be parsed as a new filter.
      const QString step =
          QStringLiteral("trunc(val/255*%1+0.5)*255/%1").arg(levels - 1);
      filters << QStringLiteral("lutrgb=r=%1:g=%1:b=%1").arg(step);
    } else if (effectId == QStringLiteral("emboss")) {
      const double strength = number(parameters, "strength", 1, 0.2, 3);
      const auto weight = [strength](double factor) {
        return QString::number(factor * strength, 'f', 2);
      };
      // Plane 0 only, so the relief is applied to luma and the original chroma
      // rides through untouched. yuva420p keeps any existing alpha.
      filters << QStringLiteral("format=yuva420p")
              << QStringLiteral("convolution=0m='%1 %2 0 %2 1 %3 0 %3 %4':"
                                "0rdiv=1:0bias=0")
                     .arg(weight(-2.0), weight(-1.0), weight(1.0),
                          weight(2.0));
    } else if (effectId == QStringLiteral("chromatic_aberration")) {
      const int horizontal =
          qRound(number(parameters, "horizontal", 4, -30, 30));
      const int vertical = qRound(number(parameters, "vertical", 0, -30, 30));
      if (horizontal != 0 || vertical != 0)
        filters << QStringLiteral("rgbashift=rh=%1:rv=%2:bh=%3:bv=%4:edge=smear")
                       .arg(horizontal)
                       .arg(vertical)
                       .arg(-horizontal)
                       .arg(-vertical);
    } else if (effectId == QStringLiteral("chroma_key")) {
      const double hue = number(parameters, "keyHue", 120, 0, 360);
      const double similarity =
          number(parameters, "similarity", 15, 1, 60) / 100.0;
      const double blend = number(parameters, "blend", 10, 0, 60) / 100.0;
      filters << QStringLiteral("chromakey=color=%1:similarity=%2:blend=%3")
                     .arg(hueToHexColor(hue))
                     .arg(similarity, 0, 'f', 4)
                     .arg(blend, 0, 'f', 4);
      if (parameters.value("suppressSpill", true).toBool()) {
        // despill only knows green and blue screens; pick whichever the keyed
        // hue is nearer to.
        const bool blueScreen = hue > 180.0 && hue < 300.0;
        filters << QStringLiteral("despill=type=%1:mix=%2:expand=%3")
                       .arg(blueScreen ? QStringLiteral("blue")
                                       : QStringLiteral("green"))
                       .arg(0.5, 0, 'f', 3)
                       .arg(0.1, 0, 'f', 3);
      }
    } else if (effectId == QStringLiteral("luma_key")) {
      filters << QStringLiteral("lumakey=threshold=%1:tolerance=%2:softness=%3")
                     .arg(number(parameters, "threshold", 10, 0, 100) / 100.0,
                          0, 'f', 4)
                     .arg(number(parameters, "tolerance", 20, 0, 100) / 100.0,
                          0, 'f', 4)
                     .arg(number(parameters, "softness", 10, 0, 100) / 100.0, 0,
                          'f', 4);
    } else if (effectId == QStringLiteral("despill")) {
      filters << QStringLiteral("despill=type=%1:mix=%2:expand=%3")
                     .arg(parameters.value("blueScreen", false).toBool()
                              ? QStringLiteral("blue")
                              : QStringLiteral("green"))
                     .arg(number(parameters, "mix", 50, 0, 100) / 100.0, 0, 'f',
                          4)
                     .arg(number(parameters, "expand", 10, 0, 100) / 100.0, 0,
                          'f', 4);
    } else if (effectId == QStringLiteral("color_splash")) {
      // Keep pixels near the chosen hue and grey out the rest.
      const double hue = number(parameters, "keyHue", 0, 0, 360);
      const double similarity =
          number(parameters, "similarity", 25, 1, 100) / 100.0;
      const double blend = number(parameters, "blend", 10, 0, 100) / 100.0;
      filters << QStringLiteral("chromahold=color=%1:similarity=%2:blend=%3")
                     .arg(hueToHexColor(hue))
                     .arg(similarity, 0, 'f', 4)
                     .arg(blend, 0, 'f', 4);
    } else if (effectId == QStringLiteral("teal_orange")) {
      // Push shadows toward teal and highlights toward orange, then lift
      // saturation a touch to sell the cinematic contrast.
      const double i = number(parameters, "intensity", 60, 0, 100) / 100.0;
      const double shadow = 0.4 * i;
      const double highlight = 0.4 * i;
      filters << QStringLiteral("colorbalance=rs=%1:bs=%2:rh=%3:bh=%4:pl=1")
                     .arg(-shadow, 0, 'f', 4)
                     .arg(shadow, 0, 'f', 4)
                     .arg(highlight, 0, 'f', 4)
                     .arg(-highlight, 0, 'f', 4);
      filters << QStringLiteral("eq=saturation=%1").arg(1.0 + 0.15 * i, 0, 'f', 4);
    } else if (effectId == QStringLiteral("faded_film")) {
      // Raise the black point and lower the white point (space-separated curve
      // points carry no comma), then mute the color.
      const double f = number(parameters, "fade", 50, 0, 100) / 100.0;
      const double lift = 0.12 * f;
      const double top = 1.0 - 0.06 * f;
      filters << QStringLiteral("curves=all=0/%1 0.5/0.5 1/%2")
                     .arg(lift, 0, 'f', 4)
                     .arg(top, 0, 'f', 4);
      filters << QStringLiteral("eq=saturation=%1").arg(1.0 - 0.25 * f, 0, 'f', 4);
    } else if (effectId == QStringLiteral("vivid_pop")) {
      const double vibrance = number(parameters, "vibrance", 50, 0, 100) / 100.0;
      const double clarity = number(parameters, "clarity", 40, 0, 100) / 100.0;
      if (vibrance > 0.001)
        filters << QStringLiteral("eq=saturation=%1:contrast=%2")
                       .arg(1.0 + 0.6 * vibrance, 0, 'f', 4)
                       .arg(1.0 + 0.2 * vibrance, 0, 'f', 4);
      if (clarity > 0.001)
        filters << QStringLiteral("unsharp=lx=5:ly=5:la=%1")
                       .arg(1.2 * clarity, 0, 'f', 3);
    } else if (effectId == QStringLiteral("vintage")) {
      const double grain = number(parameters, "grain", 20, 0, 100);
      filters << QStringLiteral("curves=preset=vintage");
      if (grain > 0.001)
        filters << QStringLiteral("noise=alls=%1:allf=t")
                       .arg(qMax(1.0, grain / 3.0), 0, 'f', 2);
    } else if (effectId == QStringLiteral("fisheye")) {
      // Negative lens coefficients bulge the frame outward (barrel), the
      // creative inverse of the lens_correction effect.
      const double amount = number(parameters, "amount", 50, 0, 100) / 100.0;
      if (amount > 0.001)
        filters << QStringLiteral(
                       "lenscorrection=cx=0.5:cy=0.5:k1=%1:k2=%2:i=bilinear")
                       .arg(-0.3 * amount, 0, 'f', 4)
                       .arg(-0.05 * amount, 0, 'f', 4);
    } else if (effectId.startsWith(QStringLiteral("transition_"))) {
      // The clip-edge fades. Everything here is in sequence seconds because
      // that is the clock this chain runs on (see the header); with no clip
      // bounds there is no edge to fade, so the effect is simply not emitted.
      if (clipDurationMs <= 0)
        continue;
      const double clipStart = static_cast<double>(clipStartMs) / 1000.0;
      const double clipEnd =
          clipStart + static_cast<double>(clipDurationMs) / 1000.0;
      // Capped at half the clip: a 4 s fade on each end of a 3 s clip would
      // otherwise cross over and dip the middle of the shot to black, which
      // looks like a bug rather than like a long fade.
      double span = number(parameters, "duration", 1, 0.1, 30);
      span = qMin(span, static_cast<double>(clipDurationMs) / 2000.0);
      if (span <= 0.01)
        continue;
      const bool white = effectId.contains(QStringLiteral("white"));
      // Transparency instead of black is what an upper track needs: fading to
      // black there would black out the picture underneath instead of revealing
      // it. It needs an alpha channel to fade, hence the format ahead of it -
      // the clip is decoded to yuv, which has none.
      const bool transparent =
          !white && parameters.value("transparent", false).toBool();
      const QString colorOption = white ? QStringLiteral(":color=white")
                                        : transparent
                                              ? QStringLiteral(":alpha=1")
                                              : QString();
      if (transparent)
        filters << QStringLiteral("format=rgba");
      const bool fadeIn = effectId.endsWith(QStringLiteral("_in")) ||
                          effectId.endsWith(QStringLiteral("_both"));
      const bool fadeOut = effectId.endsWith(QStringLiteral("_out")) ||
                           effectId.endsWith(QStringLiteral("_both"));
      if (fadeIn)
        filters << QStringLiteral("fade=t=in:st=%1:d=%2%3")
                       .arg(clipStart, 0, 'f', 3)
                       .arg(span, 0, 'f', 3)
                       .arg(colorOption);
      if (fadeOut)
        filters << QStringLiteral("fade=t=out:st=%1:d=%2%3")
                       .arg(clipEnd - span, 0, 'f', 3)
                       .arg(span, 0, 'f', 3)
                       .arg(colorOption);
    }
  }
  return filters.join(',');
}
