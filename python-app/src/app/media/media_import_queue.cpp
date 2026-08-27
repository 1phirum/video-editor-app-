#include "app/media/media_import_queue.h"

#include "app/media/media_path.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace {

// Results are handed to the owner a few times per second: fast enough to look
// live, slow enough that a 500-file import rebuilds the bin model about twenty
// times instead of five hundred.
constexpr int kFlushIntervalMs = 120;

// Ceiling on one batch so a burst cannot produce a single enormous model reset.
constexpr int kMaxBatchSize = 64;

// Copy chunk: large enough for sequential throughput, small enough that
// cancelling a multi-gigabyte copy is felt immediately.
constexpr qint64 kCopyChunkBytes = 4LL * 1024 * 1024;

// Individual failure messages shown before switching to a single summary.
constexpr int kMaxWarnings = 5;

int defaultConcurrency() {
  const int cores = QThread::idealThreadCount();
  // Probing launches an ffprobe process per file and is disk-bound; beyond a
  // handful of workers the extra parallelism turns into seek contention.
  return std::clamp(cores > 0 ? cores / 4 : 1, 1, 4);
}

QString destinationKey(const QString &path) {
#if defined(Q_OS_WIN)
  return path.toCaseFolded();
#else
  return path;
#endif
}

// Cancellable, chunked copy. QFile::copy() cannot be interrupted, so a
// cancelled import would otherwise keep writing gigabytes to disk long after
// the user dismissed it, and leave a half-written file behind on failure.
bool copyCancellable(const QString &source, const QString &destination,
                     const std::atomic_bool *cancel) {
  QFile in(source);
  if (!in.open(QIODevice::ReadOnly))
    return false;
  QFile out(destination);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;

  const auto abandon = [&out]() {
    out.close();
    out.remove();
    return false;
  };

  QByteArray buffer(int(kCopyChunkBytes), Qt::Uninitialized);
  while (!in.atEnd()) {
    if (cancel && cancel->load(std::memory_order_acquire))
      return abandon();
    const qint64 read = in.read(buffer.data(), kCopyChunkBytes);
    if (read < 0)
      return abandon();
    if (read > 0 && out.write(buffer.constData(), read) != read)
      return abandon();
  }
  if (!out.flush())
    return abandon();
  out.close();
  return true;
}

} // namespace

MediaImportQueue::MediaImportQueue(QObject *parent)
    : QObject(parent), m_concurrency(defaultConcurrency()) {
  // One extra thread so the directory walk never queues behind the probes.
  m_pool.setMaxThreadCount(m_concurrency + 1);
  m_pool.setExpiryTimeout(30000);
  m_flushTimer.setInterval(kFlushIntervalMs);
  connect(&m_flushTimer, &QTimer::timeout, this, &MediaImportQueue::flush);
}

MediaImportQueue::~MediaImportQueue() { shutdown(); }

void MediaImportQueue::setProber(Prober prober) {
  m_prober = std::move(prober);
}

void MediaImportQueue::setExistingKeys(const QSet<QString> &keys) {
  m_ownerKeys = keys;
}

void MediaImportQueue::setMaximumConcurrency(int workers) {
  m_concurrency = std::clamp(workers, 1, 16);
  m_pool.setMaxThreadCount(m_concurrency + 1);
}

int MediaImportQueue::percent() const {
  if (m_total <= 0)
    return m_active ? 0 : 100;
  return std::clamp(int(qRound(100.0 * m_completed / m_total)), 0, 100);
}

bool MediaImportQueue::cancelled() const {
  return m_cancel && m_cancel->load(std::memory_order_acquire);
}

