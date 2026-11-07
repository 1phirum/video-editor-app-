#include "app/performance/gesture_pacer.h"

#include <QCoreApplication>
#include <QObject>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cutpro::perf {

GesturePacer::GesturePacer(QObject *owner, std::function<void()> apply,
                           int intervalMs, int settleMs)
    : m_apply(std::move(apply)), m_intervalMs(std::max(0, intervalMs)),
      m_settleMs(std::max(m_intervalMs, settleMs)) {
  QObject *context = owner ? owner : QCoreApplication::instance();
  if (!context)
    return;

  QTimer *timer = new QTimer(context);
  timer->setSingleShot(true);
  // A coarse timer may round a 16 ms wait up to 32, which turns a paced scroll
  // into a 30 Hz one.
  timer->setTimerType(Qt::PreciseTimer);
  QObject::connect(timer, &QTimer::timeout, timer, [this]() { fire(); });
  m_timer = timer;

  QTimer *settle = new QTimer(context);
  settle->setSingleShot(true);
  QObject::connect(settle, &QTimer::timeout, settle, [this]() {
    m_active = false;
    if (m_settled)
      m_settled();
  });
  m_settleTimer = settle;
}

GesturePacer::~GesturePacer() {
  // Both are parented to the owner, so they may already be gone; QPointer is
  // what makes that answerable rather than a use-after-free.
  delete m_timer;
  delete m_settleTimer;
}

void GesturePacer::request() {
  ++m_requests;
  m_active = true;
  if (m_settleTimer)
    m_settleTimer->start(m_settleMs);
  if (!m_apply || m_pending)
    return;

  // No clock yet means nothing has run, so the first request in the process
  // applies immediately - a gesture must never wait a frame to start moving.
  const qint64 elapsed =
      m_sinceApply.isValid() ? m_sinceApply.elapsed() : qint64(m_intervalMs);
  if (elapsed >= m_intervalMs || !m_timer) {
    fire();
    return;
  }
  m_pending = true;
  m_timer->start(int(m_intervalMs - elapsed));
}

void GesturePacer::requestNow() {
  ++m_requests;
  m_active = true;
  if (m_settleTimer)
    m_settleTimer->start(m_settleMs);
  if (m_timer)
    m_timer->stop();
  fire();
}

void GesturePacer::cancel() {
  m_pending = false;
  m_active = false;
  if (m_timer)
    m_timer->stop();
  if (m_settleTimer)
    m_settleTimer->stop();
}

void GesturePacer::fire() {
  m_pending = false;
  m_sinceApply.restart();
  ++m_applies;
  if (m_apply)
    m_apply();
}

void GesturePacer::setOnSettled(std::function<void()> settled) {
  m_settled = std::move(settled);
}

void GesturePacer::setIntervalMs(int intervalMs) {
  m_intervalMs = std::max(0, intervalMs);
  m_settleMs = std::max(m_intervalMs, m_settleMs);
}

void GesturePacer::setSettleMs(int settleMs) {
  m_settleMs = std::max(m_intervalMs, settleMs);
}

QVariantMap GesturePacer::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("requests")] = m_requests;
  stats[QStringLiteral("applies")] = m_applies;
  stats[QStringLiteral("collapsed")] =
      m_requests > m_applies ? m_requests - m_applies : quint64(0);
  stats[QStringLiteral("intervalMs")] = m_intervalMs;
  stats[QStringLiteral("settleMs")] = m_settleMs;
  stats[QStringLiteral("active")] = m_active;
  return stats;
}

ViewportGate::ViewportGate(double marginFraction, qint64 minMarginMs)
    : m_marginFraction(marginFraction > 0.0 ? marginFraction : 0.0),
      m_minMarginMs(std::max<qint64>(0, minMarginMs)) {}

qint64 ViewportGate::marginMs() const {
  if (!m_valid)
    return m_minMarginMs;
  const qint64 width = m_endMs > m_startMs ? m_endMs - m_startMs : 0;
  const qint64 fraction = qint64(double(width) * m_marginFraction);
  return std::max(m_minMarginMs, fraction);
}

bool ViewportGate::covers(qint64 startMs, qint64 endMs,
                          double msPerPixel) const {
  if (!m_valid)
    return false;
  // A scale change means a different collapse threshold and therefore a
  // different set of rows, however little the window itself moved. Compared
  // exactly on purpose: the caller derives this from a zoom level that only
  // changes when something changed it, so there is no drift to tolerate.
  if (msPerPixel != m_msPerPixel)
    return false;
  const qint64 margin = marginMs();
  return startMs >= m_startMs - margin && endMs <= m_endMs + margin;
}

bool ViewportGate::moved(qint64 startMs, qint64 endMs, double msPerPixel) {
  if (covers(startMs, endMs, msPerPixel)) {
    ++m_rejected;
    return false;
  }
  m_startMs = startMs;
  m_endMs = endMs;
  m_msPerPixel = msPerPixel;
  m_valid = true;
  ++m_accepted;
  return true;
}

void ViewportGate::reset() {
  m_valid = false;
  m_startMs = 0;
  m_endMs = 0;
  m_msPerPixel = 0.0;
}

QVariantMap ViewportGate::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("accepted")] = m_accepted;
  stats[QStringLiteral("rejected")] = m_rejected;
  stats[QStringLiteral("startMs")] = m_startMs;
  stats[QStringLiteral("endMs")] = m_endMs;
  stats[QStringLiteral("marginMs")] = marginMs();
  stats[QStringLiteral("msPerPixel")] = m_msPerPixel;
  return stats;
}

} // namespace cutpro::perf
