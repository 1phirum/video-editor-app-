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

QString VideoEffectPipeline::filters(const QVariantList &effectStack) {
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
      filters << QStringLiteral("hue=s=0");
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
      filters << QStringLiteral("negate");
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
      filters << QStringLiteral(
          "colorchannelmixer=rr=.393:rg=.769:rb=.189:gr=.349:gg=.686:"
          "gb=.168:br=.272:bg=.534:bb=.131:pc=lum:pa=.25");
    } else if (effectId == QStringLiteral("lens_correction")) {
      const double k1 = number(parameters, "k1", 0, -100, 100) / 100.0;
      const double k2 = number(parameters, "k2", 0, -100, 100) / 100.0;
      if (std::abs(k1) > 0.0001 || std::abs(k2) > 0.0001)
        filters << QStringLiteral("lenscorrection=k1=%1:k2=%2:i=bilinear")
                       .arg(k1, 0, 'f', 4)
                       .arg(k2, 0, 'f', 4);
    } else if (effectId == QStringLiteral("deinterlace")) {
      filters << QStringLiteral("yadif=mode=send_frame:parity=auto:deint=all");
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
      // unsharp only accepts odd matrix sizes.
      int size = qRound(number(parameters, "radius", 2, 1, 11));
      if (size % 2 == 0)
        ++size;
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
    }
  }
  return filters.join(',');
}
