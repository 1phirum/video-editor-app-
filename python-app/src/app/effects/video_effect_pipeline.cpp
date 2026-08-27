#include "app/effects/video_effect_pipeline.h"

#include <QStringList>
#include <QtGlobal>

namespace {
double number(const QVariantMap &parameters, const QString &key,
              double fallback, double minimum, double maximum) {
  return qBound(minimum, parameters.value(key, fallback).toDouble(), maximum);
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
    }
  }
  return filters.join(',');
}
