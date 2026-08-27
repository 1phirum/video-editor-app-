#pragma once

#include <QJsonObject>
#include <QString>

struct CaptionStyle {
  QString fontFamily = QStringLiteral("Arial");
  int fontSize = 42;
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

  QJsonObject toJson() const;
  static CaptionStyle fromJson(const QJsonObject &object);
};
