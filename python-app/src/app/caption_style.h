#pragma once

#include <QJsonObject>
#include <QString>

struct CaptionStyle {
  QString fontFamily = QStringLiteral("Arial");
  int fontSize = 42;
  // Width of the caption box as a fraction of the frame width. The side handles
  // set it; the text wraps inside it. Corner handles change fontSize, not this.
  double boxWidth = 0.9;
  QString textColor = QStringLiteral("#ffffff");
  bool bold = true;
  bool italic = false;
  bool backgroundVisible = true;
  QString backgroundColor = QStringLiteral("#b3000000");
  QString position = QStringLiteral("bottom");
  QString alignment = QStringLiteral("center");
  double positionX = 0.5;
  double positionY = 0.85;
  bool blurEnabled = false;
  bool blurTrackingEnabled = true;
  double blurRegionX = 0.30;
  double blurRegionY = 0.35;
  double blurRegionWidth = 0.40;
  double blurRegionHeight = 0.30;
  int blurStrength = 32;
  int blurPadding = 10;

  // Letter style and caption animation, ids from TextEffectCatalog. Kept here
  // with the rest of the caption look so one project key carries all of it.
  QString textEffect = QStringLiteral("none");
  QString animation = QStringLiteral("none");
  double animationSpeed = 1.0;  // divides every duration in the recipe

  QJsonObject toJson() const;
  static CaptionStyle fromJson(const QJsonObject &object);
};
