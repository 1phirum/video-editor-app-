#include "app/core_app/project_database.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

// Each Backend instance gets its own named connection so multiple instances
// (e.g. in tests) do not share a single global QSqlDatabase.
static int s_connectionCounter = 0;

ProjectDatabase::ProjectDatabase(QObject *parent)
    : QObject(parent),
      m_connectionName(QStringLiteral("cutpro_db_%1").arg(++s_connectionCounter)) {}

ProjectDatabase::~ProjectDatabase() { close(); }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static QSqlDatabase db(const QString &name) {
  return QSqlDatabase::database(name, false);
}

static QString toJson(const QVariant &v) {
  const QString json = QString::fromUtf8(
      QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact));
  // An invalid variant makes an invalid document, whose toJson() is a null
  // QByteArray and therefore a null QString - which binds as SQL NULL. Every
  // column these go into is TEXT NOT NULL DEFAULT '{}', so that is the fallback.
  return json.isEmpty() ? QStringLiteral("{}") : json;
}

// Text for a NOT NULL column. QVariantMap::value() returns an invalid QVariant
// for a key that was never set, and an unset QString member is null; either one
// reaches SQLite as NULL rather than as '', and the whole statement then fails
// the column's NOT NULL constraint. `projects.location` was where this surfaced:
// a project that had not been given a folder yet could not write its row at all,
// so the entire save transaction rolled back on every autosave. `fallback`
// mirrors the column's own DEFAULT, so a missing value lands on what the schema
// already promised. QString("") is empty but not null, which is the distinction
// that matters here.
static QString sqlText(const QVariant &v, const QString &fallback = QString("")) {
  const QString text = v.toString();
  return text.isNull() ? fallback : text;
}

static QVariant fromJson(const QString &s) {
  return QJsonDocument::fromJson(s.toUtf8()).toVariant();
}

// A settings value, which - unlike everything else stored through toJson() - is
// usually a bare string, bool or number rather than a map.
//
// QJsonDocument can only hold an array or an object at its root, so
// QJsonDocument::fromVariant(QVariant("km")) is an *invalid* document, its toJson()
// is a null QByteArray, and toJson()'s NOT NULL guard then wrote the string "{}".
// Every scalar UI setting was therefore persisted as an empty object: the language a
// user picked came back as an empty QVariantMap, which QML renders as
// "[object Object]". That reached the voice worker as the language code, missed every
// entry in its VOICES table, and was silently answered with an English voice - so a
// Khmer transcript was read by an English speaker, which the service refuses with
// "No audio was received". The panel still showed "Khmer" only because its combo box
// maps anything that is not "en" or "zh" to the third row. Nothing was wrong with the
// Khmer text, the Khmer voices, or the request; the language code never survived a
// reload. tts/gapMs came back as 0 for the same reason, pinning Voice gap to "Back to
// back".
//
// The value is wrapped in an object so a scalar has a legal JSON root. QJsonValue
// carries strings, bools and numbers without any of this trouble.
static QString settingToJson(const QVariant &v) {
  return QString::fromUtf8(
      QJsonDocument(QJsonObject{{QStringLiteral("v"), QJsonValue::fromVariant(v)}})
          .toJson(QJsonDocument::Compact));
}

static QVariant settingFromJson(const QString &s) {
  const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
  if (doc.isObject()) {
    const QJsonObject object = doc.object();
    const auto wrapped = object.constFind(QStringLiteral("v"));
    if (wrapped != object.constEnd())
      return wrapped->toVariant();
    // A row written before the wrapper existed. An empty object is the flattened
    // scalar described above - its value is gone and cannot be recovered, so it is
    // reported as never set and the caller's own default applies. Returning the empty
    // map instead is what made a corrupt row look like a real answer.
    if (object.isEmpty())
      return QVariant();
  }
  return doc.toVariant();
}

void ProjectDatabase::setError(const QString &msg) { m_lastError = msg; }

