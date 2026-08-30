#pragma once

#include "app/media/media_scan.h"

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

#include "core/module_api.h"

// Concurrent, cancellable media import: folder expansion, optional copy into the
// project, and metadata probing.
//
// The previous pipeline processed exactly one file at a time - probe, wait,
// append, emit mediaChanged() and timelineChanged(), then post the next file
// through a zero-timer. For a folder of 40 clips that is 40 serialised ffprobe
// process launches and 80 full-collection model resets, so import time grew
// with the square of the bin size and the UI repainted the whole project bin
// between every file. Folder copies ran QFile::copy on the GUI thread, which
// freezes the window for minutes on a multi-gigabyte source and cannot be
// cancelled.
//
// What this queue does instead:
//  - the directory walk runs on a worker thread through MediaScan, bounded and
//    cancellable, so dropping a huge folder never blocks the GUI;
//  - probes run on a private thread pool with a small fixed worker count, so
//    several files are examined at once without spawning dozens of ffprobe
//    processes on a machine that cannot feed them;
//  - copies run on the same workers in cancellable chunks and clean up their
//    partial file if the import is abandoned;
//  - duplicate rejection happens in the worker before any probe, against an
//    O(1) key set that includes both the pre-existing project media and the
//    files accepted earlier in this same run;
//  - completed items are released to the owner in scan order and in batches, so
//    the bin fills predictably and the model is rebuilt a few times per second
//    rather than once per file.
//
// All signals are emitted on the thread that owns the queue.
class CUTPRO_MEDIA_API MediaImportQueue final : public QObject {
  Q_OBJECT

public:
  struct Request {
    QStringList paths;
    // Empty means import in place. Otherwise each source is copied into this
    // folder first and the copy is what gets imported.
    QString copyDestination;
    MediaScan::Limits scanLimits;
  };

  // Invoked on a worker thread with an absolute path; must not touch GUI state.
  using Prober = std::function<QVariantMap(const QString &path)>;

  explicit MediaImportQueue(QObject *parent = nullptr);
  ~MediaImportQueue() override;

  void setProber(Prober prober);
  // Duplicate keys already present in the project, from
  // MediaPath::duplicateKey(). Taken as a snapshot at start().
  void setExistingKeys(const QSet<QString> &keys);
  // Defaults to a quarter of the CPU count, clamped to [1, 4]: probing is
  // process- and disk-bound, so more workers mostly add seek contention.
  void setMaximumConcurrency(int workers);
  int maximumConcurrency() const { return m_concurrency; }

  bool start(const Request &request);
  // Returns immediately; finished() follows once the workers have drained.
  void cancel();
  // Cancels and blocks until every worker has stopped. Called from the owner's
  // destructor.
  void shutdown();

  bool active() const { return m_active; }
  int total() const { return m_total; }
  int completed() const { return m_completed; }
  int acceptedCount() const { return m_accepted; }
  int skippedCount() const { return m_skipped; }
  int percent() const;

signals:
  void started(int total);
  // Probed media in scan order, batched. Never empty.
  void itemsReady(const QVariantList &items);
  void progressChanged();
  // Scan truncation and copy failures: user-facing, non-fatal.
  void warning(const QString &message);
  void finished(int accepted, int skipped, bool cancelled);

private:
  struct Slot {
    QVariantMap item;
    bool done = false;
    bool accepted = false;
  };

  void beginProbing(const MediaScan::Result &scan);
  void dispatch();
  void submit(int index);
  void completeSlot(int index, const QVariantMap &item, bool acceptedItem,
                    const QString &failure);
  void flush();
  void finishRun();
  // Picks a free name in the copy destination and reserves it, so two workers
  // copying files with the same base name cannot pick the same target. Called
  // from worker threads.
  QString reserveCopyDestination(const QString &source,
                                 const QString &destinationFolder);
  // Inserts the key if absent. Returns false when the file is already in the
  // project or was accepted earlier in this run.
  bool claimKey(const QString &key);
  bool cancelled() const;

  Prober m_prober;
  // Private pool: the global QThreadPool is shared with every other
  // QtConcurrent user in the process, and a long import must not starve them.
  QThreadPool m_pool;
  QTimer m_flushTimer;
  Request m_request;
  QStringList m_files;
  QVector<Slot> m_slots;
  QSet<QString> m_ownerKeys;
  // m_workerKeys and m_reservedNames are read and written from worker threads.
  QSet<QString> m_workerKeys;
  QSet<QString> m_reservedNames;
  mutable QMutex m_workerMutex;
  // Shared with the in-flight tasks so a cancelled run's workers stop even
  // though the queue may already have started reporting finished().
  std::shared_ptr<std::atomic_bool> m_cancel;
  int m_concurrency = 1;
  int m_nextIndex = 0;
  int m_inFlight = 0;
  int m_emitIndex = 0;
  int m_total = 0;
  int m_completed = 0;
  int m_accepted = 0;
  int m_skipped = 0;
  // Per-file failures are reported for the first few only; the rest are
  // summarised once, so a folder of 500 unreadable files cannot produce 500
  // message boxes.
  int m_failures = 0;
  int m_warningsEmitted = 0;
  bool m_active = false;
  bool m_scanning = false;
};
