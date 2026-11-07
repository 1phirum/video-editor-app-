#include "app/core_app/project_save_service.h"

#include <QMetaObject>

#include "app/core_app/project_database.h"

ProjectSaveWorker::ProjectSaveWorker(QAtomicInteger<quint64> *latestSeq)
    : m_latestSeq(latestSeq) {}

ProjectSaveWorker::~ProjectSaveWorker() { delete m_db; }

void ProjectSaveWorker::save(const QString &databasePath,
                             const QVariantMap &state, quint64 seq) {
  // A newer snapshot is already queued behind this one, so this write would be
  // overwritten the instant it finished. Skip it: the latest-seq request will
  // write the state the user actually ended on.
  if (m_latestSeq && seq != m_latestSeq->loadAcquire())
    return;
  if (databasePath.isEmpty())
    return;
  if (!m_db)
    m_db = new ProjectDatabase();
  if (m_openPath != databasePath || !m_db->isOpen()) {
    m_db->close();
    if (!m_db->open(databasePath)) {
      emit finished(false, m_db->lastError());
      return;
    }
    m_openPath = databasePath;
  }
  const bool ok = m_db->saveProject(state);
  emit finished(ok, ok ? QString() : m_db->lastError());
}

void ProjectSaveWorker::shutdown() {
  if (m_db) {
    m_db->close();
    delete m_db;
    m_db = nullptr;
  }
}

ProjectSaveService::ProjectSaveService(QObject *parent) : QObject(parent) {
  m_worker = new ProjectSaveWorker(&m_latestSeq);
  m_worker->moveToThread(&m_thread);
  connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
  connect(this, &ProjectSaveService::requestSaveQueued, m_worker,
          &ProjectSaveWorker::save, Qt::QueuedConnection);
  connect(m_worker, &ProjectSaveWorker::finished, this,
          &ProjectSaveService::saveFinished, Qt::QueuedConnection);
  m_thread.start();
}

ProjectSaveService::~ProjectSaveService() {
  // Let a queued save finish, then tear the connection down on its own thread
  // before the event loop stops.
  flush();
  if (m_thread.isRunning()) {
    QMetaObject::invokeMethod(m_worker, &ProjectSaveWorker::shutdown,
                              Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
  }
}

void ProjectSaveService::requestSave(const QString &databasePath,
                                     const QVariantMap &state) {
  const quint64 seq = ++m_requestSeq;
  m_latestSeq.storeRelease(seq);
  emit requestSaveQueued(databasePath, state, seq);
}

void ProjectSaveService::flush() {
  if (!m_thread.isRunning())
    return;
  // An empty callable, run on the worker thread by blocking connection, can
  // only return after every slot already queued ahead of it (the pending
  // saves) has run. So when this returns the worker is idle and the latest
  // snapshot is on disk.
  QMetaObject::invokeMethod(
      m_worker, []() {}, Qt::BlockingQueuedConnection);
}
