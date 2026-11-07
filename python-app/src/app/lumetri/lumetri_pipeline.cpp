#include "app/lumetri/lumetri_pipeline.h"

#include <QFileInfo>
#include <QStringList>
#include <QtGlobal>
#include <cmath>

namespace {
QString escapedFilterPath(QString path) {
  path.replace('\\', '/');
  path.replace(':', "\\:");
  path.replace('\'', "\\'");
  return path;
}

bool hdrSource(const QString &space) {
  return space == QStringLiteral("Rec. 2100 HLG") ||
         space == QStringLiteral("Rec. 2100 PQ");
}

double clamp01(double value) { return qBound(0.0, value, 1.0); }

QString curvePoints(double shadows, double midtones, double highlights) {
  return QStringLiteral("0/%1 0.25/%2 0.5/%3 0.75/%4 1/%5")
      .arg(clamp01(shadows / 400.0), 0, 'f', 4)
      .arg(clamp01(0.25 + shadows / 400.0), 0, 'f', 4)
      .arg(clamp01(0.5 + midtones / 400.0), 0, 'f', 4)
      .arg(clamp01(0.75 + highlights / 400.0), 0, 'f', 4)
      .arg(clamp01(1.0 + highlights / 400.0), 0, 'f', 4);
}

QString curvePoints(const QVariantList &points) {
  QStringList serialized;
  for (const auto &value : points) {
    const QVariantMap point = value.toMap();
    if (!point.contains("x") || !point.contains("y"))
      continue;
    serialized << QStringLiteral("%1/%2")
                      .arg(clamp01(point.value("x").toDouble()), 0, 'f', 5)
                      .arg(clamp01(point.value("y").toDouble()), 0, 'f', 5);
  }
  return serialized.size() >= 2 ? serialized.join(' ') : QString();
}

double flatCurveAdjustment(const QVariantList &points, double range) {
  if (points.size() < 2)
    return 0.0;
  double total = 0.0;
  int count = 0;
  for (const auto &value : points) {
    const QVariantMap point = value.toMap();
    if (!point.contains("y"))
      continue;
    total += clamp01(point.value("y").toDouble()) - 0.5;
    ++count;
  }
  return count > 0 ? qBound(-range, total / count * range * 2.0, range) : 0.0;
}

QString colorsForHue(double center, double width) {
  if (width >= 170.0)
    return QStringLiteral("a");
  struct HueBand {
    double center;
    QChar flag;
  };
  static const HueBand bands[]{{0.0, 'r'},   {60.0, 'y'},  {120.0, 'g'},
                               {180.0, 'c'}, {240.0, 'b'}, {300.0, 'm'}};
  QString flags;
  const double radius = qMax(15.0, width / 2.0);
  double nearestDistance = 361.0;
  QChar nearest = 'r';
  for (const auto &band : bands) {
    double distance = std::abs(center - band.center);
    distance = qMin(distance, 360.0 - distance);
    if (distance < nearestDistance) {
      nearestDistance = distance;
      nearest = band.flag;
    }
    if (distance <= radius + 30.0) {
      if (!flags.isEmpty())
        flags += '+';
      flags += band.flag;
    }
  }
  return flags.isEmpty() ? QString(nearest) : flags;
}

QString toneMapFor(const QString &space, const QVariantMap &project) {
  if (!project.value("autoToneMapMedia", true).toBool() || !hdrSource(space) ||
      project.value("workingColorSpace", "Rec. 709").toString() !=
          QStringLiteral("Rec. 709"))
    return {};
  const QString transfer = space == QStringLiteral("Rec. 2100 PQ")
                               ? QStringLiteral("smpte2084")
                               : QStringLiteral("arib-std-b67");
  return QStringLiteral("zscale=pin=bt2020:tin=%1:min=2020_ncl:transfer=linear:"
                        "primaries=bt2020,tonemap=tonemap=hable:desat=2,"
                        "zscale=primaries=bt709:transfer=bt709:matrix=bt709")
      .arg(transfer);
}
} // namespace