bool MediaImportQueue::start(const Request &request) {
  if (m_active || request.paths.isEmpty())
    return false;

  m_request = request;
  m_files.clear();
  m_slots.clear();
  m_nextIndex = 0;
  m_inFlight = 0;
  m_emitIndex = 0;
  m_total = 0;
  m_completed = 0;
  m_accepted = 0;
  m_skipped = 0;
  m_failures = 0;
  m_warningsEmitted = 0;
  m_active = true;
  m_scanning = true;
  // A fresh token per run: an abandoned run's workers keep the old one and stop
  // on their own without touching this run's state.
  m_cancel = std::make_shared<std::atomic_bool>(false);
  {
    QMutexLocker locker(&m_workerMutex);
    m_workerKeys = m_ownerKeys;
    m_reservedNames.clear();
  }
  emit progressChanged();

  const QStringList paths = request.paths;
  const MediaScan::Limits limits = request.scanLimits;
  const std::shared_ptr<std::atomic_bool> cancel = m_cancel;
  // The walk is I/O bound and can take seconds on a network share or an
  // external drive, which is exactly why it must not run on the GUI thread.
  (void)QtConcurrent::run(&m_pool, [this, paths, limits, cancel]() {
    const MediaScan::Result scan = MediaScan::expand(paths, limits, cancel.get());
    QMetaObject::invokeMethod(
        this,
        [this, scan, cancel]() {
          if (cancel != m_cancel)
            return;
          beginProbing(scan);
        },
        Qt::QueuedConnection);
  });
  return true;
}

void MediaImportQueue::cancel() {
  if (!m_active)
    return;
  if (m_cancel)
    m_cancel->store(true, std::memory_order_release);
  // While scanning, the scan callback ends the run; while probing, the last
  // in-flight completion does. With nothing outstanding, end it here.
  if (!m_scanning && m_inFlight == 0)
    finishRun();
}

void MediaImportQueue::shutdown() {
  if (m_cancel)
    m_cancel->store(true, std::memory_order_release);
  m_flushTimer.stop();
  // Blocks until no worker can touch this object again. Queued completions
  // still in the event loop are discarded by ~QObject.
  m_pool.waitForDone();
  m_active = false;
  m_scanning = false;
  m_inFlight = 0;
}

void MediaImportQueue::beginProbing(const MediaScan::Result &scan) {
  m_scanning = false;
  if (!scan.unreadableDirectories.isEmpty())
    emit warning(
        QStringLiteral("Some folders could not be read and were skipped: %1")
            .arg(scan.unreadableDirectories.join(QStringLiteral(", "))));
  const QString truncation = scan.truncationMessage();
  if (!truncation.isEmpty())
    emit warning(truncation);

  m_files = scan.files;
  m_total = m_files.size();
  if (cancelled() || m_files.isEmpty()) {
    finishRun();
    return;
  }

  m_slots.resize(m_total);
  emit started(m_total);
  emit progressChanged();
  m_flushTimer.start();
  dispatch();
}

void MediaImportQueue::dispatch() {
  while (!cancelled() && m_inFlight < m_concurrency &&
         m_nextIndex < m_files.size())
    submit(m_nextIndex++);
  // Nothing running and nothing left to start: every submitted task has already
  // reported back, so the run is over.
  if (m_inFlight == 0 && (cancelled() || m_nextIndex >= m_files.size()))
    finishRun();
}

void MediaImportQueue::flush() {
  while (true) {
    QVariantList batch;
    int consumed = 0;
    // Released in scan order so the bin fills as the user selected, even though
    // the probes finish out of order. A single slow file therefore holds back
    // the items behind it - the progress counter still advances, which is what
    // the user is watching.
    while (m_emitIndex < m_slots.size() && m_slots.at(m_emitIndex).done &&
           batch.size() < kMaxBatchSize) {
      if (m_slots.at(m_emitIndex).accepted)
        batch.append(m_slots.at(m_emitIndex).item);
      // Drop the probed map immediately: a 20k-file import must not hold every
      // result until the run ends.
      m_slots[m_emitIndex].item.clear();
      ++m_emitIndex;
      ++consumed;
    }
    if (!batch.isEmpty())
      emit itemsReady(batch);
    if (consumed == 0)
      return;
  }
}

void MediaImportQueue::completeSlot(int index, const QVariantMap &item,
                                    bool acceptedItem, const QString &failure) {
  if (index >= 0 && index < m_slots.size()) {
    Slot &slot = m_slots[index];
    slot.item = item;
    slot.accepted = acceptedItem;
    slot.done = true;
  }
  m_inFlight = qMax(0, m_inFlight - 1);
  ++m_completed;
  if (acceptedItem)
    ++m_accepted;
  else
    ++m_skipped;
  if (!failure.isEmpty()) {
    ++m_failures;
    if (m_warningsEmitted < kMaxWarnings) {
      ++m_warningsEmitted;
      emit warning(failure);
    }
  }
  emit progressChanged();
  flush();
  dispatch();
}

