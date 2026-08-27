#pragma once

#include <QString>
#include <QVariantMap>

class AppSettings {
public:
  static QVariantMap defaults();
  static QVariantMap normalized(const QVariantMap &values);
  static QVariantMap load();
  static bool save(const QVariantMap &values, QString *error = nullptr);

  static QString cacheRoot();
  static qint64 cacheSizeBytes();
  static bool clearPreviewCache(QString *error = nullptr);
};
