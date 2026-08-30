#include "app/settings/app_settings.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSettings>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <memory>

namespace {
std::unique_ptr<QSettings> settingsStore() {
  const QString testFile = qEnvironmentVariable("CUTPRO_SETTINGS_FILE");
  if (!testFile.isEmpty())
    return std::make_unique<QSettings>(testFile, QSettings::IniFormat);
  return std::make_unique<QSettings>();
}

QString settingsDatabasePath() {
  const QString explicitPath = qEnvironmentVariable("CUTPRO_SETTINGS_DB").trimmed();
  if (!explicitPath.isEmpty())
    return explicitPath;
  const QString testFile = qEnvironmentVariable("CUTPRO_SETTINGS_FILE").trimmed();
  if (!testFile.isEmpty())
    return testFile + QStringLiteral(".sqlite");
  const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(root);
  return QDir(root).filePath(QStringLiteral("cutpro.sqlite"));
}

QVariant settingsValue(const QString &value) {
  // Rows in the SQLite store, which is now read once for migration and never
  // written. Two shapes exist there: a whole JSON document, and - from the builds
  // whose writes silently did nothing - an empty string. QJsonDocument wraps only
  // arrays and objects, so fromVariant() on an int, a bool or a string returned a
  // null document whose toJson() is empty; that is why the table has no usable
  // rows despite weeks of saves, and why an unreadable row must come back
  // invalid rather than as an empty value that would overwrite a real default.
  const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8());
  if (document.isArray()) {
    const QJsonArray array = document.array();
    return array.isEmpty() ? QVariant() : array.first().toVariant();
  }
  return document.isNull() ? QVariant() : document.toVariant();
}

bool openSettingsDatabase(QSqlDatabase *database, QString *connectionName,
                          QString *error) {
  const QString name = QStringLiteral("cutpro_settings_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
  db.setDatabaseName(settingsDatabasePath());
  if (!db.open()) {
    if (error) *error = db.lastError().text();
    QSqlDatabase::removeDatabase(name);
    return false;
  }
  QSqlQuery schema(db);
  if (!schema.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS app_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)"))) {
    if (error) *error = schema.lastError().text();
    db.close();
    QSqlDatabase::removeDatabase(name);
    return false;
  }
  *database = db;
  *connectionName = name;
  return true;
}

void closeSettingsDatabase(QSqlDatabase &database, const QString &connectionName) {
  database.close();
  database = QSqlDatabase();
  QSqlDatabase::removeDatabase(connectionName);
}

QString choice(const QVariantMap &values, const QString &key,
               const QStringList &allowed, const QString &fallback) {
  const QString value = values.value(key, fallback).toString();
  return allowed.contains(value) ? value : fallback;
}

int bounded(const QVariantMap &values, const QString &key, int fallback,
            int minimum, int maximum) {
  return qBound(minimum, values.value(key, fallback).toInt(), maximum);
}

bool removeDirectoryContents(const QString &path, QString *error) {
  const QFileInfo info(path);
  if (!info.exists())
    return true;
  QDir directory(info.absoluteFilePath());
  if (directory.removeRecursively())
    return true;
  if (error)
    *error = QStringLiteral("Could not clear cache folder: %1").arg(path);
  return false;
}
} // namespace

