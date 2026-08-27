#pragma once

#include <QString>
#include <QVariantList>

class VideoEffectPipeline {
public:
  static QString filters(const QVariantList &effectStack);
};
