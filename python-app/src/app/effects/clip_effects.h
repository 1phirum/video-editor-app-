#pragma once

#include <QVariantMap>
#include <QStringList>

// Per-clip inspector values shared by the QML context menu and the FFmpeg
// export builder. Keeping validation here prevents invalid UI values from
// leaking into filter expressions.
class ClipEffects {
public:
  static QVariantMap defaults();
  static bool setValue(QVariantMap *effects, const QString &key,
                       const QVariant &value);
};