QVariantMap AppSettings::defaults() {
  return {{"startupWorkspace", "Edit"},
          {"startupLayout", "ESSENTIALS"},
          {"defaultMediaView", "list"},
          {"effectsBrowserView", "folder"},
          {"appearanceBrightness", 50},
          {"accentColor", "#4b8ff5"},
          {"masterVolume", 100},
          {"muteAllAudio", false},
          {"audioMeterRefreshMs", 110},
          {"autoSaveEnabled", true},
          {"autoSaveIntervalMinutes", 5},
          {"defaultImageDurationMs", 5000},
          {"loopPlayback", false},
          {"defaultVideoTracks", 1},
          {"defaultAudioTracks", 1},
          {"timelineTrackHeight", 68},
          {"timelineAutoScroll", true},
          {"timelineSnapping", true},
          // "tiny" is useful for quick previews but frequently hallucinates
          // short English phrases and misdetects dubbed Asian-language audio.
          // "small" is the default quality/performance balance.
          {"transcriptionModel", "small"},
          {"transcriptionLanguage", "auto"},
          {"translationProvider", "free"},
          {"translationModel", "gemini-2.0-flash"},
          {"translationBaseUrl", "https://tabitoken.com/v1"},
          {"translationApiKeys", ""},
          {"translationGeminiModel", "gemini-2.5-flash"},
          {"translationGeminiApiKeys", ""},
          {"translationTabitokenModel", "claude-opus-5"},
          {"translationTabitokenBaseUrl", "https://tabitoken.com/v1"},
          {"translationTabitokenApiKeys", ""},
          {"pythonExecutable", "python"}};
}

