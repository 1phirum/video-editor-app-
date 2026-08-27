#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// SQLite-backed persistence for all project data.
// Replaces the single flat JSON file with a proper relational store so each
// entity (media, clip, marker, transcript segment, settings …) can be queried,
// updated and deleted independently without round-tripping the entire document.
//
// Schema version history
//   1 – initial (projects, sequences, media, clips, markers,
//               transcript_segments, track_states, muted_tracks, settings)
class ProjectDatabase : public QObject {
  Q_OBJECT
public:
  explicit ProjectDatabase(QObject *parent = nullptr);
  ~ProjectDatabase() override;

  // Open (or create) the database at *path*.  Call once before any other
  // method.  Returns false and sets lastError() on failure.
  bool open(const QString &path);
  void close();
  bool isOpen() const;

  QString lastError() const { return m_lastError; }

  // --- Whole-project helpers -------------------------------------------------
  // Serialize the complete in-memory state map (same shape as serializeState)
  // into the database, replacing everything that belongs to that project.
  bool saveProject(const QVariantMap &state);

  // Load a project by id and return its state map, or an empty map on failure.
  QVariantMap loadProject(const QString &projectId);

  // List all stored projects: [{id, name, location, updatedAt}, …]
  QVariantList listProjects();

  // Delete a project and all its child records.
  bool deleteProject(const QString &projectId);

  // --- Individual entity accessors (used for incremental saves) -------------
  bool upsertMedia(const QString &projectId, const QVariantMap &media);
  bool removeMedia(const QString &projectId, const QString &mediaId);

  bool upsertClip(const QString &sequenceId, const QVariantMap &clip);
  bool removeClip(const QString &sequenceId, const QString &clipId);
  QVariantList clipsForSequence(const QString &sequenceId);

  bool upsertMarker(const QString &sequenceId, const QVariantMap &marker);
  bool removeMarker(const QString &sequenceId, const QString &markerId);

  bool setTranscript(const QString &sequenceId, const QVariantList &segments,
                     const QString &language);
  QVariantList transcriptForSequence(const QString &sequenceId,
                                     QString *language = nullptr);

  bool setSetting(const QString &key, const QVariant &value);
  QVariant getSetting(const QString &key,
                      const QVariant &defaultValue = QVariant()) const;
  QVariantMap allSettings() const;

  // Append an audit entry and optional full state snapshot. This is kept
  // separate from the current-state tables so the complete edit history is
  // recoverable without affecting normal project queries.
  bool appendAction(const QString &projectId, const QString &sequenceId,
                    const QString &type, const QVariantMap &payload,
                    const QByteArray &stateSnapshot = {});
  QVariantList actions(const QString &projectId, int limit = 500);

private:
  bool createSchema();
  bool execSql(const QString &sql);
  void setError(const QString &msg);

  QString m_connectionName;
  QString m_lastError;
};
