#pragma once

#include <QVariantList>
#include <QVariantMap>

class EffectRegistry {
public:
  static QVariantList definitions();
  static QVariantMap definition(const QString &effectId);
  static bool supportsClip(const QVariantMap &definition,
                           const QString &clipKind, bool hasAudio);
};
