#include "app/caption_style.h"

#include <QtGlobal>

QJsonObject CaptionStyle::toJson() const {
  return {{QStringLiteral("fontFamily"), fontFamily},
          {QStringLiteral("fontSize"), fontSize},
          {QStringLiteral("textColor"), textColor},
          {QStringLiteral("bold"), bold},
          {QStringLiteral("italic"), italic},
          {QStringLiteral("backgroundVisible"), backgroundVisible},
          {QStringLiteral("backgroundColor"), backgroundColor},
          {QStringLiteral("position"), position},
          {QStringLiteral("alignment"), alignment},
          {QStringLiteral("positionX"), positionX},
          {QStringLiteral("positionY"), positionY},
          {QStringLiteral("blurEnabled"), blurEnabled},
          {QStringLiteral("blurTrackingEnabled"), blurTrackingEnabled},
          {QStringLiteral("blurRegionX"), blurRegionX},
          {QStringLiteral("blurRegionY"), blurRegionY},
          {QStringLiteral("blurRegionWidth"), blurRegionWidth},
          {QStringLiteral("blurRegionHeight"), blurRegionHeight},
          {QStringLiteral("blurStrength"), blurStrength},
          {QStringLiteral("blurPadding"), blurPadding}};
}

CaptionStyle CaptionStyle::fromJson(const QJsonObject &object) {
  CaptionStyle style;
  style.fontFamily =
      object.value("fontFamily").toString(style.fontFamily).trimmed();
  if (style.fontFamily.isEmpty())
    style.fontFamily = QStringLiteral("Arial");
  style.fontSize =
      qBound(12, object.value("fontSize").toInt(style.fontSize), 120);
  style.textColor = object.value("textColor").toString(style.textColor);
  style.bold = object.value("bold").toBool(style.bold);
  style.italic = object.value("italic").toBool(style.italic);
  style.backgroundVisible =
      object.value("backgroundVisible").toBool(style.backgroundVisible);
  style.backgroundColor =
      object.value("backgroundColor").toString(style.backgroundColor);
  const QString position = object.value("position").toString(style.position);
  if (position == "top" || position == "center" || position == "bottom" ||
      position == "custom")
    style.position = position;
  const QString alignment = object.value("alignment").toString(style.alignment);
  if (alignment == "left" || alignment == "center" || alignment == "right")
    style.alignment = alignment;
  style.positionX =
      qBound(0.0, object.value("positionX").toDouble(style.positionX), 1.0);
  style.positionY =
      qBound(0.0, object.value("positionY").toDouble(style.positionY), 1.0);
  style.blurEnabled =
      object.value("blurEnabled").toBool(style.blurEnabled);
  style.blurTrackingEnabled = object.value("blurTrackingEnabled")
                                  .toBool(style.blurTrackingEnabled);
  style.blurRegionX =
      qBound(0.0, object.value("blurRegionX").toDouble(style.blurRegionX), 1.0);
  style.blurRegionY =
      qBound(0.0, object.value("blurRegionY").toDouble(style.blurRegionY), 1.0);
  style.blurRegionWidth = qBound(
      0.05, object.value("blurRegionWidth").toDouble(style.blurRegionWidth), 1.0);
  style.blurRegionHeight = qBound(
      0.05, object.value("blurRegionHeight").toDouble(style.blurRegionHeight), 1.0);
  style.blurRegionX = qMin(style.blurRegionX, 1.0 - style.blurRegionWidth);
  style.blurRegionY = qMin(style.blurRegionY, 1.0 - style.blurRegionHeight);
  style.blurStrength =
      qBound(1, object.value("blurStrength").toInt(style.blurStrength), 64);
  style.blurPadding =
      qBound(0, object.value("blurPadding").toInt(style.blurPadding), 64);
  return style;
}