bool ProjectDatabase::execSql(const QString &sql) {
  QSqlQuery q(db(m_connectionName));
  if (!q.exec(sql)) {
    setError(q.lastError().text());
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool ProjectDatabase::open(const QString &path) {
  auto d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  d.setDatabaseName(path);
  if (!d.open()) {
    setError(d.lastError().text());
    return false;
  }
  // WAL mode: faster writes, safe concurrent reads.
  execSql(QStringLiteral("PRAGMA journal_mode=WAL"));
  // NORMAL is the right durability under WAL: it still syncs at every checkpoint
  // and only risks the last transaction on an OS-level crash (not an app crash),
  // which for an editor's autosave is a good trade for not fsyncing on every
  // commit. The project save rewrites every row, so a per-commit fsync was a real
  // cost across a bulk import.
  execSql(QStringLiteral("PRAGMA synchronous=NORMAL"));
  execSql(QStringLiteral("PRAGMA foreign_keys=ON"));
  // The project now has two connections open on this one WAL file: the GUI
  // thread's (small writes - ui settings, the action log) and the async save
  // worker's (the whole-project rewrite). WAL allows many readers but only one
  // writer at a time, so when one connection holds the write lock the other
  // would get SQLITE_BUSY immediately. busy_timeout makes it wait and retry for
  // up to 5 s instead of failing - long enough to outlast a 1000-clip rewrite.
  execSql(QStringLiteral("PRAGMA busy_timeout=5000"));
  return createSchema();
}

void ProjectDatabase::close() {
  if (QSqlDatabase::contains(m_connectionName)) {
    QSqlDatabase::database(m_connectionName, false).close();
    QSqlDatabase::removeDatabase(m_connectionName);
  }
}

bool ProjectDatabase::isOpen() const {
  return QSqlDatabase::contains(m_connectionName) &&
         db(m_connectionName).isOpen();
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------
bool ProjectDatabase::createSchema() {
  const QStringList ddl{
      // Projects
      R"(CREATE TABLE IF NOT EXISTS projects (
           id          TEXT PRIMARY KEY,
           name        TEXT NOT NULL DEFAULT 'Untitled',
           location    TEXT NOT NULL DEFAULT '',
           schema_ver  INTEGER NOT NULL DEFAULT 4,
           created_at  TEXT NOT NULL,
           updated_at  TEXT NOT NULL
         ))",
      // Sequences (one per project for now; structure allows more later)
      R"(CREATE TABLE IF NOT EXISTS sequences (
           id                TEXT PRIMARY KEY,
           project_id        TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
           name              TEXT NOT NULL DEFAULT 'Sequence 01',
           video_track_count INTEGER NOT NULL DEFAULT 1,
           audio_track_count INTEGER NOT NULL DEFAULT 1,
           snapping_enabled  INTEGER NOT NULL DEFAULT 1,
           caption_style     TEXT NOT NULL DEFAULT '{}',
           color_settings    TEXT NOT NULL DEFAULT '{}'
         ))",
      // Media items
      R"(CREATE TABLE IF NOT EXISTS media (
           id         TEXT NOT NULL,
           project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
           data       TEXT NOT NULL DEFAULT '{}',
           PRIMARY KEY (id, project_id)
         ))",
      // Timeline clips
      R"(CREATE TABLE IF NOT EXISTS clips (
           id          TEXT NOT NULL,
           sequence_id TEXT NOT NULL REFERENCES sequences(id) ON DELETE CASCADE,
           kind        TEXT NOT NULL DEFAULT 'video',
           track       TEXT NOT NULL DEFAULT 'V1',
           start_ms    INTEGER NOT NULL DEFAULT 0,
           duration_ms INTEGER NOT NULL DEFAULT 0,
           data        TEXT NOT NULL DEFAULT '{}',
           PRIMARY KEY (id, sequence_id)
         ))",
      R"(CREATE INDEX IF NOT EXISTS clips_seq ON clips(sequence_id))",
      // Timeline markers
      R"(CREATE TABLE IF NOT EXISTS markers (
           id          TEXT NOT NULL,
           sequence_id TEXT NOT NULL REFERENCES sequences(id) ON DELETE CASCADE,
           position_ms INTEGER NOT NULL DEFAULT 0,
           name        TEXT NOT NULL DEFAULT 'Marker',
           color       TEXT NOT NULL DEFAULT '#59a7ff',
           PRIMARY KEY (id, sequence_id)
         ))",
      // Transcript segments (Khmer / any language)
      R"(CREATE TABLE IF NOT EXISTS transcript_segments (
           sequence_id TEXT NOT NULL REFERENCES sequences(id) ON DELETE CASCADE,
           idx         INTEGER NOT NULL,
           start_time  REAL NOT NULL DEFAULT 0,
           end_time    REAL NOT NULL DEFAULT 0,
           text        TEXT NOT NULL DEFAULT '',
           language    TEXT NOT NULL DEFAULT '',
           PRIMARY KEY (sequence_id, idx)
         ))",
      // Muted tracks (one row per muted track)
      R"(CREATE TABLE IF NOT EXISTS muted_tracks (
           sequence_id TEXT NOT NULL REFERENCES sequences(id) ON DELETE CASCADE,
           track       TEXT NOT NULL,
           PRIMARY KEY (sequence_id, track)
         ))",
      // Track state flags
      R"(CREATE TABLE IF NOT EXISTS track_states (
           sequence_id  TEXT NOT NULL REFERENCES sequences(id) ON DELETE CASCADE,
           track        TEXT NOT NULL,
           visible      INTEGER NOT NULL DEFAULT 1,
           locked       INTEGER NOT NULL DEFAULT 0,
           sync_locked  INTEGER NOT NULL DEFAULT 1,
           targeted     INTEGER NOT NULL DEFAULT 1,
           solo         INTEGER NOT NULL DEFAULT 0,
           PRIMARY KEY (sequence_id, track)
         ))",
      // Key-value settings (app settings, persisted between sessions)
      R"(CREATE TABLE IF NOT EXISTS settings (
           key   TEXT PRIMARY KEY,
           value TEXT NOT NULL DEFAULT ''
         ))",
      R"(CREATE TABLE IF NOT EXISTS action_log (
           id          INTEGER PRIMARY KEY AUTOINCREMENT,
           project_id  TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
           sequence_id TEXT NOT NULL DEFAULT '',
           type        TEXT NOT NULL,
           payload     TEXT NOT NULL DEFAULT '{}',
           state       TEXT NOT NULL DEFAULT '',
           created_at  TEXT NOT NULL
         ))",
      R"(CREATE INDEX IF NOT EXISTS action_log_project ON action_log(project_id, id))",
      // Complete state snapshot. Entity tables remain available for indexed
      // queries, while this row guarantees that every current/future project
      // field is persisted without being silently dropped during migration.
      R"(CREATE TABLE IF NOT EXISTS project_state (
           project_id TEXT PRIMARY KEY REFERENCES projects(id) ON DELETE CASCADE,
           state      TEXT NOT NULL DEFAULT '{}',
           updated_at TEXT NOT NULL
         ))",
  };
  for (const auto &sql : ddl)
    if (!execSql(sql))
      return false;
  return true;
}

