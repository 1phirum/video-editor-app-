#pragma once

#include <QString>
#include <QVariantList>

class AudioEffectPipeline {
public:
  static QString filters(const QVariantList &effectStack, int channels);
};
