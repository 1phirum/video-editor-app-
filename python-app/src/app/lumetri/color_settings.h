#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

// Keeps Lumetri defaults and input bounds out of the QML and Backend glue.
// Project settings are shared by the sequence; correction settings belong to
// an individual timeline clip so separate uses of the same media can differ.
class ColorSettings {
public:
  static QVariantMap defaults();
  static QVariantMap clipDefaults();
  static bool setProjectValue(QVariantMap *settings, const QString &key,
                              const QVariant &value);
  static bool setClipValue(QVariantMap *settings, const QString &key,
                           const QVariant &value);
  static bool setMediaValue(QVariantMap *settings, const QString &key,
                            const QVariant &value);
};