bool ProjectDatabase::appendAction(const QString &projectId,
                                    const QString &sequenceId,
                                    const QString &type,
                                    const QVariantMap &payload,
                                    const QByteArray &stateSnapshot) {
  if (!isOpen() || projectId.trimmed().isEmpty() || type.trimmed().isEmpty())
    return false;
  QSqlQuery q(db(m_connectionName));
  q.prepare(R"(INSERT INTO action_log(project_id,sequence_id,type,payload,state,created_at)
               VALUES(:project,:sequence,:type,:payload,:state,:created))");
  q.bindValue(":project", projectId);
  q.bindValue(":sequence", sqlText(sequenceId));
  q.bindValue(":type", type);
  q.bindValue(":payload", toJson(payload));
  // Empty by design on most rows - the snapshot only rides along occasionally -
  // and QString::fromUtf8() of a default-constructed QByteArray is null, so
  // without this every ordinary action row failed action_log.state's NOT NULL and
  // the history quietly recorded nothing.
  q.bindValue(":state", sqlText(QString::fromUtf8(stateSnapshot)));
  q.bindValue(":created", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  if (!q.exec()) {
    setError(q.lastError().text());
    return false;
  }
  return true;
}

QVariantList ProjectDatabase::actions(const QString &projectId, int limit) {
  QVariantList result;
  if (!isOpen()) return result;
  QSqlQuery q(db(m_connectionName));
  q.prepare(R"(SELECT id,sequence_id,type,payload,state,created_at
               FROM action_log WHERE project_id=:project
               ORDER BY id DESC LIMIT :limit)");
  q.bindValue(":project", projectId);
  q.bindValue(":limit", qBound(1, limit, 10000));
  if (!q.exec()) {
    setError(q.lastError().text());
    return result;
  }
  while (q.next()) {
    result.append(QVariantMap{{"id", q.value(0)},
                              {"sequenceId", q.value(1)},
                              {"type", q.value(2)},
                              {"payload", fromJson(q.value(3).toString())},
                              {"state", q.value(4).toString()},
                              {"createdAt", q.value(5)}});
  }
  return result;
}

bool ProjectDatabase::pruneStateSnapshots(const QString &keepProjectId,
                                          int keep) {
  if (!isOpen())
    return false;
  QSqlQuery q(db(m_connectionName));
  // Keep the newest few plus the caller's own row, drop the rest. Ordered by
  // updated_at rather than by rowid because a reopened project rewrites its
  // snapshot in place, and the one written most recently is the one worth
  // keeping.
  q.prepare(R"(DELETE FROM project_state
               WHERE project_id <> :keep
                 AND project_id NOT IN (
                       SELECT project_id FROM project_state
                       ORDER BY updated_at DESC LIMIT :keepCount))");
  q.bindValue(QStringLiteral(":keep"), keepProjectId);
  q.bindValue(QStringLiteral(":keepCount"), qBound(1, keep, 200));
  if (!q.exec()) {
    setError(q.lastError().text());
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Whole-project save / load
// ---------------------------------------------------------------------------
bool ProjectDatabase::saveProject(const QVariantMap &state) {
  auto d = db(m_connectionName);
  if (!d.transaction()) {
    setError(d.lastError().text());
    return false;
  }

  // Both are bound into NOT NULL columns in half a dozen statements below, so
  // they are made non-null once here rather than at each binding.
  const QString projectId = sqlText(state.value("projectId"));
  const QString sequenceId = sqlText(state.value("sequenceId"));
  const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

  // --- projects ---
  {
    QSqlQuery q(d);
    q.prepare(R"(INSERT INTO projects(id,name,location,schema_ver,created_at,updated_at)
                 VALUES(:id,:name,:loc,4,:now,:now)
                 ON CONFLICT(id) DO UPDATE SET
                   name=excluded.name, location=excluded.location,
                   schema_ver=excluded.schema_ver, updated_at=excluded.updated_at)");
    q.bindValue(":id", projectId);
    q.bindValue(":name", sqlText(state.value("projectName"),
                                 QStringLiteral("Untitled")));
    q.bindValue(":loc", sqlText(state.value("projectLocation")));
    q.bindValue(":now", now);
    if (!q.exec()) { setError(q.lastError().text()); d.rollback(); return false; }
  }

  // --- sequences ---
  {
    QSqlQuery q(d);
    q.prepare(R"(INSERT INTO sequences(id,project_id,name,video_track_count,
                   audio_track_count,snapping_enabled,caption_style,color_settings)
                 VALUES(:id,:pid,:name,:vtc,:atc,:snap,:cs,:cols)
                 ON CONFLICT(id) DO UPDATE SET
                   name=excluded.name,
                   video_track_count=excluded.video_track_count,
                   audio_track_count=excluded.audio_track_count,
                   snapping_enabled=excluded.snapping_enabled,
                   caption_style=excluded.caption_style,
                   color_settings=excluded.color_settings)");
    q.bindValue(":id", sequenceId);
    q.bindValue(":pid", projectId);
    q.bindValue(":name", sqlText(state.value("sequenceName"),
                                 QStringLiteral("Sequence 01")));
    q.bindValue(":vtc", state.value("videoTrackCount").toInt());
    q.bindValue(":atc", state.value("audioTrackCount").toInt());
    q.bindValue(":snap", state.value("snappingEnabled").toBool() ? 1 : 0);
    q.bindValue(":cs", toJson(state.value("captionStyle")));
    q.bindValue(":cols", toJson(state.value("colorSettings")));
    if (!q.exec()) { setError(q.lastError().text()); d.rollback(); return false; }
  }

  // --- media (replace all for this project) ---
  {
    QSqlQuery del(d);
    del.prepare("DELETE FROM media WHERE project_id=:pid");
    del.bindValue(":pid", projectId);
    if (!del.exec()) { setError(del.lastError().text()); d.rollback(); return false; }
    QSqlQuery ins(d);
    ins.prepare("INSERT INTO media(id,project_id,data) VALUES(:id,:pid,:data)");
    for (const auto &v : state.value("media").toList()) {
      const auto m = v.toMap();
      ins.bindValue(":id", sqlText(m.value("id")));
      ins.bindValue(":pid", projectId);
      ins.bindValue(":data", toJson(v));
      if (!ins.exec()) { setError(ins.lastError().text()); d.rollback(); return false; }
    }
  }

  // --- clips ---
  {
    QSqlQuery del(d);
    del.prepare("DELETE FROM clips WHERE sequence_id=:sid");
    del.bindValue(":sid", sequenceId);
    if (!del.exec()) { setError(del.lastError().text()); d.rollback(); return false; }
    QSqlQuery ins(d);
    ins.prepare(R"(INSERT INTO clips(id,sequence_id,kind,track,start_ms,duration_ms,data)
                   VALUES(:id,:sid,:kind,:track,:start,:dur,:data))");
    for (const auto &v : state.value("clips").toList()) {
      const auto c = v.toMap();
      ins.bindValue(":id", sqlText(c.value("id")));
      ins.bindValue(":sid", sequenceId);
      ins.bindValue(":kind", sqlText(c.value("kind"), QStringLiteral("video")));
      ins.bindValue(":track", sqlText(c.value("track"), QStringLiteral("V1")));
      ins.bindValue(":start", c.value("startMs").toLongLong());
      ins.bindValue(":dur", c.value("durationMs").toLongLong());
      ins.bindValue(":data", toJson(v));
      if (!ins.exec()) { setError(ins.lastError().text()); d.rollback(); return false; }
    }
  }

  // --- markers ---
  {
    QSqlQuery del(d);
    del.prepare("DELETE FROM markers WHERE sequence_id=:sid");
    del.bindValue(":sid", sequenceId);
    if (!del.exec()) { setError(del.lastError().text()); d.rollback(); return false; }
    QSqlQuery ins(d);
    ins.prepare(R"(INSERT INTO markers(id,sequence_id,position_ms,name,color)
                   VALUES(:id,:sid,:pos,:name,:color))");
    for (const auto &v : state.value("markers").toList()) {
      const auto m = v.toMap();
      ins.bindValue(":id", sqlText(m.value("id")));
      ins.bindValue(":sid", sequenceId);
      ins.bindValue(":pos", m.value("positionMs").toLongLong());
      ins.bindValue(":name", sqlText(m.value("name"),
                                     QStringLiteral("Marker")));
      ins.bindValue(":color", sqlText(m.value("color"),
                                      QStringLiteral("#59a7ff")));
      if (!ins.exec()) { setError(ins.lastError().text()); d.rollback(); return false; }
    }
  }

  // --- muted tracks ---
  {
    QSqlQuery del(d);
    del.prepare("DELETE FROM muted_tracks WHERE sequence_id=:sid");
    del.bindValue(":sid", sequenceId);
    if (!del.exec()) { setError(del.lastError().text()); d.rollback(); return false; }
    QSqlQuery ins(d);
    ins.prepare("INSERT INTO muted_tracks(sequence_id,track) VALUES(:sid,:track)");
    for (const auto &v : state.value("mutedTracks").toList()) {
      ins.bindValue(":sid", sequenceId);
      ins.bindValue(":track", sqlText(v));
      if (!ins.exec()) { setError(ins.lastError().text()); d.rollback(); return false; }
    }
  }

  // --- track states ---
  {
    QSqlQuery del(d);
    del.prepare("DELETE FROM track_states WHERE sequence_id=:sid");
    del.bindValue(":sid", sequenceId);
    if (!del.exec()) { setError(del.lastError().text()); d.rollback(); return false; }
    QSqlQuery ins(d);
    ins.prepare(R"(INSERT INTO track_states(sequence_id,track,visible,locked,
                     sync_locked,targeted,solo)
                   VALUES(:sid,:track,:vis,:lock,:sync,:tgt,:solo))");
    const QVariantMap ts = state.value("trackStates").toMap();
    for (auto it = ts.cbegin(); it != ts.cend(); ++it) {
      const QVariantMap s = it.value().toMap();
      ins.bindValue(":sid", sequenceId);
      ins.bindValue(":track", it.key());
      ins.bindValue(":vis", s.value("visible", true).toBool() ? 1 : 0);
      ins.bindValue(":lock", s.value("locked", false).toBool() ? 1 : 0);
      ins.bindValue(":sync", s.value("syncLocked", true).toBool() ? 1 : 0);
      ins.bindValue(":tgt", s.value("targeted", true).toBool() ? 1 : 0);
      ins.bindValue(":solo", s.value("solo", false).toBool() ? 1 : 0);
      if (!ins.exec()) { setError(ins.lastError().text()); d.rollback(); return false; }
    }
  }

  // Keep an authoritative complete snapshot in SQLite. This intentionally
  // stores fields not represented by the normalized entity tables (for
  // example source transcripts and newer effect metadata). It is part of the
  // same transaction as the normalized rows so readers never observe two
  // different versions of one project.
  {
    QSqlQuery removeSnapshot(d);
    removeSnapshot.prepare(QStringLiteral(
        "DELETE FROM project_state WHERE project_id=:snapshot_project"));
    removeSnapshot.bindValue(QStringLiteral(":snapshot_project"), projectId);
    if (!removeSnapshot.exec()) {
      setError(QStringLiteral("project_state snapshot delete: ") +
               removeSnapshot.lastError().text());
      d.rollback();
      return false;
    }
    QSqlQuery snapshot(d);
    snapshot.prepare(QStringLiteral(
        "INSERT INTO project_state(project_id,state,updated_at) "
        "VALUES(:snapshot_project,:snapshot_state,:snapshot_updated)"));
    snapshot.bindValue(QStringLiteral(":snapshot_project"), projectId);
    snapshot.bindValue(QStringLiteral(":snapshot_state"), toJson(state));
    snapshot.bindValue(QStringLiteral(":snapshot_updated"), now);
    if (!snapshot.exec()) {
      setError(QStringLiteral("project_state snapshot insert: ") +
               snapshot.lastError().text());
      d.rollback();
      return false;
    }
  }

  if (!d.commit()) { setError(d.lastError().text()); return false; }
  return true;
}

QVariantMap ProjectDatabase::loadProject(const QString &projectId) {
  auto d = db(m_connectionName);
  QVariantMap state;

  // Prefer the complete snapshot. It preserves every serialized field and is
  // backward-compatible with the normalized tables below for older databases.
  {
    QSqlQuery snapshot(d);
    snapshot.prepare("SELECT state FROM project_state WHERE project_id=:id");
    snapshot.bindValue(":id", projectId);
    if (snapshot.exec() && snapshot.next()) {
      const QVariant parsed = fromJson(snapshot.value(0).toString());
      if (parsed.typeId() == QMetaType::QVariantMap)
        return parsed.toMap();
    }
  }

  // project row
  {
    QSqlQuery q(d);
    q.prepare("SELECT name,location,schema_ver FROM projects WHERE id=:id");
    q.bindValue(":id", projectId);
    if (!q.exec() || !q.next()) { setError(q.lastError().text()); return {}; }
    state["projectId"] = projectId;
    state["projectName"] = q.value(0).toString();
    state["projectLocation"] = q.value(1).toString();
    state["schemaVersion"] = q.value(2).toInt();
  }

  // sequence row
  QString sequenceId;
  {
    QSqlQuery q(d);
    q.prepare("SELECT id,name,video_track_count,audio_track_count,"
              "snapping_enabled,caption_style,color_settings "
              "FROM sequences WHERE project_id=:pid LIMIT 1");
    q.bindValue(":pid", projectId);
    if (!q.exec() || !q.next()) { setError(q.lastError().text()); return {}; }
    sequenceId = q.value(0).toString();
    state["sequenceId"] = sequenceId;
    state["sequenceName"] = q.value(1).toString();
    state["videoTrackCount"] = q.value(2).toInt();
    state["audioTrackCount"] = q.value(3).toInt();
    state["snappingEnabled"] = q.value(4).toInt() != 0;
    state["captionStyle"] = fromJson(q.value(5).toString());
    state["colorSettings"] = fromJson(q.value(6).toString());
  }

  // media
  {
    QSqlQuery q(d);
    q.prepare("SELECT data FROM media WHERE project_id=:pid");
    q.bindValue(":pid", projectId);
    q.exec();
    QVariantList media;
    while (q.next()) media << fromJson(q.value(0).toString());
    state["media"] = media;
  }

  // clips
  {
    QSqlQuery q(d);
    q.prepare("SELECT data FROM clips WHERE sequence_id=:sid ORDER BY start_ms");
    q.bindValue(":sid", sequenceId);
    q.exec();
    QVariantList clips;
    while (q.next()) clips << fromJson(q.value(0).toString());
    state["clips"] = clips;
  }

  // markers
  {
    QSqlQuery q(d);
    q.prepare("SELECT id,position_ms,name,color FROM markers "
              "WHERE sequence_id=:sid ORDER BY position_ms");
    q.bindValue(":sid", sequenceId);
    q.exec();
    QVariantList markers;
    while (q.next()) {
      markers << QVariantMap{{"id", q.value(0).toString()},
                             {"positionMs", q.value(1).toLongLong()},
                             {"name", q.value(2).toString()},
                             {"color", q.value(3).toString()}};
    }
    state["markers"] = markers;
  }

  // muted tracks
  {
    QSqlQuery q(d);
    q.prepare("SELECT track FROM muted_tracks WHERE sequence_id=:sid");
    q.bindValue(":sid", sequenceId);
    q.exec();
    QVariantList muted;
    while (q.next()) muted << q.value(0).toString();
    state["mutedTracks"] = muted;
  }

  // track states
  {
    QSqlQuery q(d);
    q.prepare("SELECT track,visible,locked,sync_locked,targeted,solo "
              "FROM track_states WHERE sequence_id=:sid");
    q.bindValue(":sid", sequenceId);
    q.exec();
    QVariantMap ts;
    while (q.next()) {
      const QString track = q.value(0).toString();
      ts[track] = QVariantMap{{"id", track},
                              {"visible", q.value(1).toInt() != 0},
                              {"locked", q.value(2).toInt() != 0},
                              {"syncLocked", q.value(3).toInt() != 0},
                              {"targeted", q.value(4).toInt() != 0},
                              {"solo", q.value(5).toInt() != 0}};
    }
    state["trackStates"] = ts;
  }

  return state;
}

QVariantList ProjectDatabase::listProjects() {
  QSqlQuery q(db(m_connectionName));
  q.exec("SELECT id,name,location,updated_at FROM projects ORDER BY updated_at DESC");
  QVariantList result;
  while (q.next())
    result << QVariantMap{{"id", q.value(0).toString()},
                          {"name", q.value(1).toString()},
                          {"location", q.value(2).toString()},
                          {"updatedAt", q.value(3).toString()}};
  return result;
}

bool ProjectDatabase::deleteProject(const QString &projectId) {
  QSqlQuery q(db(m_connectionName));
  q.prepare("DELETE FROM projects WHERE id=:id");
  q.bindValue(":id", projectId);
  if (!q.exec()) { setError(q.lastError().text()); return false; }
  return true;
}

// ---------------------------------------------------------------------------
// Incremental entity helpers
// ---------------------------------------------------------------------------
bool ProjectDatabase::upsertMedia(const QString &projectId,
                                  const QVariantMap &media) {
  QSqlQuery q(db(m_connectionName));
  q.prepare("INSERT INTO media(id,project_id,data) VALUES(:id,:pid,:data) "
            "ON CONFLICT(id,project_id) DO UPDATE SET data=excluded.data");
  q.bindValue(":id", sqlText(media.value("id")));
  q.bindValue(":pid", sqlText(projectId));
  q.bindValue(":data", toJson(media));
  if (!q.exec()) { setError(q.lastError().text()); return false; }
  return true;
}

bool ProjectDatabase::removeMedia(const QString &projectId,
                                  const QString &mediaId) {
  QSqlQuery q(db(m_connectionName));
  q.prepare("DELETE FROM media WHERE id=:id AND project_id=:pid");
  q.bindValue(":id", mediaId);
  q.bindValue(":pid", projectId);
  if (!q.exec()) { setError(q.lastError().text()); return false; }
  return true;
}

bool ProjectDatabase::upsertClip(const QString &sequenceId,
                                 const QVariantMap &clip) {
  QSqlQuery q(db(m_connectionName));
  q.prepare(R"(INSERT INTO clips(id,sequence_id,kind,track,start_ms,duration_ms,data)
               VALUES(:id,:sid,:kind,:track,:start,:dur,:data)
               ON CONFLICT(id,sequence_id) DO UPDATE SET
                 kind=excluded.kind, track=excluded.track,
                 start_ms=excluded.start_ms, duration_ms=excluded.duration_ms,
                 data=excluded.data)");
  q.bindValue(":id", sqlText(clip.value("id")));
  q.bindValue(":sid", sqlText(sequenceId));
  q.bindValue(":kind", sqlText(clip.value("kind"), QStringLiteral("video")));
  q.bindValue(":track", sqlText(clip.value("track"), QStringLiteral("V1")));
  q.bindValue(":start", clip.value("startMs").toLongLong());
  q.bindValue(":dur", clip.value("durationMs").toLongLong());
  q.bindValue(":data", toJson(clip));
  if (!q.exec()) { setError(q.lastError().text()); return false; }
  return true;
}

bool ProjectDatabase::removeClip(const QString &sequenceId,
                                 const QString &clipId) {
  QSqlQuery q(db(m_connectionName));
  q.prepare("DELETE FROM clips WHERE id=:id AND sequence_id=:sid");
  q.bindValue(":id", clipId);
  q.bindValue(":sid", sequenceId);
  if (!q.exec()) { setError(q.lastError().text()); return false; }
  return true;
}

QVariantList ProjectDatabase::clipsForSequence(const QString &sequenceId) {
  QSqlQuery q(db(m_connectionName));
  q.prepare("SELECT data FROM clips WHERE sequence_id=:sid ORDER BY start_ms");
  q.bindValue(":sid", sequenceId);
  q.exec();
  QVariantList clips;
  while (q.next()) clips << fromJson(q.value(0).toString());
  return clips;
}

bool ProjectDatabase::upsertMarker(const QString &sequenceId,
                                   const QVariantMap &marker) {
  QSqlQuery q(db(m_connectionName));
  q.prepare(R"(INSERT INTO markers(id,sequence_id,position_ms,name,color)
               VALUES(:id,:sid,:pos,:name,:color)
               ON CONFLICT(id,sequence_id) DO UPDATE SET
                 position_ms=excluded.position_ms, name=excluded.name,
                 color=excluded.color)");
  q.bindValue(":id", sqlText(marker.value("id")));
  q.bindValue(":sid", sqlText(sequenceId));
  q.bindValue(":pos", marker.value("positionMs").toLongLong());
  q.bindValue(":name", sqlText(marker.value("name"), QStringLiteral("Marker")));
  q.bindValue(":color", sqlText(marker.value("color"),
                                QStringLiteral("#59a7ff")));
  if (!q.exec()) { setError(q.lastError().text()); return false; }
  return true;
}

bool ProjectDatabase::removeMarker(const QString &sequenceId,
                                   const QString &markerId) {
  QSqlQuery q(db(m_connectionName));
  q.prepare("DELETE FROM markers WHERE id=:id AND sequence_id=:sid");
  q.bindValue(":id", markerId);
  q.bindValue(":sid", sequenceId);
  if (!q.exec()) { setError(q.lastError().text()); return false; }
  return true;
}

// Transcript: replace all segments for the sequence atomically.
bool ProjectDatabase::setTranscript(const QString &sequenceId,
                                    const QVariantList &segments,
                                    const QString &language) {
  auto d = db(m_connectionName);
  if (!d.transaction()) { setError(d.lastError().text()); return false; }
  {
    QSqlQuery del(d);
    del.prepare("DELETE FROM transcript_segments WHERE sequence_id=:sid");
    del.bindValue(":sid", sequenceId);
    if (!del.exec()) { setError(del.lastError().text()); d.rollback(); return false; }
  }
  QSqlQuery ins(d);
  ins.prepare(R"(INSERT INTO transcript_segments
                   (sequence_id,idx,start_time,end_time,text,language)
                 VALUES(:sid,:idx,:start,:end,:text,:lang))");
  for (int i = 0; i < segments.size(); ++i) {
    const auto seg = segments[i].toMap();
    ins.bindValue(":sid", sqlText(sequenceId));
    ins.bindValue(":idx", i);
    ins.bindValue(":start", seg.value("start").toDouble());
    ins.bindValue(":end", seg.value("end").toDouble());
    // text may be Khmer or any Unicode — stored as UTF-8 TEXT by SQLite
    ins.bindValue(":text", sqlText(seg.value("text")));
    ins.bindValue(":lang", sqlText(language));
    if (!ins.exec()) { setError(ins.lastError().text()); d.rollback(); return false; }
  }
  if (!d.commit()) { setError(d.lastError().text()); return false; }
  return true;
}

QVariantList ProjectDatabase::transcriptForSequence(const QString &sequenceId,
                                                     QString *language) {
  QSqlQuery q(db(m_connectionName));
  q.prepare("SELECT start_time,end_time,text,language "
            "FROM transcript_segments WHERE sequence_id=:sid ORDER BY idx");
  q.bindValue(":sid", sequenceId);
  q.exec();
  QVariantList result;
  while (q.next()) {
    if (language && language->isEmpty())
      *language = q.value(3).toString();
    result << QVariantMap{{"start", q.value(0).toDouble()},
                          {"end", q.value(1).toDouble()},
                          {"text", q.value(2).toString()}};
  }
  return result;
}

bool ProjectDatabase::setSetting(const QString &key, const QVariant &value) {
  QSqlQuery q(db(m_connectionName));
  q.prepare("INSERT INTO settings(key,value) VALUES(:k,:v) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  q.bindValue(":k", key);
  q.bindValue(":v", settingToJson(value));
  if (!q.exec()) { setError(q.lastError().text()); return false; }
  return true;
}

QVariant ProjectDatabase::getSetting(const QString &key,
                                     const QVariant &defaultValue) const {
  QSqlQuery q(db(m_connectionName));
  q.prepare("SELECT value FROM settings WHERE key=:k");
  q.bindValue(":k", key);
  if (!q.exec() || !q.next()) return defaultValue;
  const QVariant stored = settingFromJson(q.value(0).toString());
  return stored.isValid() ? stored : defaultValue;
}

QVariantMap ProjectDatabase::allSettings() const {
  QSqlQuery q(db(m_connectionName));
  q.exec("SELECT key,value FROM settings");
  QVariantMap result;
  while (q.next()) {
    const QVariant stored = settingFromJson(q.value(1).toString());
    // A row that cannot be read is left out rather than inserted as an empty map,
    // so restoreUiStateFromDatabase() does not seed m_uiState with a value that
    // would shadow the caller default.
    if (stored.isValid())
      result[q.value(0).toString()] = stored;
  }
  return result;
}
