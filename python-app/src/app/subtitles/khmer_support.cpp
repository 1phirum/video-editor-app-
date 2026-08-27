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
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QUuid>
#include <algorithm>
#include <cmath>

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

bool renderRaster(const QVariantList &segments, const QVariantMap &style,
                  int width, int height, QList<RasterSubtitle> *rendered,
                  QString *error) {
  if (!rendered || width < 2 || height < 2) {
    if (error) *error = QStringLiteral("Invalid subtitle render dimensions.");
    return false;
  }
  rendered->clear();
  const QString fontName = preferredFont(style.value("captionFontFamily").toString());
  const int requestedSize = qBound(12, style.value("captionFontSize", 42).toInt(), 160);
  const int basePixelSize = qMax(12, qRound((height / 540.0) * requestedSize));

  const QString outputDir = QDir(QDir(QDir::tempPath()).filePath(
      QStringLiteral("cutpro-khmer-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))).absolutePath();
  if (!QDir().mkpath(outputDir)) {
    if (error) *error = QStringLiteral("Could not create Khmer subtitle image directory.");
    return false;
  }
  const int horizontalMargin = qMax(32, width / 30);
  const int maxTextWidth = qMax(1, width - horizontalMargin * 2);
  const QString position = style.value("captionPosition", "bottom").toString();
  const QString alignment = style.value("captionAlignment", "center").toString();
  const int textFlags = (alignment == "left" ? Qt::AlignLeft
                         : alignment == "right" ? Qt::AlignRight
                                                  : Qt::AlignHCenter) |
                        Qt::AlignTop | Qt::TextWordWrap;
  const QColor textColor(style.value("captionTextColor", "#ffffff").toString());
  QColor background(style.value("captionBackgroundColor", "#b3000000").toString());
  const bool backgroundVisible = style.value("captionBackgroundVisible", true).toBool();

  int index = 0;
  for (const auto &value : segments) {
    const auto segment = value.toMap();
    const double start = segment.value("start").toDouble();
    const double end = segment.value("end").toDouble();
    const QString text = normalize(segment.value("text").toString());
    if (!std::isfinite(start) || !std::isfinite(end) || end <= start || text.isEmpty())
      continue;

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font(fontName);
    font.setPixelSize(basePixelSize);
    font.setBold(style.value("captionBold", true).toBool());
    font.setItalic(style.value("captionItalic", false).toBool());
    font.setStyleStrategy(QFont::PreferAntialias);
    painter.setFont(font);
    const QRect measure(horizontalMargin, 0, maxTextWidth, height);
    QRect bounds = painter.boundingRect(measure, textFlags, text);
    // EP074 has a hardcoded English subtitle near the bottom. Keep a Khmer
    // caption to at most roughly three lines and reduce only long captions so
    // the translated text does not grow upward through the source subtitle.
    const int maxCaptionHeight = qMax(64, qRound(height * 0.23));
    int effectivePixelSize = basePixelSize;
    while (bounds.height() > maxCaptionHeight && effectivePixelSize > 18) {
      effectivePixelSize -= 2;
      font.setPixelSize(effectivePixelSize);
      painter.setFont(font);
      bounds = painter.boundingRect(measure, textFlags, text);
    }
    const int textHeight = qMax(bounds.height(), effectivePixelSize + 8);
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
    const QRect target(horizontalMargin, y, maxTextWidth, textHeight);
    if (backgroundVisible) {
      const QRect box = target.adjusted(-12, -8, 12, 8);
      painter.setPen(Qt::NoPen);
      painter.setBrush(background);
      painter.drawRoundedRect(box, 8, 8);
    }
    const int strokeWidth = qMax(2, effectivePixelSize / 12);
    painter.setPen(Qt::black);
    for (int dx = -strokeWidth; dx <= strokeWidth; dx += qMax(1, strokeWidth / 3))
      for (int dy = -strokeWidth; dy <= strokeWidth; dy += qMax(1, strokeWidth / 3))
        if (dx * dx + dy * dy <= strokeWidth * strokeWidth && (dx || dy))
          painter.drawText(target.translated(dx, dy), textFlags, text);
    painter.setPen(textColor.isValid() ? textColor : QColor(Qt::white));
    painter.drawText(target, textFlags, text);
    painter.end();

    const QString path = QDir(outputDir).filePath(QStringLiteral("subtitle-%1.png").arg(index++));
    if (!image.save(path, "PNG")) {
      if (error) *error = QStringLiteral("Could not write Khmer subtitle image.");
      return false;
    }
    rendered->append({path, start, end});
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
      qBound(12, style.value("captionFontSize", 42).toInt(), 160);
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
