#include "app/core_app/backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

bool Backend::downloadCaptionFont(const QString &family) {
  static const QHash<QString, QUrl> urls{
      {QStringLiteral("Roboto"),
       QUrl(QStringLiteral("https://github.com/google/fonts/raw/main/ofl/"
                           "roboto/Roboto%5Bwght%5D.ttf"))},
      {QStringLiteral("Open Sans"),
       QUrl(QStringLiteral("https://github.com/google/fonts/raw/main/ofl/"
                           "opensans/OpenSans%5Bwght%5D.ttf"))},
      {QStringLiteral("Lato"),
       QUrl(QStringLiteral("https://github.com/google/fonts/raw/main/ofl/lato/"
                           "Lato%5Bwght%5D.ttf"))}};
  if (!urls.contains(family) || m_downloadedCaptionFonts.contains(family))
    return m_downloadedCaptionFonts.contains(family);

  const QString root =
      QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
          .filePath(QStringLiteral("fonts"));
  QDir().mkpath(root);
  const QString path =
      QDir(root).filePath(family.toLower().replace(' ', '-') + ".ttf");
  QNetworkReply *reply =
      m_fontNetwork.get(QNetworkRequest(urls.value(family)));
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, path, family]() {
            const QByteArray data =
                reply->error() == QNetworkReply::NoError ? reply->readAll()
                                                         : QByteArray();
            reply->deleteLater();
            if (data.isEmpty()) {
              setError(QStringLiteral("Could not download font: %1")
                           .arg(family));
              return;
            }
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) ||
                file.write(data) != data.size()) {
              setError(
                  QStringLiteral("Could not save font: %1").arg(family));
              return;
            }
            file.close();
            if (QFontDatabase::addApplicationFont(path) < 0) {
              setError(QStringLiteral("Downloaded font is invalid: %1")
                           .arg(family));
              return;
            }
            m_downloadedCaptionFonts << family;
            emit captionFontsChanged();
          });
  return true;
}

void Backend::setCaptionFontFamily(const QString &family) {
  const QString value = family.trimmed();
  if (value.isEmpty() || value == m_captionStyle.fontFamily)
    return;
  m_captionStyle.fontFamily = value;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionFontSize(int size) {
  const int value = qBound(12, size, 120);
  if (value == m_captionStyle.fontSize)
    return;
  m_captionStyle.fontSize = value;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionTextColor(const QString &color) {
  if (color.trimmed().isEmpty() || color == m_captionStyle.textColor)
    return;
  m_captionStyle.textColor = color;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBold(bool bold) {
  if (bold == m_captionStyle.bold)
    return;
  m_captionStyle.bold = bold;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionItalic(bool italic) {
  if (italic == m_captionStyle.italic)
    return;
  m_captionStyle.italic = italic;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBackgroundVisible(bool visible) {
  if (visible == m_captionStyle.backgroundVisible)
    return;
  m_captionStyle.backgroundVisible = visible;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBackgroundColor(const QString &color) {
  if (color.trimmed().isEmpty() || color == m_captionStyle.backgroundColor)
    return;
  m_captionStyle.backgroundColor = color;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionPosition(const QString &position) {
  if ((position != "top" && position != "center" && position != "bottom" &&
       position != "custom") ||
      position == m_captionStyle.position)
    return;
  m_captionStyle.position = position;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionAlignment(const QString &alignment) {
  if ((alignment != "left" && alignment != "center" &&
       alignment != "right") ||
      alignment == m_captionStyle.alignment)
    return;
  m_captionStyle.alignment = alignment;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurEnabled(bool enabled) {
  if (enabled == m_captionStyle.blurEnabled)
    return;
  m_captionStyle.blurEnabled = enabled;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurTrackingEnabled(bool enabled) {
  if (enabled == m_captionStyle.blurTrackingEnabled)
    return;
  m_captionStyle.blurTrackingEnabled = enabled;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurRegionNormalized(double x, double y, double width,
                                             double height) {
  const double boundedWidth = qBound(0.05, width, 1.0);
  const double boundedHeight = qBound(0.05, height, 1.0);
  const double boundedX = qBound(0.0, x, 1.0 - boundedWidth);
  const double boundedY = qBound(0.0, y, 1.0 - boundedHeight);
  if (qFuzzyCompare(m_captionStyle.blurRegionX, boundedX) &&
      qFuzzyCompare(m_captionStyle.blurRegionY, boundedY) &&
      qFuzzyCompare(m_captionStyle.blurRegionWidth, boundedWidth) &&
      qFuzzyCompare(m_captionStyle.blurRegionHeight, boundedHeight))
    return;
  m_captionStyle.blurRegionX = boundedX;
  m_captionStyle.blurRegionY = boundedY;
  m_captionStyle.blurRegionWidth = boundedWidth;
  m_captionStyle.blurRegionHeight = boundedHeight;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurStrength(int strength) {
  const int value = qBound(1, strength, 64);
  if (value == m_captionStyle.blurStrength)
    return;
  m_captionStyle.blurStrength = value;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurPadding(int padding) {
  const int value = qBound(0, padding, 64);
  if (value == m_captionStyle.blurPadding)
    return;
  m_captionStyle.blurPadding = value;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionPositionNormalized(double x, double y) {
  const double boundedX = qBound(0.0, x, 1.0);
  const double boundedY = qBound(0.0, y, 1.0);
  if (m_captionStyle.position == "custom" &&
      qFuzzyCompare(m_captionStyle.positionX, boundedX) &&
      qFuzzyCompare(m_captionStyle.positionY, boundedY))
    return;
  m_captionStyle.position = QStringLiteral("custom");
  m_captionStyle.positionX = boundedX;
  m_captionStyle.positionY = boundedY;
  markDirty();
  emit captionStyleChanged();
}
