#include "app/subtitles/khmer_support.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QColor>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QUuid>
#include <algorithm>
#include <cmath>

#include "app/text_effect_catalog.h"

namespace KhmerSupport {
QString fontDirectory() {
  const QString bundled = QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("fonts"));
  if (QDir(bundled).exists())
    return bundled;
  const QString windows = QStringLiteral("C:/Windows/Fonts");
  return QDir(windows).exists() ? windows : QString();
}

QString preferredFont(const QString &requested) {
  const QStringList allowed = {QStringLiteral("Khmer OS System"),
                               QStringLiteral("Khmer OS Siemreap"),
                               QStringLiteral("Khmer OS Battambang")};
  if (allowed.contains(requested))
    return requested;
  if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
    return QStringLiteral("Khmer OS System");
  QFontDatabase database;
  const QStringList khmerFamilies = database.families(QFontDatabase::Khmer);
  for (const QString &family : allowed)
    if (khmerFamilies.contains(family, Qt::CaseInsensitive))
      return family;
  if (!khmerFamilies.isEmpty())
    return khmerFamilies.first();
  return QStringLiteral("Khmer OS System");
}

QString normalize(const QString &text) {
  // NFC keeps Khmer combining marks in a stable order for Qt and libass.
  return text.normalized(QString::NormalizationForm_C).trimmed();
}

