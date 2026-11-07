#include "app/performance/batch_progress.h"

#include <algorithm>
#include <utility>

namespace cutpro::perf {

BatchProgress::BatchProgress(QObject *owner, Publish publish, int intervalMs)
    : m_publish(std::move(publish)) {
  const int interval = std::max(0, intervalMs);
  m_pacer = std::make_unique<GesturePacer>(
      owner, [this]() { publishNow(false); }, interval, interval * 3 + 30);
  // The settle callback is the safety net for the trailing update: a caller that
  // never gets round to flush() still sees the final counts once reports stop.
  m_pacer->setOnSettled([this]() { publishNow(false); });
}

void BatchProgress::reset(int total, const QString &label) {
  m_total = std::max(0, total);
  m_done = 0;
  m_failed = 0;
  m_weightDone = 0;
  m_label = label;
  m_active = true;
  m_reported = m_total > 0 ? QBitArray(m_total) : QBitArray();
  // A new run has to publish even when its first snapshot happens to match what
  // the previous run left on screen, so make the comparison impossible to pass.
  m_lastPublished = Snapshot();
  m_lastPublished.done = -1;
}

void BatchProgress::begin(int total, const QString &label) {
  m_weights.clear();
  m_weightTotal = 0;
  reset(total, label);
  flush();
}

void BatchProgress::beginWeighted(const QVector<qint64> &weights,
                                  const QString &label) {
  m_weights = weights;
  m_weightTotal = 0;
  for (qint64 &weight : m_weights) {
    if (weight < 0)
      weight = 0;
    m_weightTotal += weight;
  }
  reset(int(m_weights.size()), label);
  flush();
}

void BatchProgress::step(int index, bool ok) {
  ++m_steps;
  if (!m_active)
    return;
  if (index >= 0 && index < m_reported.size()) {
    // Already counted. The worker overlaps requests, so a retry or a duplicated
    // line is normal and must not move the bar twice.
    if (m_reported.testBit(index))
      return;
    m_reported.setBit(index);
    m_weightDone += m_weights.value(index, 0);
  } else if (m_weightTotal > 0) {
    m_weightDone += m_total > 0 ? m_weightTotal / m_total : 0;
  }
  m_done = m_total > 0 ? std::min(m_total, m_done + 1) : m_done + 1;
  if (!ok)
    ++m_failed;
  m_weightDone = std::min(m_weightDone, m_weightTotal);
  m_pacer->request();
}

void BatchProgress::setLabel(const QString &label) {
  if (m_label == label)
    return;
  m_label = label;
  m_pacer->request();
}

void BatchProgress::flush() {
  if (m_pacer)
    m_pacer->requestNow();
  else
    publishNow(false);
}

void BatchProgress::finish(const QString &label) {
  if (!label.isEmpty())
    m_label = label;
  if (m_total > 0)
    m_done = m_total;
  if (!m_reported.isEmpty())
    m_reported.fill(true);
  m_weightDone = m_weightTotal;
  m_active = false;
  if (m_pacer)
    m_pacer->cancel();
  publishNow(true);
}

void BatchProgress::cancel(const QString &label) {
  if (!label.isEmpty())
    m_label = label;
  m_active = false;
  if (m_pacer)
    m_pacer->cancel();
  publishNow(true);
}

double BatchProgress::fraction() const {
  if (m_weightTotal > 0)
    return std::clamp(double(m_weightDone) / double(m_weightTotal), 0.0, 1.0);
  if (m_total > 0)
    return std::clamp(double(m_done) / double(m_total), 0.0, 1.0);
  // Nothing to measure. Zero rather than one, so a run that turned out to have
  // no items cannot flash a full bar; the label is what says it finished.
  return 0.0;
}

BatchProgress::Snapshot BatchProgress::snapshot() const {
  Snapshot snap;
  snap.done = m_done;
  snap.total = m_total;
  snap.failed = m_failed;
  snap.fraction = fraction();
  snap.label = m_label;
  snap.active = m_active;
  return snap;
}

void BatchProgress::publishNow(bool force) {
  if (!m_publish)
    return;
  const Snapshot snap = snapshot();
  if (!force && snap == m_lastPublished)
    return;
  m_lastPublished = snap;
  ++m_publishes;
  m_publish(snap);
}

QVariantMap BatchProgress::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("steps")] = m_steps;
  stats[QStringLiteral("publishes")] = m_publishes;
  stats[QStringLiteral("collapsed")] =
      m_steps > m_publishes ? m_steps - m_publishes : quint64(0);
  stats[QStringLiteral("done")] = m_done;
  stats[QStringLiteral("total")] = m_total;
  stats[QStringLiteral("failed")] = m_failed;
  stats[QStringLiteral("fraction")] = fraction();
  stats[QStringLiteral("active")] = m_active;
  stats[QStringLiteral("weighted")] = m_weightTotal > 0;
  if (m_pacer)
    stats[QStringLiteral("intervalMs")] = m_pacer->intervalMs();
  return stats;
}

} // namespace cutpro::perf
