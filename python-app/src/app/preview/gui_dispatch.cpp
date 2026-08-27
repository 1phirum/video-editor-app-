#include "app/preview/gui_dispatch.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QMutexLocker>

#include <atomic>

namespace {

std::atomic<quint64> g_posted{0};
std::atomic<quint64> g_superseded{0};
std::atomic<quint64> g_drains{0};
std::atomic<quint64> g_deferredDrains{0};
std::atomic<quint64> g_cancelled{0};

struct Entry {
  const char *key = nullptr;
  std::function<void()> work;
};

// Deliberately not a QObject. Wakes are posted against the application object,
// which is guaranteed to live on the main thread, so this queue has no thread
// affinity of its own and a worker that posts before install() still lands its
// work on the GUI thread instead of nowhere.
class GuiPump final {
public:
  static GuiPump &instance() {
    static GuiPump pump;
    return pump;
  }

  void add(const char *key, std::function<void()> work) {
    if (!work)
      return;
    bool wakeNeeded = false;
    {
      QMutexLocker locker(&m_mutex);
      g_posted.fetch_add(1, std::memory_order_relaxed);
      bool replaced = false;
      if (key) {
        // Pointer comparison: keys are string literals, and the linear scan is
        // over the handful of distinct keys the preview code uses.
        for (Entry &pending : m_queue) {
          if (pending.key != key)
            continue;
          // Newest wins, and it keeps the original position: a superseded post
          // should not be able to jump the queue ahead of unrelated work that
          // was already waiting.
          pending.work = std::move(work);
          replaced = true;
          g_superseded.fetch_add(1, std::memory_order_relaxed);
          break;
        }
      }
      if (!replaced)
        m_queue.append(Entry{key, std::move(work)});
      // While a drain is in flight the flag stays raised: work added now is
      // picked up by the loop that is already running, so a second wake would
      // only cost an empty trip through the event loop.
      wakeNeeded = !m_wakePosted;
      m_wakePosted = true;
    }
    if (wakeNeeded)
      wake();
  }

  bool remove(const char *key) {
    if (!key)
      return false;
    // The closure is destroyed after the lock is dropped: it owns arbitrary
    // captures, and running a destructor that re-enters this class while holding
    // the mutex would deadlock.
    std::function<void()> doomed;
    {
      QMutexLocker locker(&m_mutex);
      for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).key != key)
          continue;
        doomed = std::move(m_queue[i].work);
        m_queue.removeAt(i);
        g_cancelled.fetch_add(1, std::memory_order_relaxed);
        break;
      }
    }
    return static_cast<bool>(doomed);
  }

  void drain() {
    g_drains.fetch_add(1, std::memory_order_relaxed);
    QElapsedTimer timer;
    timer.start();
    for (;;) {
      Entry entry;
      {
        QMutexLocker locker(&m_mutex);
        if (m_queue.isEmpty()) {
          m_wakePosted = false;
          return;
        }
        if (timer.elapsed() >= GuiDispatch::kDrainBudgetMs) {
          // Budget spent. Leave the flag raised and post a fresh wake, so the
          // event loop gets a chance to repaint between the two halves.
          g_deferredDrains.fetch_add(1, std::memory_order_relaxed);
          locker.unlock();
          wake();
          return;
        }
        entry = m_queue.takeFirst();
      }
      // Outside the lock: the work is arbitrary GUI code and will re-enter this
      // class the moment it touches anything that publishes.
      if (entry.work)
        entry.work();
    }
  }

private:
  static void wake() {
    QObject *context = QCoreApplication::instance();
    if (!context)
      return;
    QMetaObject::invokeMethod(
        context, []() { GuiPump::instance().drain(); }, Qt::QueuedConnection);
  }

  QMutex m_mutex;
  QList<Entry> m_queue;
  bool m_wakePosted = false;
};

} // namespace

void GuiDispatch::postCoalesced(const char *key, std::function<void()> work) {
  GuiPump::instance().add(key, std::move(work));
}

void GuiDispatch::post(std::function<void()> work) {
  GuiPump::instance().add(nullptr, std::move(work));
}

bool GuiDispatch::cancel(const char *key) {
  return GuiPump::instance().remove(key);
}

void GuiDispatch::install() { GuiPump::instance(); }

QVariantMap GuiDispatch::statistics() {
  QVariantMap stats;
  stats[QStringLiteral("guiPosts")] =
      static_cast<qulonglong>(g_posted.load(std::memory_order_relaxed));
  stats[QStringLiteral("guiSupersededPosts")] =
      static_cast<qulonglong>(g_superseded.load(std::memory_order_relaxed));
  stats[QStringLiteral("guiDrains")] =
      static_cast<qulonglong>(g_drains.load(std::memory_order_relaxed));
  stats[QStringLiteral("guiDeferredDrains")] =
      static_cast<qulonglong>(g_deferredDrains.load(std::memory_order_relaxed));
  stats[QStringLiteral("guiCancelledPosts")] =
      static_cast<qulonglong>(g_cancelled.load(std::memory_order_relaxed));
  return stats;
}
