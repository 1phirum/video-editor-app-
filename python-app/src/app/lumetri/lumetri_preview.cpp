#include "app/lumetri/lumetri_preview.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {

const double kPi = 3.14159265358979323846;

double clamp01(double value) { return qBound(0.0, value, 1.0); }
float clamp01f(float value) { return qBound(0.0f, value, 1.0f); }

quint8 toByte(double value) {
  return quint8(qBound(0.0, value * 255.0 + 0.5, 255.0));
}

struct CurvePoint {
  double x;
  double y;
};

// Sorted, de-duplicated on x. Two points sharing an x would divide by a zero
// interval in the spline below, and stored curves come from a project file that
// this code does not get to police.
QList<CurvePoint> sortedPoints(QList<CurvePoint> points) {
  std::sort(points.begin(), points.end(),
            [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
  QList<CurvePoint> unique;
  for (const CurvePoint &point : points) {
    if (!unique.isEmpty() && point.x - unique.last().x < 1e-6)
      unique.last() = point;
    else
      unique.append(point);
  }
  return unique;
}

QList<CurvePoint> curveControlPoints(const QVariantList &values) {
  QList<CurvePoint> points;
  for (const QVariant &value : values) {
    const QVariantMap point = value.toMap();
    if (!point.contains(QStringLiteral("x")) ||
        !point.contains(QStringLiteral("y")))
      continue;
    points.append({clamp01(point.value(QStringLiteral("x")).toDouble()),
                   clamp01(point.value(QStringLiteral("y")).toDouble())});
  }
  return sortedPoints(points);
}

// The three-slider fallback, in the shape LumetriPipeline hands FFmpeg when a
// section has no hand-drawn curve.
QList<CurvePoint> zonePoints(double shadows, double midtones,
                             double highlights) {
  return sortedPoints({{0.0, clamp01(shadows / 400.0)},
                       {0.25, clamp01(0.25 + shadows / 400.0)},
                       {0.5, clamp01(0.5 + midtones / 400.0)},
                       {0.75, clamp01(0.75 + highlights / 400.0)},
                       {1.0, clamp01(1.0 + highlights / 400.0)}});
}

// "0/0 0.5/0.4 1/1", the same syntax vf_curves' own presets are written in.
QList<CurvePoint> presetPoints(const char *spec) {
  QList<CurvePoint> points;
  const QStringList pairs =
      QString::fromLatin1(spec).split(' ', Qt::SkipEmptyParts);
  for (const QString &pair : pairs) {
    const QStringList parts = pair.split('/');
    if (parts.size() != 2)
      continue;
    points.append({clamp01(parts.at(0).toDouble()),
                   clamp01(parts.at(1).toDouble())});
  }
  return sortedPoints(points);
}

void identityLut(std::array<quint8, 256> *lut) {
  for (int i = 0; i < 256; ++i)
    (*lut)[i] = quint8(i);
}

// Natural cubic spline through the control points, which is what vf_curves
// interpolates with, so a curve drawn in the panel lands on the same table the
// exporter will build. Outside the first and last point the value is held flat,
// again matching the filter.
void curveLut(const QList<CurvePoint> &points, std::array<quint8, 256> *lut) {
  const int n = points.size();
  if (n == 0) {
    identityLut(lut);
    return;
  }
  if (n == 1) {
    for (int i = 0; i < 256; ++i)
      (*lut)[i] = toByte(points.first().y);
    return;
  }
  QList<double> h(n - 1, 0.0);
  for (int i = 0; i < n - 1; ++i)
    h[i] = points.at(i + 1).x - points.at(i).x;
  QList<double> alpha(n, 0.0), l(n, 0.0), mu(n, 0.0), z(n, 0.0);
  for (int i = 1; i < n - 1; ++i)
    alpha[i] = 3.0 * ((points.at(i + 1).y - points.at(i).y) / h[i] -
                      (points.at(i).y - points.at(i - 1).y) / h[i - 1]);
  l[0] = 1.0;
  for (int i = 1; i < n - 1; ++i) {
    l[i] = 2.0 * (points.at(i + 1).x - points.at(i - 1).x) - h[i - 1] * mu[i - 1];
    mu[i] = h[i] / l[i];
    z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
  }
  QList<double> b(n, 0.0), c(n, 0.0), d(n, 0.0);
  for (int j = n - 2; j >= 0; --j) {
    c[j] = z[j] - mu[j] * c[j + 1];
    b[j] = (points.at(j + 1).y - points.at(j).y) / h[j] -
           h[j] * (c[j + 1] + 2.0 * c[j]) / 3.0;
    d[j] = (c[j + 1] - c[j]) / (3.0 * h[j]);
  }
  int segment = 0;
  for (int i = 0; i < 256; ++i) {
    const double x = i / 255.0;
    if (x <= points.first().x) {
      (*lut)[i] = toByte(points.first().y);
      continue;
    }
    if (x >= points.last().x) {
      (*lut)[i] = toByte(points.last().y);
      continue;
    }
    while (segment < n - 2 && x > points.at(segment + 1).x)
      ++segment;
    const double dx = x - points.at(segment).x;
    (*lut)[i] = toByte(points.at(segment).y +
                       dx * (b[segment] + dx * (c[segment] + dx * d[segment])));
  }
}

// second applied after first, so a chain of curves costs one table lookup.
void composeLut(const std::array<quint8, 256> &second,
                std::array<quint8, 256> *first) {
  for (int i = 0; i < 256; ++i)
    (*first)[i] = second[(*first)[i]];
}

// One-entry .cube cache. Grading re-runs on every slider release, and a 65^3
// LUT is a few megabytes of text, so re-parsing it per change is not an option.
QMutex g_lutMutex;
QString g_lutPath;
qint64 g_lutStamp = 0;
std::shared_ptr<const LumetriPreview::Lut3d> g_lut;

std::shared_ptr<const LumetriPreview::Lut3d> loadCube(const QString &path) {
  const QFileInfo info(path);
  if (!info.exists() || !info.isFile())
    return {};
  const qint64 stamp =
      info.lastModified().toMSecsSinceEpoch() ^ (info.size() << 1);

  QMutexLocker locker(&g_lutMutex);
  if (g_lut && g_lutPath == path && g_lutStamp == stamp)
    return g_lut;

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return {};

  auto lut = std::make_shared<LumetriPreview::Lut3d>();
  float domainMin[3]{0.0f, 0.0f, 0.0f};
  float domainMax[3]{1.0f, 1.0f, 1.0f};
  while (!file.atEnd()) {
    const QString line = QString::fromLatin1(file.readLine()).trimmed();
    if (line.isEmpty() || line.startsWith('#'))
      continue;
    const QStringList parts = line.split(QRegularExpression("\\s+"),
                                         Qt::SkipEmptyParts);
    if (parts.isEmpty())
      continue;
    const QString key = parts.first().toUpper();
    if (key == QStringLiteral("LUT_3D_SIZE")) {
      if (parts.size() >= 2)
        lut->size = qBound(2, parts.at(1).toInt(), 128);
      continue;
    }
    if (key == QStringLiteral("DOMAIN_MIN") && parts.size() >= 4) {
      for (int i = 0; i < 3; ++i)
        domainMin[i] = parts.at(i + 1).toFloat();
      continue;
    }
    if (key == QStringLiteral("DOMAIN_MAX") && parts.size() >= 4) {
      for (int i = 0; i < 3; ++i)
        domainMax[i] = parts.at(i + 1).toFloat();
      continue;
    }
    if (key == QStringLiteral("TITLE") || key == QStringLiteral("LUT_1D_SIZE"))
      continue;
    if (parts.size() < 3)
      continue;
    bool okR = false, okG = false, okB = false;
    const float r = parts.at(0).toFloat(&okR);
    const float g = parts.at(1).toFloat(&okG);
    const float b = parts.at(2).toFloat(&okB);
    if (!okR || !okG || !okB)
      continue;
    lut->data.append(r);
    lut->data.append(g);
    lut->data.append(b);
  }
  file.close();

  // A short file is a truncated or 1D LUT; either way there is nothing to
  // sample, and a partial table would tear the picture rather than grade it.
  const int expected = lut->size * lut->size * lut->size * 3;
  if (lut->size < 2 || lut->data.size() < expected)
    return {};
  lut->data.resize(expected);

  // Rescale to the 0..1 domain the sampler assumes, so a log LUT declaring a
  // wider domain still lands on the right cell.
  for (int i = 0; i < 3; ++i) {
    const float span = domainMax[i] - domainMin[i];
    if (std::abs(span - 1.0f) < 1e-6f && std::abs(domainMin[i]) < 1e-6f)
      continue;
    if (std::abs(span) < 1e-6f)
      continue;
    for (int j = i; j < lut->data.size(); j += 3)
      lut->data[j] = (lut->data[j] - domainMin[i]) / span;
  }

  g_lutPath = path;
  g_lutStamp = stamp;
  g_lut = lut;
  return g_lut;
}

// The hue/sat curves collapse to a single scalar in the export chain; a preview
// that averaged them differently would disagree with the rendered file, so this
// is a straight copy of LumetriPipeline's version.
double flatCurveAdjustment(const QVariantList &points, double range) {
  if (points.size() < 2)
    return 0.0;
  double total = 0.0;
  int count = 0;
  for (const QVariant &value : points) {
    const QVariantMap point = value.toMap();
    if (!point.contains(QStringLiteral("y")))
      continue;
    total += clamp01(point.value(QStringLiteral("y")).toDouble()) - 0.5;
    ++count;
  }
  return count > 0 ? qBound(-range, total / count * range * 2.0, range) : 0.0;
}

double bounded(const QVariantMap &map, const char *key, double lo, double hi,
               double fallback = 0.0) {
  return qBound(lo, map.value(QLatin1String(key), fallback).toDouble(), hi);
}

// vf_curves' own preset tables, so "Increase Contrast" and the Faded Film look
// bend the picture the same way the exported file will.
const char *kIncreaseContrast = "0/0 0.149/0.066 0.831/0.905 0.905/0.98 1/1";
const char *kLighter = "0/0 0.4/0.5 1/1";
const char *kVintageR = "0/0.11 0.42/0.51 1/0.95";
const char *kVintageG = "0/0 0.50/0.48 1/1";
const char *kVintageB = "0/0.22 0.49/0.44 1/0.8";

} // namespace

LumetriPreview::Correction
LumetriPreview::build(const QVariantMap &clipLumetri,
                      const QVariantMap &mediaColor,
                      const QVariantMap &projectColor) {
  Correction c;
  identityLut(&c.tone);
  identityLut(&c.curveR);
  identityLut(&c.curveG);
  identityLut(&c.curveB);

  const QString lutPath = mediaColor.value(QStringLiteral("inputLut"))
                              .toString()
                              .trimmed();
  if (!lutPath.isEmpty() &&
      lutPath.compare(QStringLiteral("None"), Qt::CaseInsensitive) != 0) {
    c.lut = loadCube(lutPath);
    c.lutTetrahedral =
        projectColor.value(QStringLiteral("lutInterpolation"),
                           QStringLiteral("Tetrahedral"))
            .toString() != QStringLiteral("Trilinear");
    if (c.lut)
      c.identity = false;
  }

  const QVariantMap &color = clipLumetri;
  if (color.isEmpty())
    return c;

  const bool basicEnabled =
      color.value(QStringLiteral("basicEnabled"), true).toBool();
  const bool creativeEnabled =
      color.value(QStringLiteral("creativeEnabled"), true).toBool();
  const bool curvesEnabled =
      color.value(QStringLiteral("curvesEnabled"), true).toBool();
  const bool rgbCurvesEnabled =
      curvesEnabled &&
      color.value(QStringLiteral("rgbCurvesEnabled"), true).toBool();
  const bool hueVsHueEnabled =
      curvesEnabled &&
      color.value(QStringLiteral("hueVsHueEnabled"), true).toBool();
  const bool hueVsSatEnabled =
      curvesEnabled &&
      color.value(QStringLiteral("hueVsSatEnabled"), true).toBool();
  const bool hueVsLumaEnabled =
      curvesEnabled &&
      color.value(QStringLiteral("hueVsLumaEnabled"), true).toBool();
  const bool lumaVsSatEnabled =
      curvesEnabled &&
      color.value(QStringLiteral("lumaVsSatEnabled"), true).toBool();
  const bool satVsSatEnabled =
      curvesEnabled &&
      color.value(QStringLiteral("satVsSatEnabled"), true).toBool();
  const bool colorWheelsEnabled =
      color.value(QStringLiteral("colorWheelsEnabled"), true).toBool();
  const bool hslSecondaryEnabled =
      color.value(QStringLiteral("hslSecondaryEnabled"), true).toBool();
  const bool vignetteEnabled =
      color.value(QStringLiteral("vignetteEnabled"), true).toBool();

  const double exposure = basicEnabled ? bounded(color, "exposure", -5.0, 5.0)
                                       : 0.0;
  const double contrast =
      basicEnabled ? bounded(color, "contrast", -100.0, 100.0) : 0.0;
  const double shadows =
      basicEnabled ? bounded(color, "shadows", -100.0, 100.0) : 0.0;
  const double highlights =
      basicEnabled ? bounded(color, "highlights", -100.0, 100.0) : 0.0;
  const double whites =
      basicEnabled ? bounded(color, "whites", -100.0, 100.0) : 0.0;
  const double blacks =
      basicEnabled ? bounded(color, "blacks", -100.0, 100.0) : 0.0;
  const double saturation =
      basicEnabled ? bounded(color, "saturation", 0.0, 200.0, 100.0) : 100.0;
  const double vibrance =
      creativeEnabled ? bounded(color, "vibrance", -100.0, 100.0) : 0.0;
  const double fade = creativeEnabled ? bounded(color, "fade", 0.0, 100.0) : 0.0;
  const double hdrSpecular =
      basicEnabled ? bounded(color, "hdrSpecular", -100.0, 100.0) : 0.0;
  const double creativeSaturation =
      creativeEnabled ? bounded(color, "creativeSaturation", 0.0, 200.0, 100.0)
                      : 100.0;
  const double hueVsSat =
      qBound(-100.0,
             hueVsSatEnabled
                 ? color.value(QStringLiteral("hueVsSat")).toDouble() +
                       flatCurveAdjustment(
                           color.value(QStringLiteral("hueVsSatPoints"))
                               .toList(),
                           100.0)
                 : 0.0,
             100.0);
  const double lumaVsSat =
      qBound(-100.0,
             lumaVsSatEnabled
                 ? color.value(QStringLiteral("lumaVsSat")).toDouble() +
                       flatCurveAdjustment(
                           color.value(QStringLiteral("lumaVsSatPoints"))
                               .toList(),
                           100.0)
                 : 0.0,
             100.0);
  const double satVsSat =
      qBound(-100.0,
             satVsSatEnabled
                 ? color.value(QStringLiteral("satVsSat")).toDouble() +
                       flatCurveAdjustment(
                           color.value(QStringLiteral("satVsSatPoints"))
                               .toList(),
                           100.0)
                 : 0.0,
             100.0);
  const double shadowLuma =
      colorWheelsEnabled ? bounded(color, "shadowLuma", -100.0, 100.0) : 0.0;
  const double midtoneLuma =
      colorWheelsEnabled ? bounded(color, "midtoneLuma", -100.0, 100.0) : 0.0;
  const double highlightLuma =
      colorWheelsEnabled ? bounded(color, "highlightLuma", -100.0, 100.0) : 0.0;

  // eq. The four derived numbers are copied from LumetriPipeline so the monitor
  // and the exported file read the same sliders the same way.
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

  // Approximation: FFmpeg's eq builds this table for luma and leaves chroma
  // alone. Running it on each channel instead keeps the code to one lookup and
  // costs a slight saturation drift at the extremes of a strong contrast push.
  if (std::abs(brightness) > 0.0001 ||
      std::abs(contrastFactor - 1.0) > 0.0001 ||
      std::abs(gamma - 1.0) > 0.0001) {
    for (int i = 0; i < 256; ++i) {
      double v = (i / 255.0 - 0.5) * contrastFactor + 0.5 + brightness;
      v = v <= 0.0 ? 0.0 : std::pow(v, 1.0 / gamma);
      c.tone[i] = toByte(v);
    }
    c.toneActive = true;
    c.identity = false;
  }
  c.saturation = float(saturationFactor);
  if (std::abs(saturationFactor - 1.0) > 0.0001)
    c.identity = false;

  // colorbalance. Temperature/tint, both tint rows of the wheels, and the wheel
  // pucks all fold into the same nine shadow/midtone/highlight offsets.
  const double temperature =
      basicEnabled ? bounded(color, "temperature", -100.0, 100.0) : 0.0;
  const double tint =
      basicEnabled ? bounded(color, "tint", -100.0, 100.0) : 0.0;
  const double shadowTint =
      colorWheelsEnabled ? bounded(color, "shadowTint", -100.0, 100.0) : 0.0;
  const double midtoneTint =
      colorWheelsEnabled ? bounded(color, "midtoneTint", -100.0, 100.0) : 0.0;
  const double highlightTint =
      colorWheelsEnabled ? bounded(color, "highlightTint", -100.0, 100.0) : 0.0;
  const double creativeShadowTint =
      creativeEnabled ? bounded(color, "creativeShadowTint", -100.0, 100.0)
                      : 0.0;
  const double creativeHighlightTint =
      creativeEnabled ? bounded(color, "creativeHighlightTint", -100.0, 100.0)
                      : 0.0;
  const double tintBalance =
      creativeEnabled ? bounded(color, "tintBalance", -100.0, 100.0) : 0.0;
  const double shadowWheelX =
      colorWheelsEnabled ? bounded(color, "shadowWheelX", -100.0, 100.0) : 0.0;
  const double shadowWheelY =
      colorWheelsEnabled ? bounded(color, "shadowWheelY", -100.0, 100.0) : 0.0;
  const double midtoneWheelX =
      colorWheelsEnabled ? bounded(color, "midtoneWheelX", -100.0, 100.0) : 0.0;
  const double midtoneWheelY =
      colorWheelsEnabled ? bounded(color, "midtoneWheelY", -100.0, 100.0) : 0.0;
  const double highlightWheelX =
      colorWheelsEnabled ? bounded(color, "highlightWheelX", -100.0, 100.0)
                         : 0.0;
  const double highlightWheelY =
      colorWheelsEnabled ? bounded(color, "highlightWheelY", -100.0, 100.0)
                         : 0.0;
  const double lookIntensity =
      creativeEnabled ? bounded(color, "lookIntensity", 0.0, 200.0, 100.0) / 100.0
                      : 0.0;
  const QString look =
      creativeEnabled
          ? color.value(QStringLiteral("look"), QStringLiteral("None"))
                .toString()
          : QStringLiteral("None");

  double red = temperature / 300.0;
  double blue = -temperature / 300.0;
  const double greenMid = tint / 300.0;
  if (look == QStringLiteral("Warm")) {
    red += 0.08 * lookIntensity;
    blue -= 0.06 * lookIntensity;
  }
  if (look == QStringLiteral("Cool")) {
    red -= 0.06 * lookIntensity;
    blue += 0.08 * lookIntensity;
  }
  c.shadow[0] = float(qBound(-1.0, red + shadowWheelX / 300.0, 1.0));
  c.shadow[2] = float(qBound(-1.0, blue - shadowWheelX / 300.0, 1.0));
  c.shadow[1] = float(qBound(
      -1.0, (shadowTint + creativeShadowTint - shadowWheelY) / 300.0, 1.0));
  c.midtone[0] = float(qBound(-1.0, midtoneWheelX / 300.0, 1.0));
  c.midtone[2] = float(qBound(-1.0, -midtoneWheelX / 300.0, 1.0));
  c.midtone[1] = float(qBound(
      -1.0,
      greenMid + (midtoneTint - midtoneWheelY + tintBalance * 0.5) / 300.0,
      1.0));
  c.highlight[0] = float(qBound(-1.0, highlightWheelX / 300.0, 1.0));
  c.highlight[2] = float(qBound(-1.0, -highlightWheelX / 300.0, 1.0));
  c.highlight[1] = float(qBound(-1.0,
                                (highlightTint + creativeHighlightTint -
                                 highlightWheelY - tintBalance * 0.5) /
                                    300.0,
                                1.0));
  for (int i = 0; i < 3; ++i) {
    if (std::abs(c.shadow[i]) > 0.0001f || std::abs(c.midtone[i]) > 0.0001f ||
        std::abs(c.highlight[i]) > 0.0001f)
      c.balanceActive = true;
  }
  if (c.balanceActive)
    c.identity = false;

  // hue=h. The Hue slider and Hue Vs Hue share one rotation, and the pipeline
  // gates both on hueVsHueEnabled, so this does too.
  const double hue =
      qBound(-180.0,
             hueVsHueEnabled
                 ? color.value(QStringLiteral("hue")).toDouble() +
                       color.value(QStringLiteral("hueVsHue")).toDouble() +
                       flatCurveAdjustment(
                           color.value(QStringLiteral("hueVsHuePoints"))
                               .toList(),
                           180.0)
                 : 0.0,
             180.0);
  c.hueDegrees = float(hue);
  if (std::abs(hue) > 0.0001)
    c.identity = false;

  // huesaturation=intensity, i.e. Hue Vs Luma, which lifts or sinks every colour.
  const double hueVsLuma =
      qBound(-100.0,
             hueVsLumaEnabled
                 ? color.value(QStringLiteral("hueVsLuma")).toDouble() +
                       flatCurveAdjustment(
                           color.value(QStringLiteral("hueVsLumaPoints"))
                               .toList(),
                           100.0)
                 : 0.0,
             100.0);
  c.lightness = float(hueVsLuma / 100.0);
  if (std::abs(hueVsLuma) > 0.0001)
    c.identity = false;

  // curves. Everything that ends up as a curves= filter is folded into three
  // tables. The export chain puts the preset curves after unsharp; composing
  // them here moves them ahead of the sharpen pass, which is the one ordering
  // difference and only shows on a heavily sharpened frame.
  const auto channelCurve = [&color](const char *prefix,
                                     std::array<quint8, 256> *lut) -> bool {
    const QList<CurvePoint> custom = curveControlPoints(
        color.value(QLatin1String(prefix) + QStringLiteral("CurvePoints"))
            .toList());
    if (custom.size() >= 2) {
      curveLut(custom, lut);
      return true;
    }
    const QString base = QLatin1String(prefix);
    const double shadows = qBound(
        -100.0,
        color.value(base + QStringLiteral("CurveShadows")).toDouble(), 100.0);
    const double midtones = qBound(
        -100.0,
        color.value(base + QStringLiteral("CurveMidtones")).toDouble(), 100.0);
    const double highlights = qBound(
        -100.0,
        color.value(base + QStringLiteral("CurveHighlights")).toDouble(), 100.0);
    if (std::abs(shadows) <= 0.0001 && std::abs(midtones) <= 0.0001 &&
        std::abs(highlights) <= 0.0001)
      return false;
    curveLut(zonePoints(shadows, midtones, highlights), lut);
    return true;
  };

  if (rgbCurvesEnabled) {
    std::array<quint8, 256> master{};
    identityLut(&master);
    bool masterActive = false;
    const QList<CurvePoint> masterPoints = curveControlPoints(
        color.value(QStringLiteral("masterCurvePoints")).toList());
    if (masterPoints.size() >= 2) {
      curveLut(masterPoints, &master);
      masterActive = true;
    } else {
      const double curveShadows =
          bounded(color, "curveShadows", -100.0, 100.0);
      const double curveMidtones =
          bounded(color, "curveMidtones", -100.0, 100.0);
      const double curveHighlights =
          bounded(color, "curveHighlights", -100.0, 100.0);
      if (std::abs(curveShadows) > 0.0001 ||
          std::abs(curveMidtones) > 0.0001 ||
          std::abs(curveHighlights) > 0.0001) {
        curveLut(zonePoints(curveShadows, curveMidtones, curveHighlights),
                 &master);
        masterActive = true;
      }
    }
    if (masterActive) {
      c.curveR = master;
      c.curveG = master;
      c.curveB = master;
      c.curvesActive = true;
    }

    std::array<quint8, 256> channel{};
    if (channelCurve("red", &channel)) {
      composeLut(channel, &c.curveR);
      c.curvesActive = true;
    }
    if (channelCurve("green", &channel)) {
      composeLut(channel, &c.curveG);
      c.curvesActive = true;
    }
    if (channelCurve("blue", &channel)) {
      composeLut(channel, &c.curveB);
      c.curvesActive = true;
    }
  }

  const double sharpen =
      creativeEnabled ? bounded(color, "sharpen", 0.0, 100.0) : 0.0;
  c.sharpen = float(qBound(0.0, sharpen / 25.0, 4.0));
  if (sharpen > 0.0)
    c.identity = false;

  if (look == QStringLiteral("Faded Film") && lookIntensity > 0.01) {
    std::array<quint8, 256> preset{};
    curveLut(presetPoints(kVintageR), &preset);
    composeLut(preset, &c.curveR);
    curveLut(presetPoints(kVintageG), &preset);
    composeLut(preset, &c.curveG);
    curveLut(presetPoints(kVintageB), &preset);
    composeLut(preset, &c.curveB);
    c.curvesActive = true;
  }

  const QString curvePreset =
      rgbCurvesEnabled
          ? color.value(QStringLiteral("curvePreset"), QStringLiteral("None"))
                .toString()
          : QStringLiteral("None");
  const char *presetSpec = nullptr;
  if (curvePreset == QStringLiteral("Increase Contrast"))
    presetSpec = kIncreaseContrast;
  else if (curvePreset == QStringLiteral("Lift Shadows"))
    presetSpec = kLighter;
  if (presetSpec) {
    std::array<quint8, 256> preset{};
    curveLut(presetPoints(presetSpec), &preset);
    composeLut(preset, &c.curveR);
    composeLut(preset, &c.curveG);
    composeLut(preset, &c.curveB);
    c.curvesActive = true;
  }
  if (c.curvesActive)
    c.identity = false;

  // HSL secondary. Export narrows the key to vf_huesaturation's six colour
  // flags; here the hue/saturation/luma sliders drive a real window, so the
  // preview keys tighter than the file does when the range is narrow.
  const double hslHue =
      hslSecondaryEnabled ? bounded(color, "hslCorrectionHue", -180.0, 180.0)
                          : 0.0;
  const double hslSaturation =
      hslSecondaryEnabled
          ? bounded(color, "hslCorrectionSaturation", -100.0, 100.0)
          : 0.0;
  const double hslLuma =
      hslSecondaryEnabled ? bounded(color, "hslCorrectionLuma", -100.0, 100.0)
                          : 0.0;
  if (std::abs(hslHue) > 0.0001 || std::abs(hslSaturation) > 0.0001 ||
      std::abs(hslLuma) > 0.0001) {
    const double satMin = bounded(color, "hslSaturationMin", 0.0, 100.0);
    const double satMax = bounded(color, "hslSaturationMax", 0.0, 100.0, 100.0);
    const double lumaMin = bounded(color, "hslLumaMin", 0.0, 100.0);
    const double lumaMax = bounded(color, "hslLumaMax", 0.0, 100.0, 100.0);
    c.hslActive = true;
    c.hslHueCenter = float(bounded(color, "hslHueCenter", 0.0, 360.0));
    c.hslHueWidth = float(bounded(color, "hslHueWidth", 0.0, 180.0, 30.0));
    c.hslSatMin = float(qMin(satMin, satMax) / 100.0);
    c.hslSatMax = float(qMax(satMin, satMax) / 100.0);
    c.hslLumaMin = float(qMin(lumaMin, lumaMax) / 100.0);
    c.hslLumaMax = float(qMax(lumaMin, lumaMax) / 100.0);
    c.hslHue = float(hslHue);
    c.hslSaturation = float(hslSaturation / 100.0);
    c.hslLuma = float(hslLuma / 100.0);
    c.identity = false;
  }

  const double denoise =
      hslSecondaryEnabled ? bounded(color, "hslDenoise", 0.0, 100.0) : 0.0;
  c.denoise = float(denoise / 100.0);
  if (denoise > 0.0)
    c.identity = false;

  const double blur =
      hslSecondaryEnabled ? bounded(color, "hslBlur", 0.0, 100.0) : 0.0;
  if (blur > 0.0) {
    c.blurSigma = float(qMax(0.1, blur / 20.0));
    c.identity = false;
  }

  const double vignette =
      vignetteEnabled ? bounded(color, "vignette", -100.0, 100.0) : 0.0;
  if (std::abs(vignette) > 0.0001) {
    const double midpoint = bounded(color, "vignetteMidpoint", 0.0, 100.0, 50.0);
    const double roundness =
        bounded(color, "vignetteRoundness", -100.0, 100.0);
    const double feather = bounded(color, "vignetteFeather", 0.0, 100.0, 50.0);
    const double denominator = qBound(
        2.5, 8.0 - std::abs(vignette) / 22.0 + feather / 45.0 - midpoint / 80.0,
        10.0);
    c.vignette = float(vignette);
    c.vignetteAngle = float(kPi / denominator);
    c.vignetteAspect = float(qBound(0.35, 1.0 + roundness / 100.0, 2.0));
    c.vignetteBackward = vignette >= 0.0;
    c.identity = false;
  }

  return c;
}

namespace {

inline int lutIndex(int size, int r, int g, int b) {
  return ((b * size + g) * size + r) * 3;
}

// .cube sampling, in the two modes the exporter offers. Tetrahedral is the
// default there, and on a log-to-Rec709 LUT the difference from trilinear is
// visible in the shadows, so it is worth mirroring rather than approximating.
void sampleLut3d(const LumetriPreview::Lut3d &lut, bool tetrahedral, float *rgb) {
  const int size = lut.size;
  const int last = size - 1;
  const float x = clamp01f(rgb[0]) * last;
  const float y = clamp01f(rgb[1]) * last;
  const float z = clamp01f(rgb[2]) * last;
  // Clamped to last-1 so the upper corner of the cell is always in range; the
  // loader guarantees size >= 2, so last-1 is never negative.
  const int xi = qBound(0, int(x), last - 1);
  const int yi = qBound(0, int(y), last - 1);
  const int zi = qBound(0, int(z), last - 1);
  const float fx = x - xi;
  const float fy = y - yi;
  const float fz = z - zi;
  const float *d = lut.data.constData();
  const float *c000 = d + lutIndex(size, xi, yi, zi);
  const float *c100 = d + lutIndex(size, xi + 1, yi, zi);
  const float *c010 = d + lutIndex(size, xi, yi + 1, zi);
  const float *c110 = d + lutIndex(size, xi + 1, yi + 1, zi);
  const float *c001 = d + lutIndex(size, xi, yi, zi + 1);
  const float *c101 = d + lutIndex(size, xi + 1, yi, zi + 1);
  const float *c011 = d + lutIndex(size, xi, yi + 1, zi + 1);
  const float *c111 = d + lutIndex(size, xi + 1, yi + 1, zi + 1);

  for (int k = 0; k < 3; ++k) {
    float v;
    if (tetrahedral) {
      if (fx > fy) {
        if (fy > fz)
          v = c000[k] + (c100[k] - c000[k]) * fx + (c110[k] - c100[k]) * fy +
              (c111[k] - c110[k]) * fz;
        else if (fx > fz)
          v = c000[k] + (c100[k] - c000[k]) * fx + (c111[k] - c101[k]) * fy +
              (c101[k] - c100[k]) * fz;
        else
          v = c000[k] + (c101[k] - c001[k]) * fx + (c111[k] - c101[k]) * fy +
              (c001[k] - c000[k]) * fz;
      } else {
        if (fz > fy)
          v = c000[k] + (c111[k] - c011[k]) * fx + (c011[k] - c001[k]) * fy +
              (c001[k] - c000[k]) * fz;
        else if (fz > fx)
          v = c000[k] + (c111[k] - c011[k]) * fx + (c010[k] - c000[k]) * fy +
              (c011[k] - c010[k]) * fz;
        else
          v = c000[k] + (c110[k] - c010[k]) * fx + (c010[k] - c000[k]) * fy +
              (c111[k] - c110[k]) * fz;
      }
    } else {
      const float m00 = c000[k] + (c100[k] - c000[k]) * fx;
      const float m10 = c010[k] + (c110[k] - c010[k]) * fx;
      const float m01 = c001[k] + (c101[k] - c001[k]) * fx;
      const float m11 = c011[k] + (c111[k] - c011[k]) * fx;
      const float m0 = m00 + (m10 - m00) * fy;
      const float m1 = m01 + (m11 - m01) * fy;
      v = m0 + (m1 - m0) * fz;
    }
    rgb[k] = clamp01f(v);
  }
}

// vf_colorbalance's get_component, weights and all: each of the three offsets is
// faded in by the pixel's own lightness, which is why a shadow push leaves the
// highlights alone.
inline float balanceComponent(float v, float l, float s, float m, float h) {
  const float a = 4.0f, b = 0.333f, scale = 0.7f;
  s *= clamp01f((b - l) * a + 0.5f) * scale;
  m *= clamp01f((l - b) * a + 0.5f) * clamp01f((1.0f - l - b) * a + 0.5f) * scale;
  h *= clamp01f((l + b - 1.0f) * a + 0.5f) * scale;
  return clamp01f(v + s + m + h);
}

// BT.601, the matrix vf_hue rotates in, so the preview turns colours the same
// direction and distance the exported file will.
inline void rotateHue(float cosH, float sinH, float *rgb) {
  const float y = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
  const float u = -0.14713f * rgb[0] - 0.28886f * rgb[1] + 0.436f * rgb[2];
  const float v = 0.615f * rgb[0] - 0.51499f * rgb[1] - 0.10001f * rgb[2];
  const float u2 = cosH * u - sinH * v;
  const float v2 = sinH * u + cosH * v;
  rgb[0] = clamp01f(y + 1.13983f * v2);
  rgb[1] = clamp01f(y - 0.39465f * u2 - 0.58060f * v2);
  rgb[2] = clamp01f(y + 2.03211f * u2);
}

inline void applyLightness(float amount, float *rgb) {
  for (int k = 0; k < 3; ++k)
    rgb[k] = clamp01f(amount > 0.0f ? rgb[k] + (1.0f - rgb[k]) * amount
                                    : rgb[k] + rgb[k] * amount);
}

inline void applySaturation(float factor, float *rgb) {
  const float y = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
  for (int k = 0; k < 3; ++k)
    rgb[k] = clamp01f(y + (rgb[k] - y) * factor);
}

// Hue in degrees plus HSL saturation and lightness, for the secondary key.
inline void hueSatLuma(const float *rgb, float *hue, float *sat, float *luma) {
  const float mx = qMax(rgb[0], qMax(rgb[1], rgb[2]));
  const float mn = qMin(rgb[0], qMin(rgb[1], rgb[2]));
  const float l = (mx + mn) * 0.5f;
  const float span = mx - mn;
  *luma = l;
  if (span < 1e-6f) {
    *hue = 0.0f;
    *sat = 0.0f;
    return;
  }
  const float denom = 1.0f - std::abs(2.0f * l - 1.0f);
  *sat = denom > 1e-6f ? clamp01f(span / denom) : 0.0f;
  float h;
  if (mx == rgb[0])
    h = (rgb[1] - rgb[2]) / span;
  else if (mx == rgb[1])
    h = 2.0f + (rgb[2] - rgb[0]) / span;
  else
    h = 4.0f + (rgb[0] - rgb[1]) / span;
  h *= 60.0f;
  if (h < 0.0f)
    h += 360.0f;
  *hue = h;
}

// Soft-edged window, so a keyed correction fades out instead of tearing along
// the selection boundary.
inline float windowWeight(float value, float low, float high, float feather) {
  if (value < low)
    return low - value >= feather ? 0.0f : 1.0f - (low - value) / feather;
  if (value > high)
    return value - high >= feather ? 0.0f : 1.0f - (value - high) / feather;
  return 1.0f;
}

// A tight w*h*3 copy, so the blur passes below never have to think about the
// image's stride or its alpha channel.
QByteArray packRgb(const QImage &image, int channels) {
  const int w = image.width();
  const int h = image.height();
  QByteArray out(qsizetype(w) * h * 3, Qt::Uninitialized);
  quint8 *dst = reinterpret_cast<quint8 *>(out.data());
  for (int y = 0; y < h; ++y) {
    const quint8 *line = image.constScanLine(y);
    for (int x = 0; x < w; ++x, dst += 3) {
      dst[0] = line[x * channels + 0];
      dst[1] = line[x * channels + 1];
      dst[2] = line[x * channels + 2];
    }
  }
  return out;
}

// Separable sliding-window box blur, in place. Three passes of this is the usual
// stand-in for a Gaussian and is what the blur and denoise paths below lean on.
void boxBlurPass(QByteArray *buffer, int w, int h, int radius) {
  if (radius < 1 || w < 1 || h < 1)
    return;
  const int count = radius * 2 + 1;
  const int rowBytes = w * 3;
  QByteArray scratch(buffer->size(), Qt::Uninitialized);
  quint8 *src = reinterpret_cast<quint8 *>(buffer->data());
  quint8 *mid = reinterpret_cast<quint8 *>(scratch.data());

  for (int y = 0; y < h; ++y) {
    const quint8 *row = src + qsizetype(y) * rowBytes;
    quint8 *out = mid + qsizetype(y) * rowBytes;
    for (int k = 0; k < 3; ++k) {
      int sum = 0;
      for (int x = -radius; x <= radius; ++x)
        sum += row[qBound(0, x, w - 1) * 3 + k];
      for (int x = 0; x < w; ++x) {
        out[x * 3 + k] = quint8(sum / count);
        sum += row[qBound(0, x + radius + 1, w - 1) * 3 + k] -
               row[qBound(0, x - radius, w - 1) * 3 + k];
      }
    }
  }
  for (int x = 0; x < w; ++x) {
    for (int k = 0; k < 3; ++k) {
      const int offset = x * 3 + k;
      int sum = 0;
      for (int y = -radius; y <= radius; ++y)
        sum += mid[qsizetype(qBound(0, y, h - 1)) * rowBytes + offset];
      for (int y = 0; y < h; ++y) {
        src[qsizetype(y) * rowBytes + offset] = quint8(sum / count);
        sum += mid[qsizetype(qBound(0, y + radius + 1, h - 1)) * rowBytes +
                   offset] -
               mid[qsizetype(qBound(0, y - radius, h - 1)) * rowBytes + offset];
      }
    }
  }
}

int radiusForSigma(float sigma) {
  return qBound(1, int(std::lround(double(sigma))), 64);
}

inline int toIndex(float value) {
  return int(qBound(0.0f, value * 255.0f + 0.5f, 255.0f));
}

} // namespace

void LumetriPreview::apply(QImage *image, const Correction &correction) {
  if (!image || image->isNull() || correction.identity)
    return;

  // The provider hands out RGB888 for scrubs and RGBA8888 for playback, and both
  // are byte-ordered R,G,B, so one loop covers them. Anything else is converted
  // rather than special-cased.
  if (image->format() != QImage::Format_RGB888 &&
      image->format() != QImage::Format_RGBA8888 &&
      image->format() != QImage::Format_RGBX8888) {
    *image = image->convertToFormat(QImage::Format_RGBA8888);
    if (image->isNull())
      return;
  }
  const int channels = image->format() == QImage::Format_RGB888 ? 3 : 4;
  const int w = image->width();
  const int h = image->height();
  if (w < 1 || h < 1)
    return;

  const float radians = float(correction.hueDegrees * kPi / 180.0);
  const float cosH = std::cos(radians);
  const float sinH = std::sin(radians);
  const bool rotateActive = std::abs(correction.hueDegrees) > 0.0001f;
  const bool saturationActive =
      std::abs(correction.saturation - 1.0f) > 0.0001f;
  const bool lightnessActive = std::abs(correction.lightness) > 0.0001f;
  const bool hslWide = correction.hslHueWidth >= 170.0f;

  for (int y = 0; y < h; ++y) {
    quint8 *line = image->scanLine(y);
    for (int x = 0; x < w; ++x) {
      quint8 *p = line + x * channels;
      float rgb[3]{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f};

      if (correction.lut)
        sampleLut3d(*correction.lut, correction.lutTetrahedral, rgb);
      if (correction.toneActive)
        for (int k = 0; k < 3; ++k)
          rgb[k] = correction.tone[toIndex(rgb[k])] / 255.0f;
      if (saturationActive)
        applySaturation(correction.saturation, rgb);
      if (correction.balanceActive) {
        const float l = (qMax(rgb[0], qMax(rgb[1], rgb[2])) +
                         qMin(rgb[0], qMin(rgb[1], rgb[2]))) *
                        0.5f;
        for (int k = 0; k < 3; ++k)
          rgb[k] = balanceComponent(rgb[k], l, correction.shadow[k],
                                    correction.midtone[k],
                                    correction.highlight[k]);
      }
      if (rotateActive)
        rotateHue(cosH, sinH, rgb);
      if (lightnessActive)
        applyLightness(correction.lightness, rgb);
      if (correction.curvesActive) {
        rgb[0] = correction.curveR[toIndex(rgb[0])] / 255.0f;
        rgb[1] = correction.curveG[toIndex(rgb[1])] / 255.0f;
        rgb[2] = correction.curveB[toIndex(rgb[2])] / 255.0f;
      }
      if (correction.hslActive) {
        float hue = 0.0f, sat = 0.0f, luma = 0.0f;
        hueSatLuma(rgb, &hue, &sat, &luma);
        float mask = 1.0f;
        if (!hslWide) {
          // colorsForHue() collapses a wide range to "all colours" in the export
          // chain, so anything narrower keys on the hue distance here.
          float distance = std::abs(hue - correction.hslHueCenter);
          if (distance > 180.0f)
            distance = 360.0f - distance;
          const float half = qMax(1.0f, correction.hslHueWidth * 0.5f);
          const float t = distance / half;
          mask = t >= 1.0f ? 0.0f : 1.0f - t * t;
        }
        if (mask > 0.0f) {
          mask *= windowWeight(sat, correction.hslSatMin, correction.hslSatMax,
                               0.08f);
          mask *= windowWeight(luma, correction.hslLumaMin,
                               correction.hslLumaMax, 0.08f);
        }
        if (mask > 0.001f) {
          if (std::abs(correction.hslHue) > 0.0001f) {
            const float angle = float(correction.hslHue * mask * kPi / 180.0);
            rotateHue(std::cos(angle), std::sin(angle), rgb);
          }
          if (std::abs(correction.hslSaturation) > 0.0001f)
            applySaturation(1.0f + correction.hslSaturation * mask, rgb);
          if (std::abs(correction.hslLuma) > 0.0001f)
            applyLightness(correction.hslLuma * mask, rgb);
        }
      }

      p[0] = quint8(toIndex(rgb[0]));
      p[1] = quint8(toIndex(rgb[1]));
      p[2] = quint8(toIndex(rgb[2]));
    }
  }

  // unsharp=5:5:amount - a 5x5 blur subtracted back off the picture.
  if (correction.sharpen > 0.0f) {
    QByteArray blurred = packRgb(*image, channels);
    boxBlurPass(&blurred, w, h, 2);
    const quint8 *soft = reinterpret_cast<const quint8 *>(blurred.constData());
    for (int y = 0; y < h; ++y) {
      quint8 *line = image->scanLine(y);
      const quint8 *row = soft + qsizetype(y) * w * 3;
      for (int x = 0; x < w; ++x) {
        quint8 *p = line + x * channels;
        for (int k = 0; k < 3; ++k) {
          const float detail = float(p[k]) - float(row[x * 3 + k]);
          p[k] = quint8(qBound(0.0f, p[k] + correction.sharpen * detail, 255.0f));
        }
      }
    }
  }

  // Approximation: hqdn3d also averages across time, which a single still cannot
  // do, so the denoise slider becomes a spatial smooth blended by its strength.
  if (correction.denoise > 0.0f) {
    QByteArray smooth = packRgb(*image, channels);
    boxBlurPass(&smooth, w, h, 1);
    const quint8 *soft = reinterpret_cast<const quint8 *>(smooth.constData());
    const float mix = qBound(0.0f, correction.denoise, 1.0f);
    for (int y = 0; y < h; ++y) {
      quint8 *line = image->scanLine(y);
      const quint8 *row = soft + qsizetype(y) * w * 3;
      for (int x = 0; x < w; ++x) {
        quint8 *p = line + x * channels;
        for (int k = 0; k < 3; ++k)
          p[k] = quint8(qBound(
              0.0f, p[k] * (1.0f - mix) + row[x * 3 + k] * mix, 255.0f));
      }
    }
  }

  // gblur. Three box passes stand in for the Gaussian.
  if (correction.blurSigma > 0.0f) {
    QByteArray blurred = packRgb(*image, channels);
    const int radius = radiusForSigma(correction.blurSigma);
    for (int pass = 0; pass < 3; ++pass)
      boxBlurPass(&blurred, w, h, radius);
    const quint8 *soft = reinterpret_cast<const quint8 *>(blurred.constData());
    for (int y = 0; y < h; ++y) {
      quint8 *line = image->scanLine(y);
      const quint8 *row = soft + qsizetype(y) * w * 3;
      for (int x = 0; x < w; ++x) {
        quint8 *p = line + x * channels;
        for (int k = 0; k < 3; ++k)
          p[k] = row[x * 3 + k];
      }
    }
  }

  // vignette. cos(angle * dnorm)^4, and backward divides where forward
  // multiplies, exactly as the filter does; strength lives in the angle.
  if (std::abs(correction.vignette) > 0.0001f) {
    const float x0 = (w - 1) * 0.5f;
    const float y0 = (h - 1) * 0.5f;
    const float yscale = correction.vignetteAspect > 0.0f
                             ? correction.vignetteAspect
                             : 1.0f;
    const float dmax = std::hypot(x0, y0 * yscale);
    const float limit = float(kPi / 2.0);
    for (int y = 0; y < h; ++y) {
      quint8 *line = image->scanLine(y);
      const float dy = (y - y0) * yscale;
      for (int x = 0; x < w; ++x) {
        const float dnorm =
            dmax > 0.0f ? std::hypot(float(x) - x0, dy) / dmax : 0.0f;
        const float angle = correction.vignetteAngle * dnorm;
        float factor = 0.0f;
        if (angle < limit) {
          const float c = std::cos(angle);
          factor = (c * c) * (c * c);
        }
        quint8 *p = line + x * channels;
        for (int k = 0; k < 3; ++k) {
          const float v = correction.vignetteBackward
                              ? p[k] / qMax(factor, 0.05f)
                              : p[k] * factor;
          p[k] = quint8(qBound(0.0f, v, 255.0f));
        }
      }
    }
  }
}
