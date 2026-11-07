#pragma once

#include <QString>
#include <QVariantMap>

class AppSettings {
public:
  static QVariantMap defaults();
  static QVariantMap normalized(const QVariantMap &values);
  // Where the workspace/home screen stores projects by default: <Documents>/Cut Pro.
  static QString defaultWorkspaceLocation();
  static QVariantMap load();
  static bool save(const QVariantMap &values, QString *error = nullptr);

  static QString cacheRoot();
  // Generated subtitle voices. Counted and cleared with the preview cache, and
  // separately countable because "24112 voice files" is the part of a cache figure
  // a user is entitled to see before pressing Clear.
  static QString generatedSpeechRoot();
  static int generatedSpeechFileCount();
  // Both of these walk tens of thousands of files. Neither belongs on the GUI
  // thread; backend_settings.cpp runs them on a pool.
  static qint64 cacheSizeBytes();
  static bool clearPreviewCache(QString *error = nullptr);
};
