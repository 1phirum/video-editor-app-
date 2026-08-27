#include "app/preview/decode_work_governor.h"

#include "app/preview/gui_thread_watchdog.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QMutexLocker>
#include <QThread>
#include <QtGlobal>

namespace {
qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

int defaultSlots() {
  // A third of the machine, at least one, never more than three. The playback
  // decoder and the GUI thread need the rest, and a fourth concurrent seek on
  // one disk buys throughput nobody is waiting for.
  return qBound(1, qMax(2, QThread::idealThreadCount()) / 3, 3);
}
} // namespace

DecodeWorkGovernor::Admission::~Admission() {
  if (m_owner)
    m_owner->release();
}

DecodeWorkGovernor::Admission::Admission(Admission &&other) noexcept
    : m_owner(other.m_owner) {
  other.m_owner = nullptr;
}

DecodeWorkGovernor::Admission &
DecodeWorkGovernor::Admission::operator=(Admission &&other) noexcept {
  if (this == &other)
    return *this;
  if (m_owner)
    m_owner->release();
  m_owner = other.m_owner;
  other.m_owner = nullptr;
  return *this;
}

DecodeWorkGovernor::DecodeWorkGovernor() : m_slots(defaultSlots()) {}

DecodeWorkGovernor &DecodeWorkGovernor::instance() {
  static DecodeWorkGovernor governor;
  return governor;
}

bool DecodeWorkGovernor::holdingBackLocked() const {
  return m_interactionCount > 0 || m_interactionDeadlineMs > nowMs();
}

DecodeWorkGovernor::Admission DecodeWorkGovernor::tryAdmit(Class kind) {
  return admit(kind, 0);
}

DecodeWorkGovernor::Admission DecodeWorkGovernor::admit(Class kind,
                                                        int waitMs) {
  const bool background = kind != Class::Interactive;
  // The GUI thread is never allowed to park here. A slot that frees in two
  // seconds is a fair trade for a worker and a frozen window for this thread,
  // and a caller that reached the gate from a QML handler has no way to know it
  // is about to wait. Refusing is always safe: an ungranted admission is the
  // same "not now" a scroll already produces, and the request comes back.
  if (waitMs > 0 && GuiThreadWatchdog::onGuiThread()) {
    waitMs = 0;
    m_refusedGuiThread.fetch_add(1, std::memory_order_relaxed);
  }
  QMutexLocker locker(&m_mutex);
  if (background && holdingBackLocked()) {
    locker.unlock();
    m_refusedInteraction.fetch_add(1, std::memory_order_relaxed);
    return {};
  }

  // The interactive path is allowed one slot beyond the ceiling: the frame under
  // the playhead must not queue behind two thumbnails, and it is a single
  // worker, so the overshoot is bounded at one.
  const int ceiling = background ? m_slots : m_slots + 1;
  if (m_active < ceiling) {
    ++m_active;
    locker.unlock();
    m_granted.fetch_add(1, std::memory_order_relaxed);
    return Admission(this);
  }

  // Zero wait means try-acquire: no condition variable, no deadline, straight
  // back to the caller. Falling into the loop below with an already-expired
  // deadline would work too, but this keeps the GUI-thread path free of any
  // chance of touching QWaitCondition at all.
  if (waitMs <= 0) {
    locker.unlock();
    m_refusedBusy.fetch_add(1, std::memory_order_relaxed);
    return {};
  }

  QDeadlineTimer deadline(waitMs);
  while (m_active >= ceiling) {
    if (deadline.hasExpired() || !m_slotFreed.wait(&m_mutex, deadline)) {
      locker.unlock();
      m_refusedBusy.fetch_add(1, std::memory_order_relaxed);
      return {};
    }
    // A hold raised while this request waited applies to it too: the user has
    // started interacting since, so a thumbnail is no longer worth a core.
    if (background && holdingBackLocked()) {
      locker.unlock();
      m_refusedInteraction.fetch_add(1, std::memory_order_relaxed);
      return {};
    }
  }
  ++m_active;
  locker.unlock();
  m_granted.fetch_add(1, std::memory_order_relaxed);
  return Admission(this);
}

void DecodeWorkGovernor::release() {
  QMutexLocker locker(&m_mutex);
  if (m_active > 0)
    --m_active;
  m_slotFreed.wakeOne();
}

void DecodeWorkGovernor::beginInteraction() {
  {
    QMutexLocker locker(&m_mutex);
    ++m_interactionCount;
    m_interactionDeadlineMs = nowMs() + kInteractionLapseMs;
  }
  m_interactions.fetch_add(1, std::memory_order_relaxed);
}

void DecodeWorkGovernor::endInteraction() {
  QMutexLocker locker(&m_mutex);
  if (m_interactionCount > 0)
    --m_interactionCount;
  if (m_interactionCount == 0)
    m_interactionDeadlineMs = 0;
  // Held-back work is waiting on this: releasing the hold has to wake it, or the
  // filmstrip stays blank until the next repaint asks again.
  m_slotFreed.wakeAll();
}

void DecodeWorkGovernor::touchInteraction() {
  QMutexLocker locker(&m_mutex);
  if (m_interactionCount > 0)
    m_interactionDeadlineMs = nowMs() + kInteractionLapseMs;
}

bool DecodeWorkGovernor::interactionActive() const {
  QMutexLocker locker(&m_mutex);
  return holdingBackLocked();
}

void DecodeWorkGovernor::setConcurrency(int decodeSlots) {
  QMutexLocker locker(&m_mutex);
  m_slots = qBound(1, decodeSlots, 8);
  m_slotFreed.wakeAll();
}

int DecodeWorkGovernor::concurrency() const {
  QMutexLocker locker(&m_mutex);
  return m_slots;
}

QVariantMap DecodeWorkGovernor::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("decodeSlots")] = concurrency();
  {
    QMutexLocker locker(&m_mutex);
    stats[QStringLiteral("decodesInFlight")] = m_active;
    stats[QStringLiteral("interactionHolds")] = m_interactionCount;
    stats[QStringLiteral("interactionActive")] = holdingBackLocked();
  }
  stats[QStringLiteral("admissionsGranted")] =
      qulonglong(m_granted.load(std::memory_order_relaxed));
  stats[QStringLiteral("admissionsRefusedBusy")] =
      qulonglong(m_refusedBusy.load(std::memory_order_relaxed));
  stats[QStringLiteral("admissionsHeldForInteraction")] =
      qulonglong(m_refusedInteraction.load(std::memory_order_relaxed));
  // Any non-zero value here is a bug worth chasing: it means a decode request
  // reached the gate on the GUI thread, which is a stall that only the forced
  // try-acquire above kept from being visible.
  stats[QStringLiteral("admissionsFromGuiThread")] =
      qulonglong(m_refusedGuiThread.load(std::memory_order_relaxed));
  stats[QStringLiteral("interactionsSeen")] =
      qulonglong(m_interactions.load(std::memory_order_relaxed));
  return stats;
}