void MediaImportQueue::finishRun() {
  m_flushTimer.stop();
  flush();
  const bool wasCancelled = cancelled();
  const int accepted = m_accepted;
  const int skipped = m_skipped;
  const int unreported = m_failures - m_warningsEmitted;
  m_active = false;
  m_scanning = false;
  m_slots.clear();
  m_files.clear();
  if (unreported > 0)
    emit warning(QStringLiteral("%1 more files could not be imported.")
                     .arg(unreported));
  emit progressChanged();
  emit finished(accepted, skipped, wasCancelled);
}

bool MediaImportQueue::claimKey(const QString &key) {
  if (key.isEmpty())
    return false;
  QMutexLocker locker(&m_workerMutex);
  if (m_workerKeys.contains(key))
    return false;
  m_workerKeys.insert(key);
  return true;
}

QString MediaImportQueue::reserveCopyDestination(
    const QString &source, const QString &destinationFolder) {
  const QFileInfo info(source);
  QDir destination(destinationFolder);
  if (!destination.mkpath(QStringLiteral(".")))
    return {};
  const QString base = info.completeBaseName();
  const QString suffix = info.suffix();

  QMutexLocker locker(&m_workerMutex);
  // The reservation set is what makes concurrent copies safe: QFileInfo::exists
  // alone would let two workers agree on the same free name and race to write
  // it.
  for (int attempt = 0; attempt < 1000; ++attempt) {
    const QString name =
        attempt == 0
            ? info.fileName()
            : QStringLiteral("%1_%2%3")
                  .arg(base)
                  .arg(attempt)
                  .arg(suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix);
    const QString candidate = destination.filePath(name);
    const QString key = destinationKey(candidate);
    if (m_reservedNames.contains(key) || QFileInfo::exists(candidate))
      continue;
    m_reservedNames.insert(key);
    return candidate;
  }
  return {};
}

void MediaImportQueue::submit(int index) {
  ++m_inFlight;
  const QString source = m_files.at(index);
  const QString destinationFolder = m_request.copyDestination;
  const std::shared_ptr<std::atomic_bool> cancel = m_cancel;
  const Prober prober = m_prober;

  (void)QtConcurrent::run(&m_pool, [this, index, source, destinationFolder,
                                    cancel, prober]() {
    const auto stopping = [&cancel]() {
      return cancel->load(std::memory_order_acquire);
    };
    QVariantMap item;
    bool acceptedItem = false;
    QString failure;
    QString path;
    QString reason;

    if (stopping()) {
      // Abandoned before this task got a worker; report back so the run can
      // finish, but do no I/O.
    } else if (!MediaPath::isDecodable(source, &reason)) {
      // Missing, unreadable, empty, or a name this system cannot encode. libav
      // would report all of those much later as an unhelpful
      // "No such file or directory".
      failure = reason;
    } else if (destinationFolder.isEmpty()) {
      // Claim the key before probing: a file already in the bin must not cost
      // an ffprobe launch. A lost claim is a silent skip, not an error.
      if (claimKey(MediaPath::duplicateKey(source)))
        path = source;
    } else {
      const QString target = reserveCopyDestination(source, destinationFolder);
      if (target.isEmpty()) {
        failure = QStringLiteral("Could not create a place for %1 in the "
                                 "project media folder.")
                      .arg(QFileInfo(source).fileName());
      } else if (!copyCancellable(source, target, cancel.get())) {
        // A cancelled copy is not a failure worth telling the user about.
        if (!stopping())
          failure = QStringLiteral("Could not copy %1 into the project.")
                        .arg(QFileInfo(source).fileName());
      } else if (claimKey(MediaPath::duplicateKey(target))) {
        path = target;
      }
    }

    if (!path.isEmpty() && !stopping() && prober) {
      QVariantMap probed = prober(path);
      if (probed.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("unknown")) {
        item = std::move(probed);
        acceptedItem = true;
      } else {
        failure = QStringLiteral("%1 is not a supported media file.")
                      .arg(QFileInfo(path).fileName());
      }
    }

    QMetaObject::invokeMethod(
        this,
        [this, index, item, acceptedItem, failure, cancel]() {
          // A superseded run's completions must never disturb the current one.
          if (cancel != m_cancel)
            return;
          completeSlot(index, item, acceptedItem, failure);
        },
        Qt::QueuedConnection);
  });
}