QString LumetriPipeline::filterForClip(const QVariantMap &clip,
                                       const QVariantMap &media,
                                       const QVariantMap &project) {
  const QVariantMap color = clip.value("lumetri").toMap();
  const QVariantMap source = media.value("color").toMap();
  QStringList filters;

  const QString sourceSpace =
      source.value("overrideMediaColorSpace").toString();
  const QString toneMap = toneMapFor(sourceSpace, project);
  if (!toneMap.isEmpty())
    filters << toneMap;

  const QString lut = source.value("inputLut").toString().trimmed();
  if (!lut.isEmpty() && lut.compare("None", Qt::CaseInsensitive) != 0 &&
      QFileInfo::exists(lut)) {
    const QString interpolation =
        project.value("lutInterpolation", "Tetrahedral").toString() ==
                QStringLiteral("Trilinear")
            ? QStringLiteral("trilinear")
            : QStringLiteral("tetrahedral");
    filters << QStringLiteral("lut3d=file='%1':interp=%2")
                   .arg(escapedFilterPath(lut), interpolation);
  }

  if (color.isEmpty())
    return filters.join(',');

  const bool basicEnabled = color.value("basicEnabled", true).toBool();
  const bool creativeEnabled = color.value("creativeEnabled", true).toBool();
  const bool curvesEnabled = color.value("curvesEnabled", true).toBool();
  const bool rgbCurvesEnabled =
      curvesEnabled && color.value("rgbCurvesEnabled", true).toBool();
  const bool hueVsHueEnabled =
      curvesEnabled && color.value("hueVsHueEnabled", true).toBool();
  const bool hueVsSatEnabled =
      curvesEnabled && color.value("hueVsSatEnabled", true).toBool();
  const bool hueVsLumaEnabled =
      curvesEnabled && color.value("hueVsLumaEnabled", true).toBool();
  const bool lumaVsSatEnabled =
      curvesEnabled && color.value("lumaVsSatEnabled", true).toBool();
  const bool satVsSatEnabled =
      curvesEnabled && color.value("satVsSatEnabled", true).toBool();
  const bool colorWheelsEnabled =
      color.value("colorWheelsEnabled", true).toBool();
  const bool hslSecondaryEnabled =
      color.value("hslSecondaryEnabled", true).toBool();
  const bool vignetteEnabled = color.value("vignetteEnabled", true).toBool();

  const double exposure = basicEnabled
                              ? qBound(-5.0, color.value("exposure").toDouble(),
                                       5.0)
                              : 0.0;
  const double contrast =
      basicEnabled
          ? qBound(-100.0, color.value("contrast").toDouble(), 100.0)
          : 0.0;
  const double shadows =
      basicEnabled
          ? qBound(-100.0, color.value("shadows").toDouble(), 100.0)
          : 0.0;
  const double highlights =
      basicEnabled
          ? qBound(-100.0, color.value("highlights").toDouble(), 100.0)
          : 0.0;
  const double whites =
      basicEnabled
          ? qBound(-100.0, color.value("whites").toDouble(), 100.0)
          : 0.0;
  const double blacks =
      basicEnabled
          ? qBound(-100.0, color.value("blacks").toDouble(), 100.0)
          : 0.0;
  const double saturation =
      basicEnabled
          ? qBound(0.0, color.value("saturation", 100.0).toDouble(), 200.0)
          : 100.0;
  const double vibrance =
      creativeEnabled
          ? qBound(-100.0, color.value("vibrance").toDouble(), 100.0)
          : 0.0;
  const double fade = creativeEnabled
                          ? qBound(0.0, color.value("fade").toDouble(), 100.0)
                          : 0.0;
  const double hdrSpecular =
      basicEnabled
          ? qBound(-100.0, color.value("hdrSpecular").toDouble(), 100.0)
          : 0.0;
  const double creativeSaturation =
      creativeEnabled
          ? qBound(0.0, color.value("creativeSaturation", 100.0).toDouble(),
                   200.0)
          : 100.0;
  const double hueVsSat = qBound(
      -100.0,
      hueVsSatEnabled
          ? color.value("hueVsSat").toDouble() +
                flatCurveAdjustment(color.value("hueVsSatPoints").toList(),
                                    100.0)
          : 0.0,
      100.0);
  const double lumaVsSat = qBound(
      -100.0,
      lumaVsSatEnabled
          ? color.value("lumaVsSat").toDouble() +
                flatCurveAdjustment(color.value("lumaVsSatPoints").toList(),
                                    100.0)
          : 0.0,
      100.0);
  const double satVsSat = qBound(
      -100.0,
      satVsSatEnabled
          ? color.value("satVsSat").toDouble() +
                flatCurveAdjustment(color.value("satVsSatPoints").toList(),
                                    100.0)
          : 0.0,
      100.0);
  const double shadowLuma =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("shadowLuma").toDouble(), 100.0)
          : 0.0;
  const double midtoneLuma =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("midtoneLuma").toDouble(), 100.0)
          : 0.0;
  const double highlightLuma =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("highlightLuma").toDouble(), 100.0)
          : 0.0;

  const double brightness = qBound(
      -1.0,
      exposure * 0.08 + whites * 0.0012 + blacks * 0.0008 + fade * 0.001 +
          hdrSpecular * 0.0008 + midtoneLuma * 0.001 + highlightLuma * 0.0007,
      1.0);
  const double contrastFactor =
      qBound(0.1, (1.0 + contrast / 100.0) * (1.0 - fade / 180.0), 3.0);
  const double gamma = qBound(0.1,
                              1.0 + shadows * 0.004 - highlights * 0.003 +
                                  shadowLuma * 0.003 - highlightLuma * 0.0015,
                              3.0);
  const double saturationFactor =
      qBound(0.0,
             saturation / 100.0 * (1.0 + vibrance / 200.0) *
                 (creativeSaturation / 100.0) *
                 (1.0 + (hueVsSat + lumaVsSat + satVsSat) / 600.0),
             3.0);
  if (std::abs(brightness) > 0.0001 ||
      std::abs(contrastFactor - 1.0) > 0.0001 ||
      std::abs(gamma - 1.0) > 0.0001 ||
      std::abs(saturationFactor - 1.0) > 0.0001)
    filters << QStringLiteral(
                   "eq=brightness=%1:contrast=%2:gamma=%3:saturation=%4")
                   .arg(brightness, 0, 'f', 4)
                   .arg(contrastFactor, 0, 'f', 4)
                   .arg(gamma, 0, 'f', 4)
                   .arg(saturationFactor, 0, 'f', 4);

  const double temperature =
      basicEnabled
          ? qBound(-100.0, color.value("temperature").toDouble(), 100.0)
          : 0.0;
  const double tint =
      basicEnabled ? qBound(-100.0, color.value("tint").toDouble(), 100.0)
                   : 0.0;
  const double shadowTint =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("shadowTint").toDouble(), 100.0)
          : 0.0;
  const double midtoneTint =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("midtoneTint").toDouble(), 100.0)
          : 0.0;
  const double highlightTint =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("highlightTint").toDouble(), 100.0)
          : 0.0;
  const double creativeShadowTint =
      creativeEnabled
          ? qBound(-100.0, color.value("creativeShadowTint").toDouble(), 100.0)
          : 0.0;
  const double creativeHighlightTint =
      creativeEnabled
          ? qBound(-100.0, color.value("creativeHighlightTint").toDouble(),
                   100.0)
          : 0.0;
  const double tintBalance =
      creativeEnabled
          ? qBound(-100.0, color.value("tintBalance").toDouble(), 100.0)
          : 0.0;
  const double shadowWheelX =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("shadowWheelX").toDouble(), 100.0)
          : 0.0;
  const double shadowWheelY =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("shadowWheelY").toDouble(), 100.0)
          : 0.0;
  const double midtoneWheelX =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("midtoneWheelX").toDouble(), 100.0)
          : 0.0;
  const double midtoneWheelY =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("midtoneWheelY").toDouble(), 100.0)
          : 0.0;
  const double highlightWheelX =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("highlightWheelX").toDouble(), 100.0)
          : 0.0;
  const double highlightWheelY =
      colorWheelsEnabled
          ? qBound(-100.0, color.value("highlightWheelY").toDouble(), 100.0)
          : 0.0;
  const double lookIntensity =
      creativeEnabled
          ? qBound(0.0, color.value("lookIntensity", 100.0).toDouble(), 200.0) /
                100.0
          : 0.0;
  const QString look =
      creativeEnabled ? color.value("look", "None").toString()
                      : QStringLiteral("None");
  double red = temperature / 300.0;
  double blue = -temperature / 300.0;
  double greenMid = tint / 300.0;
  if (look == "Warm") {
    red += 0.08 * lookIntensity;
    blue -= 0.06 * lookIntensity;
  }
  if (look == "Cool") {
    red -= 0.06 * lookIntensity;
    blue += 0.08 * lookIntensity;
  }
  const double rs = red + shadowWheelX / 300.0;
  const double bs = blue - shadowWheelX / 300.0;
  const double gs = (shadowTint + creativeShadowTint - shadowWheelY) / 300.0;
  const double rm = midtoneWheelX / 300.0;
  const double bm = -midtoneWheelX / 300.0;
  const double gm =
      greenMid + (midtoneTint - midtoneWheelY + tintBalance * 0.5) / 300.0;
  const double rh = highlightWheelX / 300.0;
  const double bh = -highlightWheelX / 300.0;
  const double gh = (highlightTint + creativeHighlightTint - highlightWheelY -
                     tintBalance * 0.5) /
                    300.0;
  if (std::abs(rs) > 0.0001 || std::abs(gs) > 0.0001 || std::abs(bs) > 0.0001 ||
      std::abs(rm) > 0.0001 || std::abs(gm) > 0.0001 || std::abs(bm) > 0.0001 ||
      std::abs(rh) > 0.0001 || std::abs(gh) > 0.0001 || std::abs(bh) > 0.0001)
    filters << QStringLiteral(
                   "colorbalance=rs=%1:gs=%2:bs=%3:rm=%4:gm=%5:bm=%6:rh=%7:"
                   "gh=%8:bh=%9")
                   .arg(qBound(-1.0, rs, 1.0), 0, 'f', 4)
                   .arg(qBound(-1.0, gs, 1.0), 0, 'f', 4)
                   .arg(qBound(-1.0, bs, 1.0), 0, 'f', 4)
                   .arg(qBound(-1.0, rm, 1.0), 0, 'f', 4)
                   .arg(qBound(-1.0, gm, 1.0), 0, 'f', 4)
                   .arg(qBound(-1.0, bm, 1.0), 0, 'f', 4)
                   .arg(qBound(-1.0, rh, 1.0), 0, 'f', 4)
                   .arg(qBound(-1.0, gh, 1.0), 0, 'f', 4)
                   .arg(qBound(-1.0, bh, 1.0), 0, 'f', 4);

  const double hue = qBound(
      -180.0,
      hueVsHueEnabled
          ? color.value("hue").toDouble() +
                color.value("hueVsHue").toDouble() +
                flatCurveAdjustment(color.value("hueVsHuePoints").toList(),
                                    180.0)
          : 0.0,
      180.0);
  if (std::abs(hue) > 0.0001)
    filters << QStringLiteral("hue=h=%1").arg(hue, 0, 'f', 3);

  const double hueVsLuma = qBound(
      -100.0,
      hueVsLumaEnabled
          ? color.value("hueVsLuma").toDouble() +
                flatCurveAdjustment(color.value("hueVsLumaPoints").toList(),
                                    100.0)
          : 0.0,
      100.0);
  if (std::abs(hueVsLuma) > 0.0001)
    filters << QStringLiteral(
                   "huesaturation=intensity=%1:colors=a:strength=100")
                   .arg(hueVsLuma / 100.0, 0, 'f', 4);

  const auto appendChannelCurve = [&color, &filters](const QString &channel,
                                                     const QString &prefix) {
    const QString custom =
        curvePoints(color.value(prefix + "CurvePoints").toList());
    if (!custom.isEmpty()) {
      filters << QStringLiteral("curves=%1='%2'").arg(channel, custom);
      return;
    }
    const double shadows =
        qBound(-100.0, color.value(prefix + "CurveShadows").toDouble(), 100.0);
    const double midtones =
        qBound(-100.0, color.value(prefix + "CurveMidtones").toDouble(), 100.0);
    const double highlights = qBound(
        -100.0, color.value(prefix + "CurveHighlights").toDouble(), 100.0);
    if (std::abs(shadows) > 0.0001 || std::abs(midtones) > 0.0001 ||
        std::abs(highlights) > 0.0001)
      filters << QStringLiteral("curves=%1='%2'")
                     .arg(channel, curvePoints(shadows, midtones, highlights));
  };
  if (rgbCurvesEnabled) {
    const double curveShadows =
        qBound(-100.0, color.value("curveShadows").toDouble(), 100.0);
    const double curveMidtones =
        qBound(-100.0, color.value("curveMidtones").toDouble(), 100.0);
    const double curveHighlights =
        qBound(-100.0, color.value("curveHighlights").toDouble(), 100.0);
    const QString masterPoints =
        curvePoints(color.value("masterCurvePoints").toList());
    if (!masterPoints.isEmpty()) {
      filters << QStringLiteral("curves=master='%1'").arg(masterPoints);
    } else if (std::abs(curveShadows) > 0.0001 ||
               std::abs(curveMidtones) > 0.0001 ||
               std::abs(curveHighlights) > 0.0001) {
      filters << QStringLiteral("curves=master='%1'")
                     .arg(curvePoints(curveShadows, curveMidtones,
                                      curveHighlights));
    }
    appendChannelCurve(QStringLiteral("r"), QStringLiteral("red"));
    appendChannelCurve(QStringLiteral("g"), QStringLiteral("green"));
    appendChannelCurve(QStringLiteral("b"), QStringLiteral("blue"));
  }
  const double sharpen =
      creativeEnabled
          ? qBound(0.0, color.value("sharpen").toDouble(), 100.0)
          : 0.0;
  if (sharpen > 0.0)
    filters << QStringLiteral("unsharp=5:5:%1:5:5:0")
                   .arg(qBound(0.0, sharpen / 25.0, 4.0), 0, 'f', 3);
  if (look == "Faded Film" && lookIntensity > 0.01)
    filters << QStringLiteral("curves=preset=vintage");
  const QString curvePreset =
      rgbCurvesEnabled ? color.value("curvePreset", "None").toString()
                       : QStringLiteral("None");
  if (curvePreset == "Increase Contrast")
    filters << QStringLiteral("curves=preset=increase_contrast");
  else if (curvePreset == "Lift Shadows")
    filters << QStringLiteral("curves=preset=lighter");
  const double hslHue = hslSecondaryEnabled
                            ? qBound(-180.0,
                                     color.value("hslCorrectionHue").toDouble(),
                                     180.0)
                            : 0.0;
  const double hslSaturation =
      hslSecondaryEnabled
          ? qBound(-100.0, color.value("hslCorrectionSaturation").toDouble(),
                   100.0)
          : 0.0;
  const double hslLuma =
      hslSecondaryEnabled
          ? qBound(-100.0, color.value("hslCorrectionLuma").toDouble(), 100.0)
          : 0.0;
  if (std::abs(hslHue) > 0.0001 || std::abs(hslSaturation) > 0.0001 ||
      std::abs(hslLuma) > 0.0001) {
    const QString colors = colorsForHue(
        qBound(0.0, color.value("hslHueCenter").toDouble(), 360.0),
        qBound(0.0, color.value("hslHueWidth", 30.0).toDouble(), 180.0));
    const double saturationSpan =
        qBound(0.0,
               color.value("hslSaturationMax", 100.0).toDouble() -
                   color.value("hslSaturationMin").toDouble(),
               100.0);
    const double lumaSpan = qBound(0.0,
                                   color.value("hslLumaMax", 100.0).toDouble() -
                                       color.value("hslLumaMin").toDouble(),
                                   100.0);
    const double selectionStrength =
        qBound(1.0, std::sqrt(saturationSpan * lumaSpan), 100.0);
    filters << QStringLiteral(
                   "huesaturation=hue=%1:saturation=%2:intensity=%3:colors=%4:"
                   "strength=%5")
                   .arg(hslHue, 0, 'f', 3)
                   .arg(hslSaturation / 100.0, 0, 'f', 4)
                   .arg(hslLuma / 100.0, 0, 'f', 4)
                   .arg(colors)
                   .arg(selectionStrength, 0, 'f', 2);
  }
  const double denoise =
      hslSecondaryEnabled
          ? qBound(0.0, color.value("hslDenoise").toDouble(), 100.0)
          : 0.0;
  if (denoise > 0.0)
    filters << QStringLiteral("hqdn3d=%1:%1:%2:%2")
                   .arg(denoise / 20.0, 0, 'f', 3)
                   .arg(denoise / 13.0, 0, 'f', 3);
  const double blur =
      hslSecondaryEnabled
          ? qBound(0.0, color.value("hslBlur").toDouble(), 100.0)
          : 0.0;
  if (blur > 0.0)
    filters << QStringLiteral("gblur=sigma=%1")
                   .arg(qMax(0.1, blur / 20.0), 0, 'f', 3);

  const double vignette =
      vignetteEnabled
          ? qBound(-100.0, color.value("vignette").toDouble(), 100.0)
          : 0.0;
  if (std::abs(vignette) > 0.0001) {
    const double midpoint =
        qBound(0.0, color.value("vignetteMidpoint", 50.0).toDouble(), 100.0);
    const double roundness =
        qBound(-100.0, color.value("vignetteRoundness").toDouble(), 100.0);
    const double feather =
        qBound(0.0, color.value("vignetteFeather", 50.0).toDouble(), 100.0);
    const double denominator = qBound(
        2.5, 8.0 - std::abs(vignette) / 22.0 + feather / 45.0 - midpoint / 80.0,
        10.0);
    const double aspect = qBound(0.35, 1.0 + roundness / 100.0, 2.0);
    filters << QStringLiteral(
                   "vignette=angle=PI/%1:mode=%2:aspect=%3:eval=frame")
                   .arg(denominator, 0, 'f', 3)
                   .arg(vignette < 0.0 ? QStringLiteral("forward")
                                       : QStringLiteral("backward"))
                   .arg(aspect, 0, 'f', 3);
  }
  return filters.join(',');
}
