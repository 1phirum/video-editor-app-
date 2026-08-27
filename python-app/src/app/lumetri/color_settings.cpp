#include "app/lumetri/color_settings.h"

#include <QtGlobal>
#include <algorithm>

namespace {
bool assign(QVariantMap *settings, const QString &key, const QVariant &value) {
  if (!settings || settings->value(key) == value)
    return false;
  settings->insert(key, value);
  return true;
}

bool isOneOf(const QString &value, const QStringList &choices) {
  return choices.contains(value);
}
} // namespace

QVariantMap ColorSettings::defaults() {
  return {{"displayColorManagement", false},
          {"extendedDynamicRange", false},
          {"transmitVideoStream", false},
          {"hdrGraphicsWhite", "203 (75% HLG, 58% PQ)"},
          {"lutInterpolation", "Tetrahedral"},
          {"viewerGamma", "2.4 (Broadcast)"},
          {"autoDetectLogColorSpace", true},
          {"workingColorSpace", "Rec. 709"},
          {"autoToneMapMedia", true},
          {"transmitMode", "Listener"},
          {"transmitStreamId", ""},
          {"transmitAddress", ""},
          {"transmitPort", 4201},
          {"transmitPassphrase", ""},
          {"transmitLatencyMs", 0},
          {"transmitQuality", "Medium"}};
}

QVariantMap ColorSettings::clipDefaults() {
  return {{"basicEnabled", true},
          {"creativeEnabled", true},
          {"curvesEnabled", true},
          {"rgbCurvesEnabled", true},
          {"hueVsHueEnabled", true},
          {"hueVsSatEnabled", true},
          {"hueVsLumaEnabled", true},
          {"lumaVsSatEnabled", true},
          {"satVsSatEnabled", true},
          {"colorWheelsEnabled", true},
          {"hslSecondaryEnabled", true},
          {"vignetteEnabled", true},
          {"temperature", 0.0},
          {"tint", 0.0},
          {"exposure", 0.0},
          {"contrast", 0.0},
          {"highlights", 0.0},
          {"shadows", 0.0},
          {"whites", 0.0},
          {"blacks", 0.0},
          {"hdrSpecular", 0.0},
          {"saturation", 100.0},
          {"vibrance", 0.0},
          {"sharpen", 0.0},
          {"fade", 0.0},
          {"lookIntensity", 100.0},
          {"creativeSaturation", 100.0},
          {"creativeShadowTint", 0.0},
          {"creativeHighlightTint", 0.0},
          {"tintBalance", 0.0},
          {"curvePreset", "None"},
          {"curveShadows", 0.0},
          {"curveMidtones", 0.0},
          {"curveHighlights", 0.0},
          {"redCurveShadows", 0.0},
          {"redCurveMidtones", 0.0},
          {"redCurveHighlights", 0.0},
          {"greenCurveShadows", 0.0},
          {"greenCurveMidtones", 0.0},
          {"greenCurveHighlights", 0.0},
          {"blueCurveShadows", 0.0},
          {"blueCurveMidtones", 0.0},
          {"blueCurveHighlights", 0.0},
          {"hueVsHue", 0.0},
          {"hueVsSat", 0.0},
          {"hueVsLuma", 0.0},
          {"lumaVsSat", 0.0},
          {"satVsSat", 0.0},
          {"hue", 0.0},
          {"shadowTint", 0.0},
          {"midtoneTint", 0.0},
          {"highlightTint", 0.0},
          {"shadowWheelX", 0.0},
          {"shadowWheelY", 0.0},
          {"shadowLuma", 0.0},
          {"midtoneWheelX", 0.0},
          {"midtoneWheelY", 0.0},
          {"midtoneLuma", 0.0},
          {"highlightWheelX", 0.0},
          {"highlightWheelY", 0.0},
          {"highlightLuma", 0.0},
          {"hslHueCenter", 0.0},
          {"hslHueWidth", 30.0},
          {"hslSaturationMin", 0.0},
          {"hslSaturationMax", 100.0},
          {"hslLumaMin", 0.0},
          {"hslLumaMax", 100.0},
          {"hslCorrectionHue", 0.0},
          {"hslCorrectionSaturation", 0.0},
          {"hslCorrectionLuma", 0.0},
          {"hslDenoise", 0.0},
          {"hslBlur", 0.0},
          {"vignette", 0.0},
          {"vignetteMidpoint", 50.0},
          {"vignetteRoundness", 0.0},
          {"vignetteFeather", 50.0},
          {"look", "None"},
          {"masterCurvePoints", QVariantList{}},
          {"redCurvePoints", QVariantList{}},
          {"greenCurvePoints", QVariantList{}},
          {"blueCurvePoints", QVariantList{}},
          {"hueVsHuePoints", QVariantList{}},
          {"hueVsSatPoints", QVariantList{}},
          {"hueVsLumaPoints", QVariantList{}},
          {"lumaVsSatPoints", QVariantList{}},
          {"satVsSatPoints", QVariantList{}}};
}

