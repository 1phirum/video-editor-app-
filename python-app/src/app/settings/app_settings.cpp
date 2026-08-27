#include "app/settings/app_settings.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
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

QString settingsJson(const QVariant &value) {
  return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

QVariant settingsValue(const QString &value) {
  return QJsonDocument::fromJson(value.toUtf8()).toVariant();
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
          {"appearanceBrightness", 50},
          {"accentColor", "#4b8ff5"},
          {"masterVolume", 100},
          {"muteAllAudio", false},
          {"audioMeterRefreshMs", 110},
          {"autoSaveEnabled", true},
          {"autoSaveIntervalMinutes", 5},
          {"defaultImageDurationMs", 5000},
          {"pauseOnFocusLoss", true},
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
  result["pauseOnFocusLoss"] =
      values.value("pauseOnFocusLoss", true).toBool();
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

  QSqlDatabase database;
  QString connectionName;
  QString databaseError;
  if (openSettingsDatabase(&database, &connectionName, &databaseError)) {
    QSqlQuery query(database);
    if (query.exec(QStringLiteral("SELECT key,value FROM app_settings"))) {
      while (query.next())
        saved[query.value(0).toString()] = settingsValue(query.value(1).toString());
    }
    const bool hasStoredSettings = !saved.isEmpty();
    if (!hasStoredSettings) {
      auto legacy = settingsStore();
      legacy->beginGroup(QStringLiteral("Preferences"));
      for (auto it = fallback.cbegin(); it != fallback.cend(); ++it)
        saved[it.key()] = legacy->value(it.key(), it.value());
      legacy->endGroup();
      QSqlQuery insert(database);
      insert.prepare(QStringLiteral("INSERT OR REPLACE INTO app_settings(key,value) VALUES(:key,:value)"));
      for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
        insert.bindValue(":key", it.key());
        insert.bindValue(":value", settingsJson(it.value()));
        insert.exec();
      }
    }
    closeSettingsDatabase(database, connectionName);
    return normalized(saved);
  }

  // Keep the legacy INI fallback available if SQLite cannot be opened.
  auto settings = settingsStore();
  settings->beginGroup(QStringLiteral("Preferences"));
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
  return normalized(saved);
}

bool AppSettings::save(const QVariantMap &values, QString *error) {
  const QVariantMap clean = normalized(values);
  QSqlDatabase database;
  QString connectionName;
  QString databaseError;
  if (openSettingsDatabase(&database, &connectionName, &databaseError)) {
    const bool transactionStarted = database.transaction();
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO app_settings(key,value) VALUES(:key,:value)"));
    bool ok = transactionStarted;
    for (auto it = clean.cbegin(); ok && it != clean.cend(); ++it) {
      query.bindValue(":key", it.key());
      query.bindValue(":value", settingsJson(it.value()));
      ok = query.exec();
    }
    if (ok) ok = database.commit();
    if (!ok) databaseError = query.lastError().text();
    closeSettingsDatabase(database, connectionName);
    if (ok) return true;
  }

  // SQLite is the durable primary store, but preserve compatibility with
  // environments where the Qt SQLite driver is not deployed yet.
  auto legacy = settingsStore();
  legacy->beginGroup(QStringLiteral("Preferences"));
  for (auto it = clean.cbegin(); it != clean.cend(); ++it)
    legacy->setValue(it.key(), it.value());
  legacy->endGroup();
  legacy->sync();
  if (legacy->status() == QSettings::NoError)
    return true;
  if (error)
    *error = QStringLiteral("Could not save application settings to SQLite or legacy storage: %1")
                 .arg(databaseError);
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
