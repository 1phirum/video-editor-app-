#include "app/preview/timeline_preview_prefetcher.h"

#include "app/preview/audio_peak_window_service.h"
#include "app/preview/decode_work_governor.h"
#include "app/preview/timeline_thumbnail_service.h"

#include <QDeadlineTimer>
#include <QMutexLocker>
#include <QThread>

// The decoding thread. One, deliberately: the point of routing every timeline
// picture through here is that exactly one background decode can be in flight,
// so the GUI thread never has to win a core back from a fan-out it did not ask
// for. Throughput is not the goal - a strip that fills in a second and a half
// while the window stays responsive beats one that fills in half a second while
// Windows greys the title bar.
class TimelinePreviewPrefetcher::Worker final : public QThread {
public:
  explicit Worker(TimelinePreviewPrefetcher *owner)
      : QThread(nullptr), m_owner(owner) {}

protected:
  void run() override {
    forever {
      const Item item = m_owner->takeNext();
      if (!item.valid)
        return;
      m_owner->decode(item);
    }
  }

private:
  TimelinePreviewPrefetcher *m_owner = nullptr;
};

TimelinePreviewPrefetcher &TimelinePreviewPrefetcher::instance() {
  // First touched from a QML handler, so the object - and therefore its notify
  // timer - lives on the GUI thread.
  static TimelinePreviewPrefetcher prefetcher;
  return prefetcher;
}

TimelinePreviewPrefetcher::TimelinePreviewPrefetcher() {
  m_notify.setInterval(kNotifyIntervalMs);
  m_notify.setSingleShot(false);
  connect(&m_notify, &QTimer::timeout, this, [this]() {
    const int revision = m_revision.load(std::memory_order_acquire);
    if (revision == m_notifiedRevision) {
      if (++m_quietTicks >= kQuietTicksBeforeIdle)
        m_notify.stop();
      return;
    }
    m_notifiedRevision = revision;
    m_quietTicks = 0;
    emit progressed();
  });
}

TimelinePreviewPrefetcher::~TimelinePreviewPrefetcher() {
  {
    QMutexLocker locker(&m_mutex);
    m_stopping = true;
    m_wishes.clear();
    m_order.clear();
    m_work.wakeAll();
  }
  if (m_worker) {
    m_worker->wait();
    delete m_worker;
    m_worker = nullptr;
  }
}

bool TimelinePreviewPrefetcher::sameShape(const Wish &a, const Wish &b) {
  return a.kind == b.kind && a.spanMs == b.spanMs && a.columns == b.columns &&
         a.path == b.path && a.positions == b.positions;
}

void TimelinePreviewPrefetcher::requestTiles(const QString &requesterId,
                                             const QString &path,
                                             const QVector<qint64> &bucketsMs) {
  Wish wish;
  wish.kind = Kind::Tile;
  wish.path = path;
  wish.positions = bucketsMs;
  submit(requesterId, std::move(wish));
}

void TimelinePreviewPrefetcher::requestWindows(const QString &requesterId,
                                               const QString &path,
                                               const QVector<qint64> &startsMs,
                                               qint64 spanMs, int columns) {
  if (spanMs <= 0)
    return;
  Wish wish;
  wish.kind = Kind::WaveWindow;
  wish.path = path;
  wish.positions = startsMs;
  wish.spanMs = spanMs;
  wish.columns = columns;
  submit(requesterId, std::move(wish));
}

void TimelinePreviewPrefetcher::submit(const QString &requesterId, Wish wish) {
  if (requesterId.isEmpty() || wish.path.isEmpty() || wish.positions.isEmpty())
    return;
  if (wish.positions.size() > kMaximumPositions)
    wish.positions.resize(kMaximumPositions);

  {
    QMutexLocker locker(&m_mutex);
    if (m_stopping)
      return;
    const auto found = m_wishes.find(requesterId);
    if (found != m_wishes.end() && sameShape(*found, wish)) {
      // The heartbeat re-issue. Keep the cursor so a sweep in progress is not
      // restarted from the left; rewind a spent one so anything a gate refused
      // the first time round is attempted again.
      if (found->next >= found->positions.size())
        found->next = 0;
    } else {
      const bool arriving = found == m_wishes.end();
      m_wishes.insert(requesterId, wish);
      if (arriving)
        m_order.append(requesterId);
    }

    // Only reachable if QML leaked requester ids - a clip layer cancels its own
    // on destruction. Oldest first, so what is on screen survives.
    while (m_order.size() > kMaximumWishes) {
      const QString stale = m_order.takeFirst();
      if (stale != requesterId)
        m_wishes.remove(stale);
    }
    if (m_cursor >= m_order.size())
      m_cursor = 0;

    if (!m_worker) {
      m_worker = new Worker(this);
      m_worker->setObjectName(QStringLiteral("cutpro-preview-prefetch"));
      // Below the GUI thread. Windows schedules a normal-priority decoder
      // against the thread that has to paint, and losing that race is what the
      // user sees as a freeze.
      m_worker->start(QThread::LowPriority);
    }
    m_work.wakeAll();
  }

  if (!m_notify.isActive()) {
    m_quietTicks = 0;
    m_notify.start();
  }
}