bool ColorSettings::setProjectValue(QVariantMap *settings, const QString &key,
                                    const QVariant &value) {
  if (!settings)
    return false;
  if (key == "displayColorManagement" || key == "extendedDynamicRange" ||
      key == "transmitVideoStream" || key == "autoDetectLogColorSpace" ||
      key == "autoToneMapMedia")
    return assign(settings, key, value.toBool());
  if (key == "transmitPort")
    return assign(settings, key, qBound(1, value.toInt(), 65535));
  if (key == "transmitLatencyMs")
    return assign(settings, key, qBound(0, value.toInt(), 10000));
  if (key == "lutInterpolation" &&
      isOneOf(value.toString(), {"Trilinear", "Tetrahedral"}))
    return assign(settings, key, value.toString());
  if (key == "workingColorSpace" &&
      isOneOf(value.toString(), {"Rec. 709", "Rec. 2100 HLG", "Rec. 2100 PQ"}))
    return assign(settings, key, value.toString());
  if (key == "viewerGamma" &&
      isOneOf(value.toString(), {"2.2 (Web)", "2.4 (Broadcast)"}))
    return assign(settings, key, value.toString());
  if (key == "hdrGraphicsWhite" && !value.toString().trimmed().isEmpty())
    return assign(settings, key, value.toString());
  if (key == "transmitMode" &&
      isOneOf(value.toString(), {"Listener", "Rendezvous", "Caller"}))
    return assign(settings, key, value.toString());
  if ((key == "transmitStreamId" || key == "transmitAddress" ||
       key == "transmitPassphrase") &&
      value.canConvert<QString>())
    return assign(settings, key, value.toString());
  if (key == "transmitQuality" &&
      isOneOf(value.toString(), {"Low", "Medium", "High"}))
    return assign(settings, key, value.toString());
  return false;
}

