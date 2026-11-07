#include "app/core_app/signal_coalescer.h"

#include "app/preview/gui_dispatch.h"

SignalCoalescer::SignalCoalescer(const char *key, std::function<void()> emitter)
    : m_key(key), m_emitter(std::move(emitter)) {}

SignalCoalescer::~SignalCoalescer() {
  // A queued closure captured `this`. If it were left in the queue it would run
  // against a destroyed object on the next drain, which is a use-after-free with
  // a delay on it - the worst kind to debug, because the crash happens in the
  // event loop and names nothing that is still alive.
  if (m_pending.load(std::memory_order_acquire))
    GuiDispatch::cancel(m_key);
}

void SignalCoalescer::schedule() {
  m_requests.fetch_add(1, std::memory_order_relaxed);
  if (!m_emitter)
    return;
  // Already queued: the pending notification will carry this change too, because
  // a notify signal says "re-read", not "here is a value".
  if (m_pending.exchange(true, std::memory_order_acq_rel))
    return;
  GuiDispatch::postCoalesced(m_key, [this]() { deliver(); });
}

void SignalCoalescer::flush() {
  if (!m_pending.load(std::memory_order_acquire))
    return;
  // Retract before emitting, not after: the emit runs QML bindings that can call
  // straight back into the owner and schedule again, and that new request has to
  // survive. Cancelling afterwards would silently swallow it.
  GuiDispatch::cancel(m_key);
  deliver();
}

void SignalCoalescer::discard() {
  if (m_pending.exchange(false, std::memory_order_acq_rel))
    GuiDispatch::cancel(m_key);
}

bool SignalCoalescer::pending() const {
  return m_pending.load(std::memory_order_acquire);
}

void SignalCoalescer::deliver() {
  // Cleared first so an emit that re-enters schedule() - a binding reacting by
  // marking something else dirty - queues a fresh notification instead of being
  // folded into the one currently being delivered and lost.
  m_pending.store(false, std::memory_order_release);
  m_emissions.fetch_add(1, std::memory_order_relaxed);
  if (m_emitter)
    m_emitter();
}

QVariantMap SignalCoalescer::statistics() const {
  const quint64 requests = m_requests.load(std::memory_order_relaxed);
  const quint64 emissions = m_emissions.load(std::memory_order_relaxed);
  QVariantMap stats;
  stats[QStringLiteral("requests")] = static_cast<qulonglong>(requests);
  stats[QStringLiteral("emissions")] = static_cast<qulonglong>(emissions);
  stats[QStringLiteral("collapsed")] =
      static_cast<qulonglong>(requests > emissions ? requests - emissions : 0);
  stats[QStringLiteral("pending")] = pending();
  return stats;
}