void TimelinePreviewPrefetcher::cancel(const QString &requesterId) {
  QMutexLocker locker(&m_mutex);
  m_wishes.remove(requesterId);
  // Left in m_order on purpose: removing from the middle of the list would move
  // the cursor onto a different wish. Pruned when it is next walked over.
}

void TimelinePreviewPrefetcher::clear() {
  QMutexLocker locker(&m_mutex);
  m_wishes.clear();
  m_order.clear();
  m_cursor = 0;
}

TimelinePreviewPrefetcher::Item TimelinePreviewPrefetcher::takeNext() {
  QMutexLocker locker(&m_mutex);
  forever {
    if (m_stopping)
      return {};

    // Nothing is started while the user is dragging, trimming or scrubbing. What
    // is already decoded stays on screen - the strip is not torn down for the
    // length of a gesture any more, it just stops growing.
    if (DecodeWorkGovernor::instance().interactionActive()) {
      m_work.wait(&m_mutex, QDeadlineTimer(kInteractionPollMs));
      continue;
    }

    const int count = m_order.size();
    for (int step = 0; step < count; ++step) {
      const int at = (m_cursor + step) % count;
      const QString &requesterId = m_order.at(at);
      const auto found = m_wishes.find(requesterId);
      if (found == m_wishes.end())
        continue;
      Wish &wish = *found;
      if (wish.next < 0 || wish.next >= wish.positions.size())
        continue;

      Item item;
      item.valid = true;
      item.kind = wish.kind;
      item.path = wish.path;
      item.positionMs = wish.positions.at(wish.next);
      item.spanMs = wish.spanMs;
      item.columns = wish.columns;
      ++wish.next;
      // Advance past the wish we just served, so the next item comes from the
      // next clip. A clip whose slice is thirty tiles deep therefore interleaves
      // with its neighbours instead of finishing first.
      m_cursor = (at + 1) % count;
      return item;
    }

    // Every wish is spent. Drop the ids that no longer name one, so the list
    // does not grow across a session of scrolling.
    for (int index = m_order.size() - 1; index >= 0; --index) {
      if (!m_wishes.contains(m_order.at(index)))
        m_order.removeAt(index);
    }
    m_cursor = 0;
    m_work.wait(&m_mutex, QDeadlineTimer(kIdlePollMs));
  }
}

void TimelinePreviewPrefetcher::decode(const Item &item) {
  if (item.kind == Kind::Tile) {
    TimelineThumbnailService &service = TimelineThumbnailService::instance();
    if (service.hasTile(item.path, item.positionMs)) {
      m_alreadyHeld.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    const TimelineThumbnailService::Tile tile =
        service.tile(item.path, item.positionMs, nullptr);
    if (tile.valid()) {
      m_decoded.fetch_add(1, std::memory_order_relaxed);
      noteLanded();
    } else if (tile.cancelled) {
      m_refused.fetch_add(1, std::memory_order_relaxed);
    } else {
      m_failed.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }

  AudioPeakWindowService &service = AudioPeakWindowService::instance();
  if (service.hasWindow(item.path, item.positionMs, item.spanMs,
                        item.columns)) {
    m_alreadyHeld.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const AudioPeakWindowService::Window window = service.window(
      item.path, item.positionMs, item.spanMs, item.columns, nullptr);
  if (window.valid()) {
    m_decoded.fetch_add(1, std::memory_order_relaxed);
    noteLanded();
  } else if (window.cancelled) {
    m_refused.fetch_add(1, std::memory_order_relaxed);
  } else {
    m_failed.fetch_add(1, std::memory_order_relaxed);
  }
}

void TimelinePreviewPrefetcher::noteLanded() {
  m_revision.fetch_add(1, std::memory_order_release);
  // The GUI thread's notify timer picks this up. Deliberately not a queued
  // signal per item: a burst of completions would then be a burst of QML
  // re-evaluations, which is the cost this whole file exists to avoid.
}

QVariantMap TimelinePreviewPrefetcher::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("prefetchRevision")] =
      m_revision.load(std::memory_order_relaxed);
  stats[QStringLiteral("prefetchDecoded")] =
      qulonglong(m_decoded.load(std::memory_order_relaxed));
  stats[QStringLiteral("prefetchAlreadyHeld")] =
      qulonglong(m_alreadyHeld.load(std::memory_order_relaxed));
  stats[QStringLiteral("prefetchRefused")] =
      qulonglong(m_refused.load(std::memory_order_relaxed));
  stats[QStringLiteral("prefetchFailed")] =
      qulonglong(m_failed.load(std::memory_order_relaxed));
  QMutexLocker locker(&m_mutex);
  int pending = 0;
  for (auto it = m_wishes.cbegin(); it != m_wishes.cend(); ++it)
    pending += qMax(0, it->positions.size() - it->next);
  stats[QStringLiteral("prefetchWishes")] = m_wishes.size();
  stats[QStringLiteral("prefetchPending")] = pending;
  return stats;
}
