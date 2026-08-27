#pragma once

#include <QString>
#include <QVariantMap>

class ClipEffectsPipeline {
public:
  static QString videoFilters(const QVariantMap &effects);
  static QString overlayX(const QVariantMap &effects);
  static QString overlayY(const QVariantMap &effects);
  static QString audioFilters(const QVariantMap &effects, int channels);
};
