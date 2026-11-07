#pragma once

#include <QAtomicInteger>
#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantMap>

#include "core/module_api.h"

class ProjectDatabase;

// Writes the whole-project SQLite snapshot off the GUI thread.
//
// ProjectDatabase::saveProject() is a DELETE-all + INSERT-all of every clip,
// media source, marker and transcript segment plus a full project_state blob,
// one transaction. On the 1000-clip projects this costs tens of milliseconds
// and it used to run on the GUI thread 250 ms after every edit (and on every
// autosave), so a drag that dirtied the project turned into a string of GUI
// stalls. The work does not need the GUI thread at all - it only reads a
// snapshot of the state - so it runs here on a dedicated thread with its own
// database connection (SQLite connections are thread-affine, so the worker
// opens and uses its own; the GUI keeps m_projectDatabase for its small writes,
// and PRAGMA busy_timeout lets the two connections coexist on one WAL file).
//
// requestSave() coalesces: each call stamps a monotonic sequence and the worker
// skips any queued snapshot that a newer one has already superseded, so an edit
// burst that outruns the disk collapses to a single write of the latest state.
class ProjectSaveWorker : public QObject {
  Q_OBJECT
public:
  explicit ProjectSaveWorker(QAtomicInteger<quint64> *latestSeq);
  ~ProjectSaveWorker() override;

public slots:
  // Runs on the worker thread. Opens (once, lazily) a private connection to
  // *databasePath* and writes *state*, unless *seq* has already been superseded.
  void save(const QString &databasePath, const QVariantMap &state, quint64 seq);
  // Closes the private connection on its own thread before the thread stops.
  void shutdown();

signals:
  void finished(bool ok, const QString &error);

private:
  QAtomicInteger<quint64> *m_latestSeq = nullptr;  // newest requested id; not owned
  ProjectDatabase *m_db = nullptr;
  QString m_openPath;
};

class CUTPRO_BACKEND_API ProjectSaveService : public QObject {
  Q_OBJECT
public:
  explicit ProjectSaveService(QObject *parent = nullptr);
  ~ProjectSaveService() override;

  // Queue a save of *state* to *databasePath*. Returns immediately; the write
  // happens on the worker thread. Superseded snapshots are dropped.
  void requestSave(const QString &databasePath, const QVariantMap &state);

  // Block the calling (GUI) thread until the worker has drained every save
  // queued so far. Call before a synchronous write of the same file (explicit
  // Save) and on shutdown so the on-disk state is never left behind an edit.
  void flush();

signals:
  void saveFinished(bool ok, const QString &error);
  // Internal: carries a requestSave() across to the worker thread by queued
  // connection. Not meant to be emitted by callers.
  void requestSaveQueued(const QString &databasePath, const QVariantMap &state,
                         quint64 seq);

private:
  QThread m_thread;
  ProjectSaveWorker *m_worker = nullptr;
  QAtomicInteger<quint64> m_latestSeq{0};
  quint64 m_requestSeq = 0;  // GUI-thread only
};