QVariantMap AppSettings::normalized(const QVariantMap &values) {
  const QVariantMap fallback = defaults();
  QVariantMap result;
  result["startupWorkspace"] =
      choice(values, "startupWorkspace", {"Import", "Edit", "Export"},
             fallback.value("startupWorkspace").toString());
  result["startupLayout"] =
      choice(values, "startupLayout", {"ESSENTIALS", "VERTICAL"},
             fallback.value("startupLayout").toString());
  result["defaultMediaView"] =
      choice(values, "defaultMediaView", {"list", "grid"}, "list");
  // Written by the effects panel's own layout dropdown rather than by a settings
  // page, so that the button the user pressed is what decides. An install saved
  // before the sidebar existed has no row for this, and choice() then falls back
  // to the tree that build shipped with.
  result["effectsBrowserView"] =
      choice(values, "effectsBrowserView", {"folder", "sidebar"}, "folder");
  result["appearanceBrightness"] =
      bounded(values, "appearanceBrightness", 50, 20, 80);
  const QString accent = values.value("accentColor", "#4b8ff5").toString();
  static const QRegularExpression colorPattern(
      QStringLiteral("^#[0-9a-fA-F]{6}$"));
  result["accentColor"] = colorPattern.match(accent).hasMatch()
                              ? accent.toLower()
                              : QStringLiteral("#4b8ff5");
  result["masterVolume"] = bounded(values, "masterVolume", 100, 0, 100);
  result["muteAllAudio"] = values.value("muteAllAudio", false).toBool();
  result["audioMeterRefreshMs"] =
      bounded(values, "audioMeterRefreshMs", 110, 50, 500);
  result["autoSaveEnabled"] =
      values.value("autoSaveEnabled", true).toBool();
  result["autoSaveIntervalMinutes"] =
      bounded(values, "autoSaveIntervalMinutes", 5, 1, 120);
  result["defaultImageDurationMs"] =
      bounded(values, "defaultImageDurationMs", 5000, 1000, 60000);
  // "pauseOnFocusLoss" was dropped: playback keeps running while another
  // application is in front, the way Premiere and CapCut behave. Values written
  // by older builds are simply ignored.
  result["loopPlayback"] = values.value("loopPlayback", false).toBool();
  result["defaultVideoTracks"] =
      bounded(values, "defaultVideoTracks", 1, 1, 16);
  result["defaultAudioTracks"] =
      bounded(values, "defaultAudioTracks", 1, 1, 16);
  result["timelineTrackHeight"] =
      bounded(values, "timelineTrackHeight", 68, 48, 96);
  result["timelineAutoScroll"] =
      values.value("timelineAutoScroll", true).toBool();
  result["timelineSnapping"] =
      values.value("timelineSnapping", true).toBool();
  result["transcriptionModel"] = choice(
      values, "transcriptionModel",
      {"tiny", "base", "small", "medium", "large-v3", "turbo"}, "small");
  result["transcriptionLanguage"] = choice(
      values, "transcriptionLanguage",
      {"auto", "en", "zh", "km", "es", "fr", "de", "ja", "ko", "vi"},
      "auto");
  result["translationProvider"] = choice(
      values, "translationProvider",
      {"free", "gemini", "openai_compatible"}, "free");
  // Keep the old shared fields as aliases for projects saved before the
  // provider sections were split. Dedicated values always win when present.
  const QString legacyModel = values.value("translationModel").toString().trimmed();
  const QString legacyBaseUrl = values.value("translationBaseUrl").toString().trimmed();
  const QString legacyKeys = values.value("translationApiKeys", values.value("translationApiKey", ""))
                                 .toString().trimmed();
  const QString geminiModel = values.value("translationGeminiModel").toString().trimmed();
  const QString tabitokenModel = values.value("translationTabitokenModel").toString().trimmed();
  const QString geminiKeys = values.value("translationGeminiApiKeys").toString().trimmed();
  const QString tabitokenKeys = values.value("translationTabitokenApiKeys").toString().trimmed();
  result["translationGeminiModel"] = (geminiModel.isEmpty()
                                          ? (legacyModel.startsWith("gemini-") ? legacyModel : QStringLiteral("gemini-2.5-flash"))
                                          : geminiModel).left(160);
  result["translationGeminiApiKeys"] = (geminiKeys.isEmpty() ? (legacyModel.startsWith("gemini-") ? legacyKeys : QString()) : geminiKeys).left(4000);
  result["translationTabitokenModel"] = (tabitokenModel.isEmpty()
                                              ? (legacyModel.startsWith("gemini-") || legacyModel.isEmpty() ? QStringLiteral("claude-opus-5") : legacyModel)
                                              : tabitokenModel).left(160);
  const QString tabitokenBaseUrl = values.value("translationTabitokenBaseUrl").toString().trimmed();
  result["translationTabitokenBaseUrl"] = (tabitokenBaseUrl.isEmpty() ? (legacyBaseUrl.isEmpty() ? QStringLiteral("https://tabitoken.com/v1") : legacyBaseUrl) : tabitokenBaseUrl).left(400);
  result["translationTabitokenApiKeys"] = (tabitokenKeys.isEmpty() ? (!legacyModel.startsWith("gemini-") ? legacyKeys : QString()) : tabitokenKeys).left(4000);
  // Legacy aliases are retained for older QML and project files.
  result["translationModel"] = result["translationProvider"].toString() == "gemini"
                                    ? result["translationGeminiModel"]
                                    : result["translationTabitokenModel"];
  result["translationBaseUrl"] = result["translationTabitokenBaseUrl"];
  result["translationApiKeys"] = result["translationProvider"].toString() == "gemini"
                                      ? result["translationGeminiApiKeys"]
                                      : result["translationTabitokenApiKeys"];
  const QString python = values.value("pythonExecutable", "python")
                             .toString()
                             .trimmed();
  result["pythonExecutable"] =
      python.isEmpty() ? QStringLiteral("python") : python;
  return result;
}