bool ColorSettings::setClipValue(QVariantMap *settings, const QString &key,
                                 const QVariant &value) {
  if (!settings)
    return false;
  const QStringList sectionEnabledKeys{
      "basicEnabled",       "creativeEnabled",     "curvesEnabled",
      "rgbCurvesEnabled",   "hueVsHueEnabled",     "hueVsSatEnabled",
      "hueVsLumaEnabled",   "lumaVsSatEnabled",    "satVsSatEnabled",
      "colorWheelsEnabled", "hslSecondaryEnabled", "vignetteEnabled"};
  if (sectionEnabledKeys.contains(key))
    return assign(settings, key, value.toBool());

  const QStringList curvePointKeys{
      "masterCurvePoints", "redCurvePoints",  "greenCurvePoints",
      "blueCurvePoints",   "hueVsHuePoints",  "hueVsSatPoints",
      "hueVsLumaPoints",   "lumaVsSatPoints", "satVsSatPoints"};
  if (curvePointKeys.contains(key)) {
    QVariantList points;
    for (const auto &entry : value.toList()) {
      if (points.size() >= 16)
        break;
      const QVariantMap point = entry.toMap();
      if (!point.contains("x") || !point.contains("y"))
        continue;
      points.append(
          QVariantMap{{"x", qBound(0.0, point.value("x").toDouble(), 1.0)},
                      {"y", qBound(0.0, point.value("y").toDouble(), 1.0)}});
    }
    std::sort(points.begin(), points.end(),
              [](const QVariant &left, const QVariant &right) {
                return left.toMap().value("x").toDouble() <
                       right.toMap().value("x").toDouble();
              });
    if (!points.isEmpty() && points.size() < 2)
      return false;
    return assign(settings, key, points);
  }
  const QVariantMap bounds{
      {"temperature", QVariantList{-100.0, 100.0}},
      {"tint", QVariantList{-100.0, 100.0}},
      {"exposure", QVariantList{-5.0, 5.0}},
      {"contrast", QVariantList{-100.0, 100.0}},
      {"highlights", QVariantList{-100.0, 100.0}},
      {"shadows", QVariantList{-100.0, 100.0}},
      {"whites", QVariantList{-100.0, 100.0}},
      {"blacks", QVariantList{-100.0, 100.0}},
      {"hdrSpecular", QVariantList{-100.0, 100.0}},
      {"saturation", QVariantList{0.0, 200.0}},
      {"vibrance", QVariantList{-100.0, 100.0}},
      {"sharpen", QVariantList{0.0, 100.0}},
      {"fade", QVariantList{0.0, 100.0}},
      {"lookIntensity", QVariantList{0.0, 200.0}},
      {"creativeSaturation", QVariantList{0.0, 200.0}},
      {"creativeShadowTint", QVariantList{-100.0, 100.0}},
      {"creativeHighlightTint", QVariantList{-100.0, 100.0}},
      {"tintBalance", QVariantList{-100.0, 100.0}},
      {"curveShadows", QVariantList{-100.0, 100.0}},
      {"curveMidtones", QVariantList{-100.0, 100.0}},
      {"curveHighlights", QVariantList{-100.0, 100.0}},
      {"redCurveShadows", QVariantList{-100.0, 100.0}},
      {"redCurveMidtones", QVariantList{-100.0, 100.0}},
      {"redCurveHighlights", QVariantList{-100.0, 100.0}},
      {"greenCurveShadows", QVariantList{-100.0, 100.0}},
      {"greenCurveMidtones", QVariantList{-100.0, 100.0}},
      {"greenCurveHighlights", QVariantList{-100.0, 100.0}},
      {"blueCurveShadows", QVariantList{-100.0, 100.0}},
      {"blueCurveMidtones", QVariantList{-100.0, 100.0}},
      {"blueCurveHighlights", QVariantList{-100.0, 100.0}},
      {"hueVsHue", QVariantList{-180.0, 180.0}},
      {"hueVsSat", QVariantList{-100.0, 100.0}},
      {"hueVsLuma", QVariantList{-100.0, 100.0}},
      {"lumaVsSat", QVariantList{-100.0, 100.0}},
      {"satVsSat", QVariantList{-100.0, 100.0}},
      {"vignette", QVariantList{-100.0, 100.0}},
      {"vignetteMidpoint", QVariantList{0.0, 100.0}},
      {"vignetteRoundness", QVariantList{-100.0, 100.0}},
      {"vignetteFeather", QVariantList{0.0, 100.0}},
      {"hue", QVariantList{-180.0, 180.0}},
      {"shadowTint", QVariantList{-100.0, 100.0}},
      {"midtoneTint", QVariantList{-100.0, 100.0}},
      {"highlightTint", QVariantList{-100.0, 100.0}},
      {"shadowWheelX", QVariantList{-100.0, 100.0}},
      {"shadowWheelY", QVariantList{-100.0, 100.0}},
      {"shadowLuma", QVariantList{-100.0, 100.0}},
      {"midtoneWheelX", QVariantList{-100.0, 100.0}},
      {"midtoneWheelY", QVariantList{-100.0, 100.0}},
      {"midtoneLuma", QVariantList{-100.0, 100.0}},
      {"highlightWheelX", QVariantList{-100.0, 100.0}},
      {"highlightWheelY", QVariantList{-100.0, 100.0}},
      {"highlightLuma", QVariantList{-100.0, 100.0}},
      {"hslHueCenter", QVariantList{0.0, 360.0}},
      {"hslHueWidth", QVariantList{0.0, 180.0}},
      {"hslSaturationMin", QVariantList{0.0, 100.0}},
      {"hslSaturationMax", QVariantList{0.0, 100.0}},
      {"hslLumaMin", QVariantList{0.0, 100.0}},
      {"hslLumaMax", QVariantList{0.0, 100.0}},
      {"hslCorrectionHue", QVariantList{-180.0, 180.0}},
      {"hslCorrectionSaturation", QVariantList{-100.0, 100.0}},
      {"hslCorrectionLuma", QVariantList{-100.0, 100.0}},
      {"hslDenoise", QVariantList{0.0, 100.0}},
      {"hslBlur", QVariantList{0.0, 100.0}}};
  if (bounds.contains(key)) {
    const auto range = bounds.value(key).toList();
    return assign(settings, key,
                  qBound(range.at(0).toDouble(), value.toDouble(),
                         range.at(1).toDouble()));
  }
  if (key == "look" &&
      isOneOf(value.toString(), {"None", "Faded Film", "Warm", "Cool"}))
    return assign(settings, key, value.toString());
  if (key == "curvePreset" &&
      isOneOf(value.toString(), {"None", "Increase Contrast", "Lift Shadows"}))
    return assign(settings, key, value.toString());
  return false;
}

bool ColorSettings::setMediaValue(QVariantMap *settings, const QString &key,
                                  const QVariant &value) {
  if (!settings)
    return false;
  if (key == "inputLut" || key == "overrideMediaColorSpace")
    return assign(settings, key, value.toString());
  if (key == "useMediaColorSpace")
    return assign(settings, key, value.toBool());
  return false;
}
