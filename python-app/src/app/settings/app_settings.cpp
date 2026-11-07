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

// A discovered model catalogue, normalised. Capped by entry count so a long
// list is shortened at a model boundary instead of mid-id.
QString modelList(const QString &value) {
  QStringList models;
  const QStringList parts =
      value.split(QRegularExpression(QStringLiteral("[,;\r\n]+")),
                  Qt::SkipEmptyParts);
  for (const QString &part : parts) {
    const QString name = part.trimmed().left(200);
    if (!name.isEmpty() && !models.contains(name))
      models.append(name);
    if (models.size() >= 400)
      break;
  }
  return models.join(QLatin1Char('\n'));
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

QString AppSettings::defaultWorkspaceLocation() {
  // Every project the workspace/home screen creates lands here, and a fresh
  // install needs a real folder from the first launch: without one the home
  // screen has nowhere to list and auto-save has no target for a new project.
  // Documents is where a user expects their own files, and it always exists.
  QString base =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (base.isEmpty())
    base = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  return QDir(base).filePath(QStringLiteral("Cut Pro"));
}

QVariantMap AppSettings::defaults() {
  return {{"startupWorkspace", "Home"},
          // UI language: "en" (source strings) or "km" (Khmer .qm catalogue).
          {"language", "en"},
          {"workspaceLocation", defaultWorkspaceLocation()},
          {"startupLayout", "ESSENTIALS"},
          {"defaultMediaView", "list"},
          {"effectsBrowserView", "folder"},
          {"appearanceBrightness", 50},
          {"accentColor", "#4b8ff5"},
          // External editor control is opt-in. The bearer key is DPAPI protected
          // in a dedicated MCP file, never in exported/imported app settings.
          {"mcpEnabled", false},
          {"mcpPort", 20129},
          {"masterVolume", 100},
          {"muteAllAudio", false},
          {"audioMeterRefreshMs", 110},
          // Voice-Over Record. deviceId empty means "follow the system default
          // microphone", which is the right behaviour for a USB mic that is not
          // always plugged in.
          {"voiceOverDeviceId", ""},
          {"voiceOverName", "Voiceover"},
          {"voiceOverSampleRate", 48000},
          {"voiceOverChannels", 1},
          {"voiceOverCountdownSec", 3},
          // Off by default: on headphones you want to hear the programme you are
          // narrating to. It is for recording on speakers, where the programme
          // would otherwise bleed into the microphone.
          {"voiceOverMuteMonitor", false},
          {"voiceOverPlayWhileRecording", true},
          // Hands-free: stay open and make a clip out of each burst of speech,
          // so a dozen lines take one button press rather than twelve.
          {"voiceOverHandsFree", true},
          // The level below which audio counts as silence, and how long that
          // silence has to last before a line is treated as finished.
          {"voiceOverSilenceDb", -42.0},
          {"voiceOverHangoverMs", 1200},
          // Voice applied to each take as it is placed. "none" leaves the
          // recording as it was spoken.
          {"voiceOverVoiceId", "none"},
          {"autoSaveEnabled", true},
          {"autoSaveIntervalMinutes", 5},
          // Cleared to false on a fresh install so main.qml shows the first-run
          // welcome / dependency check once, then set true when it is dismissed.
          {"onboardingComplete", false},
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
          // One Gemini model and source shared by transcription and translation.
          {"geminiModel", "gemini-2.5-flash"},
          {"geminiUsePool", true},
          // One saved work mode for transcription and translation. Keep the
          // existing parallel mode by default; off serializes each new job.
          {"geminiBulkProcessing", true},
          {"transcriptionGeminiModel", "gemini-2.5-flash"},
          // When on, a Gemini transcription is followed by a text-only recap
          // pass that rewrites the accurate lines into richer narration. An
          // app-level preference, so it stays on across new projects.
          {"transcriptionRecapMovie", false},
          // Longest subtitle line the worker will leave intact. It cuts at the
          // pauses in the word stream, so this is only the backstop for a speaker
          // who never stops.
          {"transcriptionMaxSegmentMs", 6200},
          // Vocal separation ("Remove Vocals"). Engine picks the tool; the
          // Demucs quality picks speed vs. cleanliness. App-level so the choice
          // sticks across new projects.
          //   vocalEngine: "demucs" (best quality, heavy) | "mdxnet" (light, fast on weak CPUs)
          //   vocalDemucsQuality: "fast" (htdemucs, 1 pass) | "clean" (htdemucs_ft, 4 passes)
          {"vocalEngine", "demucs"},
          {"vocalDemucsQuality", "fast"},
          {"translationProvider", "free"},
          {"translationModel", "gemini-2.0-flash"},
          {"translationBaseUrl", "https://tabitoken.com/v1"},
          {"translationApiKeys", ""},
          {"translationGeminiModel", "gemini-2.5-flash"},
          {"translationGeminiApiKeys", ""},
          {"translationTabitokenModel", "claude-opus-5"},
          {"translationTabitokenBaseUrl", "https://tabitoken.com/v1"},
          {"translationTabitokenApiKeys", ""},
          // Filled in by the settings page's model check: the catalogue the
          // endpoint published (newline separated) and the name derived from its
          // host. Empty means "never checked", and the interface then shows its
          // built-in list under the built-in name.
          {"translationTabitokenModels", ""},
          {"translationTabitokenLabel", ""},
          {"translationGeminiModels", ""},
          {"pythonExecutable", "python"},
          // User overrides for built-in keyboard shortcuts: a flat
          // id -> portable-text-sequence map (e.g. {"timeline.delete": "Del"}).
          // Empty by default; the canonical action table and its defaults live
          // in backend_shortcuts.cpp, and only ids the user actually rebound are
          // stored here. Must appear in both defaults() and normalized() or the
          // settings store would drop it on every save/load.
          {"keyboardShortcuts", QVariantMap{}}};
}

QVariantMap AppSettings::normalized(const QVariantMap &values) {
  const QVariantMap fallback = defaults();
  QVariantMap result;
  result["startupWorkspace"] =
      choice(values, "startupWorkspace", {"Home", "Import", "Edit", "Export"},
             fallback.value("startupWorkspace").toString());
  // UI language. Only "en"/"km" ship; anything else falls back to English.
  result["language"] = choice(values, "language", {"en", "km"}, "en");
  result["mcpEnabled"] = values.value("mcpEnabled", false).toBool();
  result["mcpPort"] = bounded(values, "mcpPort", 20129, 1024, 65535);
  // A folder path, not a fixed choice: trimmed and left as-is (empty falls back
  // to the default location). Backend resolves and mkpath's it on use.
  const QString workspace =
      values.value("workspaceLocation").toString().trimmed();
  result["workspaceLocation"] =
      workspace.isEmpty() ? defaultWorkspaceLocation() : workspace;
  result["startupLayout"] =
      choice(values, "startupLayout", {"ESSENTIALS", "VERTICAL", "EFFECT EDITOR"},
             fallback.value("startupLayout").toString());
  result["defaultMediaView"] =
      choice(values, "defaultMediaView", {"list", "grid"}, "list");
  // Written by the effects panel's own layout dropdown rather than by a settings
  // page, so that the button the user pressed is what decides. An install saved
  // before the sidebar existed has no row for this, and choice() then falls back
  // to the tree that build shipped with.
  result["effectsBrowserView"] =
      choice(values, "effectsBrowserView", {"folder", "sidebar"}, "folder");
  // 50 is the neutral midpoint; 100 is full brightness. The old ceiling of 80
  // capped the slider before it reached "full", so raise it to 100.
  result["appearanceBrightness"] =
      bounded(values, "appearanceBrightness", 50, 20, 100);
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
  result["voiceOverDeviceId"] =
      values.value("voiceOverDeviceId", QString()).toString();
  const QString voiceOverName =
      values.value("voiceOverName", QStringLiteral("Voiceover"))
          .toString()
          .trimmed();
  result["voiceOverName"] =
      voiceOverName.isEmpty() ? QStringLiteral("Voiceover") : voiceOverName;
  // 48 kHz matches the preview sink's format, so a take needs no resampling to
  // play back next to everything else.
  result["voiceOverSampleRate"] =
      bounded(values, "voiceOverSampleRate", 48000, 8000, 192000);
  result["voiceOverChannels"] =
      bounded(values, "voiceOverChannels", 1, 1, 2);
  result["voiceOverCountdownSec"] =
      bounded(values, "voiceOverCountdownSec", 3, 0, 10);
  result["voiceOverMuteMonitor"] =
      values.value("voiceOverMuteMonitor", false).toBool();
  result["voiceOverPlayWhileRecording"] =
      values.value("voiceOverPlayWhileRecording", true).toBool();
  result["voiceOverHandsFree"] =
      values.value("voiceOverHandsFree", true).toBool();
  result["voiceOverSilenceDb"] = qBound(
      -70.0, values.value("voiceOverSilenceDb", -42.0).toDouble(), -12.0);
  result["voiceOverHangoverMs"] =
      bounded(values, "voiceOverHangoverMs", 1200, 300, 10000);
  const QString voiceOverVoice =
      values.value("voiceOverVoiceId", QStringLiteral("none")).toString();
  result["voiceOverVoiceId"] =
      voiceOverVoice.isEmpty() ? QStringLiteral("none") : voiceOverVoice;
  result["autoSaveEnabled"] =
      values.value("autoSaveEnabled", true).toBool();
  result["onboardingComplete"] =
      values.value("onboardingComplete", false).toBool();
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
  // "gemini" is not a downloadable Whisper checkpoint - it routes the job to the
  // Gemini worker instead, and the concrete model name comes from
  // transcriptionGeminiModel. It has to be in this list or choice() would reset
  // the selection to "small" on the next save.
  result["transcriptionModel"] = choice(
      values, "transcriptionModel",
      {"tiny", "base", "small", "medium", "large-v3", "turbo", "gemini"},
      "small");
  const QString transcriptionGemini =
      values.value("transcriptionGeminiModel").toString().trimmed();
  result["transcriptionGeminiModel"] =
      (transcriptionGemini.isEmpty() ? QStringLiteral("gemini-2.5-flash")
                                     : transcriptionGemini)
          .left(160);
  result["transcriptionRecapMovie"] =
      values.value("transcriptionRecapMovie", false).toBool();
  result["transcriptionLanguage"] = choice(
      values, "transcriptionLanguage",
      {"auto", "en", "zh", "km", "es", "fr", "de", "ja", "ko", "vi"},
      "auto");
  result["transcriptionMaxSegmentMs"] =
      bounded(values, "transcriptionMaxSegmentMs", 6200, 2000, 15000);
  result["vocalEngine"] =
      choice(values, "vocalEngine", {"demucs", "mdxnet"}, "demucs");
  result["vocalDemucsQuality"] =
      choice(values, "vocalDemucsQuality", {"fast", "clean"}, "fast");
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
  QString sharedGemini = values.value("geminiModel").toString().trimmed();
  if (sharedGemini.isEmpty())
    sharedGemini = transcriptionGemini.isEmpty() ? result.value("translationGeminiModel").toString() : transcriptionGemini;
  sharedGemini = sharedGemini.left(160);
  result["geminiModel"] = sharedGemini;
  result["transcriptionGeminiModel"] = sharedGemini;
  result["translationGeminiModel"] = sharedGemini;
  result["geminiUsePool"] = values.value("geminiUsePool", true).toBool();
  result["geminiBulkProcessing"] = values.value("geminiBulkProcessing", true).toBool();
  result["translationGeminiApiKeys"] = (geminiKeys.isEmpty() ? (legacyModel.startsWith("gemini-") ? legacyKeys : QString()) : geminiKeys).left(4000);
  result["translationTabitokenModel"] = (tabitokenModel.isEmpty()
                                              ? (legacyModel.startsWith("gemini-") || legacyModel.isEmpty() ? QStringLiteral("claude-opus-5") : legacyModel)
                                              : tabitokenModel).left(160);
  const QString tabitokenBaseUrl = values.value("translationTabitokenBaseUrl").toString().trimmed();
  result["translationTabitokenBaseUrl"] = (tabitokenBaseUrl.isEmpty() ? (legacyBaseUrl.isEmpty() ? QStringLiteral("https://tabitoken.com/v1") : legacyBaseUrl) : tabitokenBaseUrl).left(400);
  result["translationTabitokenApiKeys"] = (tabitokenKeys.isEmpty() ? (!legacyModel.startsWith("gemini-") ? legacyKeys : QString()) : tabitokenKeys).left(4000);
  // Discovered catalogues. Capped by entry count rather than by string length:
  // a gateway can publish hundreds of models, and cutting the text at a byte
  // offset would leave a half-written model id as the last choice in the list.
  result["translationTabitokenModels"] =
      modelList(values.value("translationTabitokenModels").toString());
  result["translationGeminiModels"] =
      modelList(values.value("translationGeminiModels").toString());
  result["translationTabitokenLabel"] =
      values.value("translationTabitokenLabel").toString().trimmed().left(60);
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
  // Keyboard-shortcut overrides. Light structural validation only: keep string
  // keys mapped to non-empty string values, with sane length caps so a corrupt
  // store can't bloat the file. Deep validation (is this a real action id? is
  // the sequence parseable?) belongs to Backend, which owns the action table
  // and QKeySequence; here we only guarantee a clean string->string map.
  const QVariantMap rawShortcuts =
      values.value("keyboardShortcuts").toMap();
  QVariantMap shortcuts;
  for (auto it = rawShortcuts.cbegin(); it != rawShortcuts.cend(); ++it) {
    const QString key = it.key().trimmed();
    const QString value = it.value().toString().trimmed();
    if (key.isEmpty() || value.isEmpty() || key.size() > 64 ||
        value.size() > 64)
      continue;
    shortcuts.insert(key.left(64), value.left(64));
  }
  result["keyboardShortcuts"] = shortcuts;
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
  // Older installs chose the transcription model independently. Preserve that
  // choice while migrating to the shared field instead of inserting a default.
  if (!settings->contains(QStringLiteral("geminiModel"))) {
    saved.remove(QStringLiteral("geminiModel"));
    if (!settings->contains(QStringLiteral("transcriptionGeminiModel")))
      saved.remove(QStringLiteral("transcriptionGeminiModel"));
  }
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

QString AppSettings::generatedSpeechRoot() {
  // Where Backend::generateTimedTextToSpeech() tells the worker to write. It is not
  // under cacheRoot() and it is not small: one mp3 per distinct subtitle line, so a
  // feature-length track leaves tens of thousands of files behind. It is cache in
  // every sense that matters - content-addressed, reproducible, and reused only as
  // a resume - so Clear cache has to be able to reach it.
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::AppDataLocation))
      .filePath(QStringLiteral("generated-speech"));
}

qint64 AppSettings::cacheSizeBytes() {
  qint64 total = 0;
  const QStringList folders{QDir(cacheRoot()).filePath("timeline-previews"),
                            QDir(cacheRoot()).filePath("effect-previews"),
                            generatedSpeechRoot()};
  for (const QString &folder : folders) {
    QDirIterator iterator(folder, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext())
      total += QFileInfo(iterator.next()).size();
  }
  return total;
}

int AppSettings::generatedSpeechFileCount() {
  int count = 0;
  QDirIterator iterator(generatedSpeechRoot(), QDir::Files,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    ++count;
  }
  return count;
}

bool AppSettings::clearPreviewCache(QString *error) {
  const QDir root(cacheRoot());
  return removeDirectoryContents(root.filePath("timeline-previews"), error) &&
         removeDirectoryContents(root.filePath("effect-previews"), error) &&
         removeDirectoryContents(generatedSpeechRoot(), error);
}