QVariantMap AppSettings::load() {
  const QVariantMap fallback = defaults();
  QVariantMap saved;

  // Read the registry first. It is where every value this app has ever saved
  // actually is, and reading it costs microseconds.
  //
  // SQLite used to come first, and the stall tracer caught what that cost: 450 ms
  // on the GUI thread inside QSqlDatabase::addDatabase, before any window exists,
  // spent by QFactoryLoader walking the plugin directories and canonicalising
  // every path it found. Paid on every launch, to read a table with no rows in
  // it. The driver is now loaded only on the one launch that has to migrate.
  auto settings = settingsStore();
  settings->beginGroup(QStringLiteral("Preferences"));
  const bool hasStoredSettings = !settings->allKeys().isEmpty();
  for (auto it = fallback.cbegin(); it != fallback.cend(); ++it)
    saved[it.key()] = settings->value(it.key(), it.value());
  // Migrate settings written before Gemini and Tabitoken had separate
  // sections. QSettings::contains distinguishes a legacy file from a new
  // file whose dedicated values are simply still at their defaults.
  if (!settings->contains(QStringLiteral("translationGeminiModel")) &&
      saved.value("translationModel").toString().startsWith("gemini-")) {
    saved["translationGeminiModel"] = saved.value("translationModel");
    saved["translationGeminiApiKeys"] = saved.value("translationApiKeys");
  }
  if (!settings->contains(QStringLiteral("translationTabitokenModel")) &&
      !saved.value("translationModel").toString().startsWith("gemini-") &&
      !saved.value("translationModel").toString().isEmpty()) {
    saved["translationTabitokenModel"] = saved.value("translationModel");
    saved["translationTabitokenApiKeys"] = saved.value("translationApiKeys");
  }
  if (!settings->contains(QStringLiteral("translationTabitokenBaseUrl")))
    saved["translationTabitokenBaseUrl"] = saved.value("translationBaseUrl");
  settings->endGroup();
  if (hasStoredSettings)
    return normalized(saved);

  // Nothing under Preferences: either a first run, or a machine whose settings an
  // older build wrote to the SQLite store. That is the only case worth loading
  // the driver for, and it happens once.
  QSqlDatabase database;
  QString connectionName;
  QString databaseError;
  if (openSettingsDatabase(&database, &connectionName, &databaseError)) {
    QSqlQuery query(database);
    QVariantMap stored;
    if (query.exec(QStringLiteral("SELECT key,value FROM app_settings"))) {
      while (query.next()) {
        const QVariant value = settingsValue(query.value(1).toString());
        if (value.isValid())
          stored[query.value(0).toString()] = value;
      }
    }
    closeSettingsDatabase(database, connectionName);
    if (!stored.isEmpty()) {
      for (auto it = stored.cbegin(); it != stored.cend(); ++it)
        saved[it.key()] = it.value();
      // Written back to the registry, so the next launch skips all of the above.
      QString ignored;
      save(saved, &ignored);
    }
  }
  return normalized(saved);
}

bool AppSettings::save(const QVariantMap &values, QString *error) {
  const QVariantMap clean = normalized(values);
  // Registry only. The SQLite write that used to come first has been removed:
  // it silently failed on every save - the serialiser above turned every scalar
  // into an empty string, so the table stayed empty - and the values were only
  // durable because this fallback ran afterwards. Loading the SQL driver to write
  // rows nothing reads cost a Qt plugin scan per settings change.
  auto store = settingsStore();
  store->beginGroup(QStringLiteral("Preferences"));
  for (auto it = clean.cbegin(); it != clean.cend(); ++it)
    store->setValue(it.key(), it.value());
  store->endGroup();
  store->sync();
  if (store->status() == QSettings::NoError)
    return true;
  if (error)
    *error = QStringLiteral("Could not save application settings.");
  return false;
}

QString AppSettings::cacheRoot() {
  QString root = qEnvironmentVariable("CUTPRO_CACHE_DIR");
  if (root.isEmpty())
    root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir::cleanPath(root);
}

qint64 AppSettings::cacheSizeBytes() {
  qint64 total = 0;
  const QStringList folders{"timeline-previews", "effect-previews"};
  for (const QString &folder : folders) {
    QDirIterator iterator(QDir(cacheRoot()).filePath(folder), QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
      total += QFileInfo(iterator.next()).size();
  }
  return total;
}

bool AppSettings::clearPreviewCache(QString *error) {
  const QDir root(cacheRoot());
  return removeDirectoryContents(root.filePath("timeline-previews"), error) &&
         removeDirectoryContents(root.filePath("effect-previews"), error);
}
