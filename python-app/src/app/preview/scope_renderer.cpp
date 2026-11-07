#include "app/preview/scope_renderer.h"

#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include <cmath>

namespace scope {
namespace {

constexpr QRgb kBackground = qRgb(14, 14, 14);
constexpr QRgb kGraticule = qRgb(70, 70, 70);
constexpr QRgb kGraticuleFaint = qRgb(44, 44, 44);

// Soft-saturating tone map: a lone pixel still registers, yet a column where
// thousands share a value does not blow out to a flat block. gain sets how fast
// the trace fills in.
inline float toneMap(float count, float gain) {
  return 1.0f - std::exp(-count * gain);
}

// Separable box blur over a float intensity buffer. Two of these in a row
// approximate a Gaussian; run on the accumulation buffer before the tone map,
// it fuses the plotted points into the smooth, glowing ribbons a real scope
// shows instead of a grainy trace. Edge samples clamp - imperceptible on a graph.
void boxBlur(QVector<float> &buf, int W, int H, int radius) {
  if (radius < 1 || W <= 0 || H <= 0)
    return;
  const float norm = 1.0f / (2 * radius + 1);
  QVector<float> tmp(W * H, 0.0f);
  // Horizontal pass -> tmp
  for (int y = 0; y < H; ++y) {
    const int row = y * W;
    float sum = 0.0f;
    for (int k = -radius; k <= radius; ++k)
      sum += buf[row + qBound(0, k, W - 1)];
    for (int x = 0; x < W; ++x) {
      tmp[row + x] = sum * norm;
      sum += buf[row + qBound(0, x + radius + 1, W - 1)] -
             buf[row + qBound(0, x - radius, W - 1)];
    }
  }
  // Vertical pass -> buf
  for (int x = 0; x < W; ++x) {
    float sum = 0.0f;
    for (int k = -radius; k <= radius; ++k)
      sum += tmp[qBound(0, k, H - 1) * W + x];
    for (int y = 0; y < H; ++y) {
      buf[y * W + x] = sum * norm;
      sum += tmp[qBound(0, y + radius + 1, H - 1) * W + x] -
             tmp[qBound(0, y - radius, H - 1) * W + x];
    }
  }
}

// Rec.709 luma (HD), the same weighting the grade shader uses.
inline float luma709(int r, int g, int b) {
  return 0.2126f * float(r) + 0.7152f * float(g) + 0.0722f * float(b);
}

QImage makeCanvas(QSize out) {
  QImage img(out, QImage::Format_RGBA8888);
  img.fill(kBackground);
  return img;
}

QImage ensureRgb(const QImage &src) {
  if (src.format() == QImage::Format_RGB888)
    return src;
  return src.convertToFormat(QImage::Format_RGB888);
}

} // namespace

Type typeFromId(const QString &id) {
  if (id.startsWith(QLatin1String("waveform-rgb")))
    return Type::WaveformRGB;
  if (id.startsWith(QLatin1String("parade")))
    return Type::RGBParade;
  if (id.startsWith(QLatin1String("vectorscope")))
    return Type::Vectorscope;
  if (id.startsWith(QLatin1String("histogram-rgb")))
    return Type::HistogramRGB;
  if (id.startsWith(QLatin1String("histogram")))
    return Type::HistogramLuma;
  return Type::WaveformLuma;
}

namespace {

// Horizontal IRE gridlines at 0/10/.../100, plus the panel separators a parade
// needs. Drawn first so the trace sits over them.
void drawWaveformGraticule(QImage *img, int panels) {
  QPainter p(img);
  p.setRenderHint(QPainter::Antialiasing, false);
  const int w = img->width();
  const int h = img->height();
  for (int i = 0; i <= 10; ++i) {
    const int y = int(std::lround((h - 1) * (1.0 - i / 10.0)));
    p.setPen(QPen(QColor(i % 5 == 0 ? kGraticule : kGraticuleFaint)));
    p.drawLine(0, y, w - 1, y);
  }
  if (panels > 1) {
    p.setPen(QPen(QColor(kGraticule)));
    for (int s = 1; s < panels; ++s) {
      const int x = int(std::lround(double(w) * s / panels));
      p.drawLine(x, 0, x, h - 1);
    }
  }
}

// Waveform / parade. mode: 0 = luma (one green trace), 1 = RGB overlaid full
// width, 2 = RGB parade in three side-by-side panels.
QImage renderWaveform(const QImage &srcIn, int mode, QSize out) {
  QImage img = makeCanvas(out);
  drawWaveformGraticule(&img, mode == 2 ? 3 : 1);
  const QImage src = ensureRgb(srcIn);
  const int sw = src.width();
  const int sh = src.height();
  if (sw <= 0 || sh <= 0)
    return img;

  const int W = out.width();
  const int H = out.height();
  const int panelW = mode == 2 ? W / 3 : W;
  QVector<float> accR(W * H, 0.0f), accG, accB;
  if (mode != 0) {
    accG.fill(0.0f, W * H);
    accB.fill(0.0f, W * H);
  }

  // Fractional vertical plot: a value between two rows deposits into both,
  // weighted by how close it is to each. Without this the trace quantises to
  // integer rows and reads as a dotted scatter; splitting the sample makes a
  // continuous, film-like column of light.
  auto plot = [&](QVector<float> &acc, int ox, float value01) {
    if (ox < 0 || ox >= W)
      return;
    float fy = (H - 1) * (1.0f - value01);
    if (fy < 0.0f) fy = 0.0f; else if (fy > H - 1) fy = H - 1;
    const int y0 = int(fy);
    const float frac = fy - y0;
    acc[y0 * W + ox] += (1.0f - frac);
    if (y0 + 1 < H)
      acc[(y0 + 1) * W + ox] += frac;
  };

  // Walk every OUTPUT column and read the source column it maps to, rather than
  // scattering source columns across the graph. When the graph is wider than the
  // sample (it always is) this fills every column instead of leaving blank gaps -
  // the difference between a sparse dot-cloud and a solid trace.
  auto columnFor = [&](int ox, int width) {
    return qMin(sw - 1, int(qint64(ox) * sw / width));
  };

  if (mode == 2) {
    for (int ox = 0; ox < panelW; ++ox) {
      const int sx = columnFor(ox, panelW);
      for (int y = 0; y < sh; ++y) {
        const uchar *px = src.scanLine(y) + sx * 3;
        plot(accR, ox, px[0] / 255.0f);
        plot(accG, panelW + ox, px[1] / 255.0f);
        plot(accB, 2 * panelW + ox, px[2] / 255.0f);
      }
    }
  } else {
    for (int ox = 0; ox < W; ++ox) {
      const int sx = columnFor(ox, W);
      for (int y = 0; y < sh; ++y) {
        const uchar *px = src.scanLine(y) + sx * 3;
        if (mode == 0) {
          plot(accR, ox, luma709(px[0], px[1], px[2]) / 255.0f);
        } else {
          plot(accR, ox, px[0] / 255.0f);
          plot(accG, ox, px[1] / 255.0f);
          plot(accB, ox, px[2] / 255.0f);
        }
      }
    }
  }

  // Fuse the plotted points into smooth, glowing bands - the difference between
  // a grainy trace and the silky ribbons a hardware scope shows. Blur spreads
  // each sample's energy, so the peak per cell drops and the gain rises to match.
  const int blurRadius = qMax(1, qRound(H / 300.0));
  boxBlur(accR, W, H, blurRadius);
  if (mode != 0) {
    boxBlur(accG, W, H, blurRadius);
    boxBlur(accB, W, H, blurRadius);
  }

  const float gain = mode == 0 ? 0.65f : 0.55f;
  // Direct scanLine writes, not setPixel/pixel: this loop touches every output
  // pixel every refresh, and QImage's per-pixel accessors are bounds-checked
  // function calls that dominated the render cost and lagged playback. The
  // canvas is Format_RGBA8888, so bytes are R,G,B,A in order on every platform.
  for (int y = 0; y < H; ++y) {
    uchar *out = img.scanLine(y);
    const int row = y * W;
    for (int x = 0; x < W; ++x) {
      uchar *o = out + x * 4;
      if (mode == 0) {
        const float t = toneMap(accR[row + x], gain);
        if (t <= 0.0f)
          continue;
        // A phosphor-green luma trace, brightening to white where it piles up.
        o[0] = uchar(int(t * 180));
        o[1] = uchar(int(t * 255));
        o[2] = uchar(int(t * 170));
        o[3] = 255;
      } else {
        const float tr = toneMap(accR[row + x], gain);
        const float tg = toneMap(accG[row + x], gain);
        const float tb = toneMap(accB[row + x], gain);
        if (tr <= 0.0f && tg <= 0.0f && tb <= 0.0f)
          continue;
        // Additive over the graticule: a neutral grey plots equally into all
        // three and reads white, which is the point of the overlay.
        o[0] = uchar(qMin(255, int(o[0]) + int(tr * 255)));
        o[1] = uchar(qMin(255, int(o[1]) + int(tg * 255)));
        o[2] = uchar(qMin(255, int(o[2]) + int(tb * 255)));
        o[3] = 255;
      }
    }
  }
  return img;
}

} // namespace

namespace {

// BT.601 full-range chroma from normalised RGB. The vectorscope is a chroma plot;
// angle is hue, distance from centre is saturation.
inline void chroma601(float r, float g, float b, float *cb, float *cr) {
  *cb = -0.168736f * r - 0.331264f * g + 0.5f * b;
  *cr = 0.5f * r - 0.418688f * g - 0.081312f * b;
}

void drawVectorscopeGraticule(QImage *img, QPointF c, float s) {
  QPainter p(img);
  p.setRenderHint(QPainter::Antialiasing, true);
  const float R = s * 0.55f; // 100%-saturation circle
  p.setPen(QPen(QColor(kGraticule)));
  p.drawEllipse(c, R, R);
  p.setPen(QPen(QColor(kGraticuleFaint)));
  p.drawLine(QPointF(c.x() - R, c.y()), QPointF(c.x() + R, c.y()));
  p.drawLine(QPointF(c.x(), c.y() - R), QPointF(c.x(), c.y() + R));

  // The six 75%-bar targets. cb plots to +x, cr to -y (video convention).
  struct Bar { float r, g, b; };
  const Bar bars[6] = {{0.75f, 0, 0},        {0.75f, 0.75f, 0},
                       {0, 0.75f, 0},        {0, 0.75f, 0.75f},
                       {0, 0, 0.75f},        {0.75f, 0, 0.75f}};
  p.setPen(QPen(QColor(200, 200, 200)));
  for (const Bar &bar : bars) {
    float cb, cr;
    chroma601(bar.r, bar.g, bar.b, &cb, &cr);
    const QPointF t(c.x() + cb * s, c.y() - cr * s);
    p.drawRect(QRectF(t.x() - 3, t.y() - 3, 6, 6));
  }

  // Skin-tone (I) line: the axis warm skin tones fall along.
  float cb, cr;
  chroma601(0.82f, 0.57f, 0.46f, &cb, &cr);
  const float len = R * 1.02f;
  const float ang = std::atan2(-cr, cb);
  p.setPen(QPen(QColor(150, 130, 110)));
  p.drawLine(c, QPointF(c.x() + std::cos(ang) * len,
                        c.y() + std::sin(ang) * len));
}

QImage renderVectorscope(const QImage &srcIn, QSize out) {
  QImage img = makeCanvas(out);
  const int S = qMin(out.width(), out.height());
  const QPointF center(out.width() / 2.0, out.height() / 2.0);
  const float s = (S / 2.0f - 6.0f) / 0.55f; // 100% chroma -> the graticule circle
  drawVectorscopeGraticule(&img, center, s);

  const QImage src = ensureRgb(srcIn);
  const int sw = src.width(), sh = src.height();
  if (sw <= 0 || sh <= 0)
    return img;

  const int W = out.width(), H = out.height();
  QVector<float> acc(W * H, 0.0f);
  for (int y = 0; y < sh; ++y) {
    const uchar *line = src.scanLine(y);
    for (int x = 0; x < sw; ++x) {
      const uchar *px = line + x * 3;
      float cb, cr;
      chroma601(px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f, &cb, &cr);
      const int ox = int(std::lround(center.x() + cb * s));
      const int oy = int(std::lround(center.y() - cr * s));
      if (ox < 0 || ox >= W || oy < 0 || oy >= H)
        continue;
      acc[oy * W + ox] += 1.0f;
    }
  }
  // Same glow treatment as the waveform: blur the chroma cloud into a smooth,
  // luminous plot rather than a spray of dots.
  boxBlur(acc, W, H, qMax(1, qRound(qMin(W, H) / 300.0)));
  for (int y = 0; y < H; ++y) {
    uchar *out = img.scanLine(y);
    const int row = y * W;
    for (int x = 0; x < W; ++x) {
      const float t = toneMap(acc[row + x], 0.7f);
      if (t <= 0.0f)
        continue;
      uchar *o = out + x * 4;
      o[0] = uchar(qMin(255, int(o[0]) + int(t * 210)));
      o[1] = uchar(qMin(255, int(o[1]) + int(t * 235)));
      o[2] = uchar(qMin(255, int(o[2]) + int(t * 190)));
      o[3] = 255;
    }
  }
  return img;
}

} // namespace

namespace {

// Histogram: pixel count per 8-bit bin, no spatial information. rgb=false plots
// one luma distribution; rgb=true overlays the three channels additively.
QImage renderHistogram(const QImage &srcIn, bool rgb, QSize out) {
  QImage img = makeCanvas(out);
  drawWaveformGraticule(&img, 1);
  const QImage src = ensureRgb(srcIn);
  const int sw = src.width(), sh = src.height();
  if (sw <= 0 || sh <= 0)
    return img;

  QVector<double> binR(256, 0.0), binG(256, 0.0), binB(256, 0.0);
  for (int y = 0; y < sh; ++y) {
    const uchar *line = src.scanLine(y);
    for (int x = 0; x < sw; ++x) {
      const uchar *px = line + x * 3;
      if (rgb) {
        binR[px[0]] += 1.0;
        binG[px[1]] += 1.0;
        binB[px[2]] += 1.0;
      } else {
        binR[int(std::lround(luma709(px[0], px[1], px[2])))] += 1.0;
      }
    }
  }
  double peak = 1.0;
  for (int i = 0; i < 256; ++i) {
    peak = std::max(peak, binR[i]);
    if (rgb) {
      peak = std::max(peak, binG[i]);
      peak = std::max(peak, binB[i]);
    }
  }

  const int W = out.width(), H = out.height();
  auto column = [&](const QVector<double> &bin, int bri, int gri, int bbi) {
    for (int x = 0; x < W; ++x) {
      const int b = qMin(255, x * 256 / W);
      const int barH = int((bin[b] / peak) * (H - 1));
      for (int y = H - 1; y >= H - barH; --y) {
        uchar *o = img.scanLine(y) + x * 4;
        o[0] = uchar(qMin(255, int(o[0]) + bri));
        o[1] = uchar(qMin(255, int(o[1]) + gri));
        o[2] = uchar(qMin(255, int(o[2]) + bbi));
        o[3] = 255;
      }
    }
  };
  if (rgb) {
    column(binR, 150, 30, 30);
    column(binG, 30, 150, 30);
    column(binB, 40, 40, 160);
  } else {
    column(binR, 150, 160, 150);
  }
  return img;
}

} // namespace

QImage render(const QImage &gradedSrc, Type type, QSize out) {
  if (out.width() <= 0 || out.height() <= 0)
    out = QSize(320, 240);
  switch (type) {
  case Type::WaveformLuma:
    return renderWaveform(gradedSrc, 0, out);
  case Type::WaveformRGB:
    return renderWaveform(gradedSrc, 1, out);
  case Type::RGBParade:
    return renderWaveform(gradedSrc, 2, out);
  case Type::Vectorscope:
    return renderVectorscope(gradedSrc, out);
  case Type::HistogramLuma:
    return renderHistogram(gradedSrc, false, out);
  case Type::HistogramRGB:
    return renderHistogram(gradedSrc, true, out);
  }
  return renderWaveform(gradedSrc, 0, out);
}

} // namespace scope