namespace {

// One laid-out cluster: what to draw, where its pen sits inside the block, and
// the transform the solver handed back for it.
struct PlacedGlyph {
  QString text;
  QPointF pen;  // baseline origin, block-relative
  double advance = 0.0;
  double opacity = 1.0;
  double scale = 1.0;
  double dx = 0.0;  // em
  double dy = 0.0;
  QColor color;  // invalid: use the style's fill
};

// Greedy word wrap over the solver's words, so a letter animation can never
// break a word across two lines. Words carry their own clusters, and a cluster's
// pen only depends on the text - which is why the layout is done once per cue
// and only the transforms change from one animation step to the next.
QList<PlacedGlyph> layoutCaption(const QVariantList &words, const QFont &font,
                                 double maxWidth, const QString &alignment,
                                 QSizeF *sizeOut) {
  const QFontMetricsF fm(font);
  const double spaceWidth = fm.horizontalAdvance(QLatin1Char(' '));
  const double lineHeight = fm.height();

  struct Token {
    QList<QPair<QString, double>> glyphs;
    double width = 0.0;
  };
  QList<Token> tokens;
  for (const QVariant &wordValue : words) {
    Token token;
    for (const QVariant &glyphValue :
         wordValue.toMap().value(QStringLiteral("glyphs")).toList()) {
      const QString text = glyphValue.toMap().value(QStringLiteral("text")).toString();
      const double advance = fm.horizontalAdvance(text);
      token.glyphs.append({text, advance});
      token.width += advance;
    }
    if (!token.glyphs.isEmpty())
      tokens.append(token);
  }

  QList<QList<int>> lines;
  QList<double> lineWidths;
  QList<int> current;
  double currentWidth = 0.0;
  for (int i = 0; i < tokens.size(); ++i) {
    const double added =
        (current.isEmpty() ? 0.0 : spaceWidth) + tokens.at(i).width;
    if (!current.isEmpty() && currentWidth + added > maxWidth) {
      lines.append(current);
      lineWidths.append(currentWidth);
      current.clear();
      currentWidth = 0.0;
      currentWidth += tokens.at(i).width;
    } else {
      currentWidth += added;
    }
    current.append(i);
  }
  if (!current.isEmpty()) {
    lines.append(current);
    lineWidths.append(currentWidth);
  }

  double blockWidth = 0.0;
  for (double w : lineWidths)
    blockWidth = std::max(blockWidth, w);
  if (sizeOut)
    *sizeOut = QSizeF(blockWidth, lines.size() * lineHeight);

  QList<PlacedGlyph> placed;
  for (int li = 0; li < lines.size(); ++li) {
    double x = alignment == QLatin1String("left")
                   ? 0.0
                   : alignment == QLatin1String("right")
                         ? blockWidth - lineWidths.at(li)
                         : (blockWidth - lineWidths.at(li)) / 2.0;
    const double baseline = li * lineHeight + fm.ascent();
    for (int k = 0; k < lines.at(li).size(); ++k) {
      if (k > 0)
        x += spaceWidth;
      for (const auto &glyph : tokens.at(lines.at(li).at(k)).glyphs) {
        PlacedGlyph pg;
        pg.text = glyph.first;
        pg.advance = glyph.second;
        pg.pen = QPointF(x, baseline);
        placed.append(pg);
        x += glyph.second;
      }
    }
  }
  return placed;
}

// Copies one solved step onto an existing layout. Walked in the same order the
// solver emits, which is the order layoutCaption consumed.
void applyStep(QList<PlacedGlyph> *placed, const QVariantMap &solved) {
  int i = 0;
  for (const QVariant &wordValue :
       solved.value(QStringLiteral("words")).toList()) {
    for (const QVariant &glyphValue :
         wordValue.toMap().value(QStringLiteral("glyphs")).toList()) {
      if (i >= placed->size())
        return;
      const QVariantMap glyph = glyphValue.toMap();
      PlacedGlyph &pg = (*placed)[i++];
      pg.opacity = glyph.value(QStringLiteral("opacity"), 1.0).toDouble();
      pg.scale = glyph.value(QStringLiteral("scale"), 1.0).toDouble();
      pg.dx = glyph.value(QStringLiteral("dx")).toDouble();
      pg.dy = glyph.value(QStringLiteral("dy")).toDouble();
      const QString color = glyph.value(QStringLiteral("color")).toString();
      pg.color = color.isEmpty() ? QColor() : QColor(color);
    }
  }
}

// Draws the block with one letter style. Every layer is a stroke or a fill of
// the same glyph outline, in the order a reader expects to see them stacked:
// shadow behind, then the extruded body, the glow around it, the outline, and
// the face on top.
void paintCaption(QPainter *painter, const QFont &font,
                  const QList<PlacedGlyph> &glyphs, const QPointF &origin,
                  const QVariantMap &recipe, const QColor &fallbackFill,
                  const QRectF &blockRect, double em) {
  const QFontMetricsF fm(font);
  const double outlineWidth = recipe.value(QStringLiteral("outline")).toDouble() * em;
  const QColor outlineColor(recipe.value(QStringLiteral("outlineColor")).toString());
  const double shadowDx = recipe.value(QStringLiteral("shadowDx")).toDouble() * em;
  const double shadowDy = recipe.value(QStringLiteral("shadowDy")).toDouble() * em;
  const double shadowBlur = recipe.value(QStringLiteral("shadowBlur")).toDouble() * em;
  const QColor shadowColor(recipe.value(QStringLiteral("shadowColor")).toString());
  const double glow = recipe.value(QStringLiteral("glow")).toDouble() * em;
  const QColor glowColor(recipe.value(QStringLiteral("glowColor")).toString());
  const double extrude = recipe.value(QStringLiteral("extrude")).toDouble() * em;
  const QColor extrudeColor(recipe.value(QStringLiteral("extrudeColor")).toString());
  const QString fillId = recipe.value(QStringLiteral("fill")).toString();
  const QString fillId2 = recipe.value(QStringLiteral("fill2")).toString();
  const QColor face = fillId.isEmpty() ? fallbackFill : QColor(fillId);

  // One ramp across the whole block, not per glyph: that is what makes a
  // two-colour style read as a single metal or flame surface.
  QBrush fillBrush(face);
  if (!fillId2.isEmpty()) {
    QLinearGradient gradient(blockRect.topLeft(),
                             QPointF(blockRect.left(), blockRect.bottom()));
    gradient.setColorAt(0.0, face);
    gradient.setColorAt(1.0, QColor(fillId2));
    fillBrush = QBrush(gradient);
  }

  for (const PlacedGlyph &pg : glyphs) {
    if (pg.opacity <= 0.004 || pg.text.trimmed().isEmpty())
      continue;
    QPainterPath path;
    path.addText(origin + pg.pen, font, pg.text);
    if (!qFuzzyCompare(pg.scale, 1.0) || pg.dx != 0.0 || pg.dy != 0.0) {
      // Scales about the glyph's own middle so a letter grows in place instead
      // of sliding out of the line while it grows.
      const QPointF centre = origin + pg.pen +
                             QPointF(pg.advance / 2.0, -fm.ascent() / 3.0);
      QTransform transform;
      transform.translate(centre.x() + pg.dx * em, centre.y() + pg.dy * em);
      transform.scale(pg.scale, pg.scale);
      transform.translate(-centre.x(), -centre.y());
      path = transform.map(path);
    }
    painter->setOpacity(pg.opacity);

    if (shadowDx != 0.0 || shadowDy != 0.0 || shadowBlur > 0.0) {
      const QPainterPath shadow =
          QTransform::fromTranslate(shadowDx, shadowDy).map(path);
      painter->setPen(Qt::NoPen);
      painter->setBrush(shadowColor);
      painter->drawPath(shadow);
      // Softness without a blur pass: a few widening strokes at falling alpha.
      // A real blur would mean an offscreen image per glyph per step.
      const int rings = shadowBlur > 0.0 ? 3 : 0;
      painter->setBrush(Qt::NoBrush);
      for (int i = 1; i <= rings; ++i) {
        QColor ring = shadowColor;
        ring.setAlphaF(shadowColor.alphaF() * 0.30 / i);
        QPen pen(ring, shadowBlur * 2.0 * i / rings);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->drawPath(shadow);
      }
    }

    if (extrude > 0.0) {
      const int steps = qBound(2, int(extrude / 1.2), 28);
      painter->setPen(Qt::NoPen);
      painter->setBrush(extrudeColor);
      for (int i = steps; i >= 1; --i) {
        const double offset = extrude * double(i) / steps;
        painter->drawPath(
            QTransform::fromTranslate(offset * 0.75, offset).map(path));
      }
    }

    if (glow > 0.0) {
      painter->setBrush(Qt::NoBrush);
      const int rings = 5;
      for (int i = rings; i >= 1; --i) {
        QColor ring = glowColor;
        ring.setAlphaF(0.16);
        QPen pen(ring, glow * 2.0 * i / rings);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->drawPath(path);
      }
    }

    if (outlineWidth > 0.0) {
      // Doubled: a centred stroke puts half its width inside the glyph, so this
      // is what leaves `outline` em standing outside the face.
      QPen pen(outlineColor, outlineWidth * 2.0);
      pen.setJoinStyle(Qt::RoundJoin);
      pen.setCapStyle(Qt::RoundCap);
      painter->setPen(pen);
      painter->setBrush(Qt::NoBrush);
      painter->drawPath(path);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(pg.color.isValid() ? QBrush(pg.color) : fillBrush);
    painter->drawPath(path);
  }
  painter->setOpacity(1.0);
}

}  // namespace

bool renderRaster(const QVariantList &segments, const QVariantMap &style,
                  int width, int height, QList<RasterSubtitle> *rendered,
                  QString *error) {
  if (!rendered || width < 2 || height < 2) {
    if (error) *error = QStringLiteral("Invalid subtitle render dimensions.");
    return false;
  }
  rendered->clear();
  const QString fontName = preferredFont(style.value("captionFontFamily").toString());
  const int requestedSize = qBound(1, style.value("captionFontSize", 42).toInt(), 400);
  const int basePixelSize = qMax(1, qRound((height / 540.0) * requestedSize));

  const QString outputDir = QDir(QDir(QDir::tempPath()).filePath(
      QStringLiteral("cutpro-khmer-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))).absolutePath();
  if (!QDir().mkpath(outputDir)) {
    if (error) *error = QStringLiteral("Could not create Khmer subtitle image directory.");
    return false;
  }
  // The caption box width the user set with the side handles, as a fraction of
  // the frame. Text wraps inside it, so the preview and the burned-in file agree.
  const double boxWidthFrac =
      qBound(0.1, style.value("captionBoxWidth", 0.9).toDouble(), 1.0);
  const int horizontalMargin = qMax(32, width / 30);
  const int maxTextWidth =
      qMax(1, qRound(width * boxWidthFrac) - horizontalMargin);
  const QString position = style.value("captionPosition", "bottom").toString();
  const QString alignment = style.value("captionAlignment", "center").toString();
  const QColor textColor(style.value("captionTextColor", "#ffffff").toString());
  QColor background(style.value("captionBackgroundColor", "#b3000000").toString());
  const bool backgroundVisible = style.value("captionBackgroundVisible", true).toBool();

  // Letter style and animation come from the same two tables the monitor overlay
  // reads, so what the preview plays is what lands in the file.
  const QVariantMap recipe =
      TextEffectCatalog::styleFor(style.value("captionTextEffect").toString());
  const QString animationId = style.value("captionAnimation", "none").toString();
  const double animationSpeed =
      style.value("captionAnimationSpeed", 1.0).toDouble();
  // An undecorated style keeps the plain black ring this renderer has always
  // drawn. A caption over unknown footage has to stay readable, and "None" is
  // what every project made before this feature carries.
  const bool undecorated = recipe.value("outline").toDouble() <= 0.0 &&
                           recipe.value("glow").toDouble() <= 0.0 &&
                           recipe.value("extrude").toDouble() <= 0.0 &&
                           recipe.value("shadowDx").toDouble() == 0.0 &&
                           recipe.value("shadowDy").toDouble() == 0.0;

  // Animating costs one image per step per cue where a static caption costs one
  // per cue, and the filtergraph carries an input and an overlay for every
  // image. A transcript runs to thousands of cues, so either the whole export
  // animates or none of it does - animating the first few hundred cues and
  // stopping would look like a bug.
  constexpr int kMaxImages = 600;
  constexpr double kStepMs = 60.0;
  constexpr int kMaxStepsPerCue = 12;
  int plannedImages = 0;
  for (const auto &value : segments) {
    const auto segment = value.toMap();
    const double start = segment.value("start").toDouble();
    const double end = segment.value("end").toDouble();
    const QString text = normalize(segment.value("text").toString());
    if (!std::isfinite(start) || !std::isfinite(end) || end <= start ||
        text.isEmpty())
      continue;
    const double cueMs = (end - start) * 1000.0;
    const double lead = std::min(
        TextEffectCatalog::leadInMs(animationId, text, animationSpeed, cueMs),
        cueMs);
    plannedImages +=
        lead <= 1.0
            ? 1
            : TextEffectCatalog::stepTimes(
                  animationId, text, animationSpeed, cueMs,
                  segment.value("wordTimes").toString(), kStepMs,
                  kMaxStepsPerCue)
                      .size() +
                  1;
  }
  const bool animate = plannedImages > 0 && plannedImages <= kMaxImages;

  int index = 0;
  for (const auto &value : segments) {
    const auto segment = value.toMap();
    const double start = segment.value("start").toDouble();
    const double end = segment.value("end").toDouble();
    const QString text = normalize(segment.value("text").toString());
    if (!std::isfinite(start) || !std::isfinite(end) || end <= start || text.isEmpty())
      continue;
    const double cueMs = (end - start) * 1000.0;
    // When each word of this cue is spoken, if anything knows: Whisper's word
    // timestamps, or a reading of the clip's own audio. Karaoke follows it
    // instead of dividing the cue evenly, so the burned-in highlight moves with
    // the speaker exactly as the monitor showed it.
    const QString wordTimes = segment.value("wordTimes").toString();

    QFont font(fontName);
    font.setPixelSize(basePixelSize);
    font.setBold(style.value("captionBold", true).toBool());
    font.setItalic(style.value("captionItalic", false).toBool());
    font.setStyleStrategy(QFont::PreferAntialias);

    // The settled state is laid out first because it is what decides the font
    // size that fits, the block size and the background box - none of which may
    // move while the animation plays. A step only changes transforms, and a
    // transform does not move a pen, so this layout is reused for every step.
    const QVariantMap settled = TextEffectCatalog::solve(
        text, animationId, animationSpeed, cueMs + 600000.0, cueMs, wordTimes);
    const QVariantList words = settled.value(QStringLiteral("words")).toList();
    QSizeF blockSize;
    QList<PlacedGlyph> layout =
        layoutCaption(words, font, maxTextWidth, alignment, &blockSize);
    // EP074 has a hardcoded English subtitle near the bottom. Keep a Khmer
    // caption to at most roughly three lines and reduce only long captions so
    // the translated text does not grow upward through the source subtitle.
    const int maxCaptionHeight = qMax(64, qRound(height * 0.23));
    int effectivePixelSize = basePixelSize;
    while (blockSize.height() > maxCaptionHeight && effectivePixelSize > 18) {
      effectivePixelSize -= 2;
      font.setPixelSize(effectivePixelSize);
      layout = layoutCaption(words, font, maxTextWidth, alignment, &blockSize);
    }
    applyStep(&layout, settled);

    const int textHeight =
        qMax(int(std::ceil(blockSize.height())), effectivePixelSize + 8);
    const int margin = qMax(24, qRound(height / 1080.0 * 80.0));
    // Reserve the lower 38% for hardcoded source subtitles when using the
    // normal bottom position. Explicit top/center/custom positions remain
    // fully respected.
    int y = qMax(margin, qRound(height * 0.58) - textHeight);
    if (position == "top") y = margin;
    else if (position == "center") y = qMax(0, (height - textHeight) / 2);
    else if (position == "custom") {
      const double normalizedY = qBound(0.0, segment.value("positionY", style.value("captionPositionY", 0.85)).toDouble(), 1.0);
      y = qBound(0, qRound(height * normalizedY - textHeight / 2.0), height - textHeight);
    }
    // The block hugs its text inside the full-width band the old renderer used,
    // so left/centre/right keep meaning what they did and a background box no
    // longer stretches the whole frame for three words.
    const double blockWidth = std::max(1.0, blockSize.width());
    double blockX = horizontalMargin + (maxTextWidth - blockWidth) / 2.0;
    if (alignment == QLatin1String("left"))
      blockX = horizontalMargin;
    else if (alignment == QLatin1String("right"))
      blockX = horizontalMargin + maxTextWidth - blockWidth;
    const QRectF blockRect(blockX, y, blockWidth, textHeight);

    // One recipe per cue: an undecorated style is turned into the legacy ring
    // here, once the font size that actually fits is known.
    QVariantMap effectiveRecipe = recipe;
    if (undecorated) {
      effectiveRecipe[QStringLiteral("outline")] =
          qMax(2.0, effectivePixelSize / 12.0) / double(effectivePixelSize);
      effectiveRecipe[QStringLiteral("outlineColor")] = QStringLiteral("#000000");
    }

    const double lead = std::min(
        TextEffectCatalog::leadInMs(animationId, text, animationSpeed, cueMs),
        cueMs);
    // Where the images go inside this cue. Evenly across the lead-in for an
    // animation that settles; on the word boundaries for karaoke, so an image
    // lands exactly when the highlight moves rather than a fifth of a second
    // either side of it.
    const QVector<double> stepStarts =
        (!animate || lead <= 1.0)
            ? QVector<double>{0.0}
            : TextEffectCatalog::stepTimes(animationId, text, animationSpeed,
                                           cueMs, wordTimes, kStepMs,
                                           kMaxStepsPerCue);
    const int steps = std::max(1, int(stepStarts.size()));
    // A karaoke-style animation runs for the whole cue, so there is nothing
    // settled to hold afterwards and no tail image to write.
    const bool settledTail = steps > 1 && lead < cueMs - 1.0;

    for (int step = 0; step < steps + (settledTail ? 1 : 0); ++step) {
      const bool tail = step >= steps;
      const double stepStart = tail ? lead : stepStarts.at(step);
      const double stepEnd = tail                 ? cueMs
                             : step + 1 < steps   ? stepStarts.at(step + 1)
                             : settledTail        ? lead
                                                  : cueMs;
      if (steps > 1)
        applyStep(&layout,
                  TextEffectCatalog::solve(text, animationId, animationSpeed,
                                           tail ? cueMs + 600000.0 : stepStart,
                                           cueMs, wordTimes));

      QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      QPainter painter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setRenderHint(QPainter::TextAntialiasing);
      if (backgroundVisible) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(blockRect.adjusted(-14, -8, 14, 8), 8, 8);
      }
      paintCaption(&painter, font, layout, blockRect.topLeft(), effectiveRecipe,
                   textColor.isValid() ? textColor : QColor(Qt::white),
                   blockRect, effectivePixelSize);
      painter.end();

      const QString path = QDir(outputDir).filePath(
          QStringLiteral("subtitle-%1.png").arg(index++));
      if (!image.save(path, "PNG")) {
        if (error) *error = QStringLiteral("Could not write Khmer subtitle image.");
        return false;
      }
      rendered->append(
          {path, start + stepStart / 1000.0, start + stepEnd / 1000.0});
    }
  }
  if (rendered->isEmpty()) {
    if (error) *error = QStringLiteral("No valid Khmer subtitle segments were rendered.");
    return false;
  }
  return true;
}

static QString assTime(double seconds) {
  const int centis = qMax(0, qRound(seconds * 100.0));
  return QString("%1:%2:%3.%4")
      .arg(centis / 360000, 1, 10, QLatin1Char('0'))
      .arg((centis / 6000) % 60, 2, 10, QLatin1Char('0'))
      .arg((centis / 100) % 60, 2, 10, QLatin1Char('0'))
      .arg(centis % 100, 2, 10, QLatin1Char('0'));
}

static QString assColor(const QString &value, const QString &fallback) {
  QColor color(value);
  if (!color.isValid()) color = QColor(fallback);
  return QString("&H%1%2%3%4")
      .arg(255 - color.alpha(), 2, 16, QLatin1Char('0'))
      .arg(color.blue(), 2, 16, QLatin1Char('0'))
      .arg(color.green(), 2, 16, QLatin1Char('0'))
      .arg(color.red(), 2, 16, QLatin1Char('0')).toUpper();
}

bool writeAss(const QString &path, const QVariantList &segments,
              const QVariantMap &style, int width, int height, QString *error) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = file.errorString();
    return false;
  }
  QTextStream out(&file);
  out.setEncoding(QStringConverter::Utf8);
  const QString font = preferredFont(style.value("captionFontFamily").toString());
  const int requestedSize =
      qBound(1, style.value("captionFontSize", 42).toInt(), 160);
  // Caption controls use a 720p visual reference. Scale that value into ASS
  // script pixels so portrait/1080p/4K exports match the preview instead of
  // becoming progressively smaller as output resolution increases.
  const int size = qBound(12, qRound(requestedSize * height / 720.0), 480);
  const QString position = style.value("captionPosition", "bottom").toString();
  const QString align = style.value("captionAlignment", "center").toString();
  int alignment = align == "left" ? 1 : align == "right" ? 3 : 2;
  if (position == "top") alignment += 6;
  else if (position == "center" || position == "custom") alignment += 3;
  const int marginV = position == "top" ? 28 : position == "center" ? height / 2 : 28;
  const bool bold = style.value("captionBold", true).toBool();
  const bool italic = style.value("captionItalic", false).toBool();
  const bool backgroundVisible =
      style.value("captionBackgroundVisible", true).toBool();
  out << "[Script Info]\nScriptType: v4.00+\nScaledBorderAndShadow: yes\nPlayResX: " << width
      << "\nPlayResY: " << height << "\n\n[V4+ Styles]\n"
      << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
         "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
         "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
         "Alignment, MarginL, MarginR, MarginV, Encoding\n"
      << "Style: Khmer," << font << "," << size << ","
      << assColor(style.value("captionTextColor", "#ffffff").toString(), "#ffffff")
      << ",&H000000FF,&H00000000,"
      << assColor(style.value("captionBackgroundColor", "#b3000000").toString(), "#b3000000")
      << "," << (bold ? -1 : 0) << "," << (italic ? -1 : 0)
      << ",0,0,100,100,0,0," << (backgroundVisible ? 3 : 1)
      << "," << (backgroundVisible ? 1 : 3) << ",0," << alignment
      << ",40,40," << marginV << ",1\n\n"
      << "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
  for (const auto &value : segments) {
    const auto segment = value.toMap();
    QString text = normalize(segment.value("text").toString());
    text.replace("\\", "\\\\");
    text.replace("\n", "\\N");
    QString override;
    if (position == "custom") {
      const int x = qRound(qBound(0.0, style.value("captionPositionX", 0.5).toDouble(), 1.0) * width);
      const int y = qRound(qBound(0.0, style.value("captionPositionY", 0.85).toDouble(), 1.0) * height);
      override = QString("{\\an%1\\pos(%2,%3)}").arg(alignment).arg(x).arg(y);
    }
    out << "Dialogue: 0," << assTime(segment.value("start").toDouble()) << ","
        << assTime(segment.value("end").toDouble())
        << ",Khmer,,0,0,0,," << override << text << "\n";
  }
  out.flush();
  if (!file.commit()) {
    if (error) *error = file.errorString();
    return false;
  }
  return true;
}
}
