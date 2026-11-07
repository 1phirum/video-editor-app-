#include "app/preview/keyframe_index.h"

#include <algorithm>

KeyframeIndex KeyframeIndex::fromTimestamps(QVector<qint64> timestampsMs,
                                            qint64 durationMs) {
  KeyframeIndex index;
  index.m_durationMs = qMax<qint64>(0, durationMs);
  std::sort(timestampsMs.begin(), timestampsMs.end());
  timestampsMs.erase(std::unique(timestampsMs.begin(), timestampsMs.end()),
                     timestampsMs.end());
  // Negative positions come out of containers whose first edit list entry starts
  // before zero; they are not seek targets.
  while (!timestampsMs.isEmpty() && timestampsMs.first() < 0)
    timestampsMs.removeFirst();
  if (timestampsMs.isEmpty())
    return estimated(kDefaultGopMs, durationMs);

  index.m_timestamps = std::move(timestampsMs);
  index.m_exact = true;

  // Median gap rather than the mean: one very long gap at the end of a file, or
  // a burst of keyframes around a scene cut, should not move the typical value.
  if (index.m_timestamps.size() >= 2) {
    QVector<qint64> gaps;
    gaps.reserve(index.m_timestamps.size() - 1);
    for (int i = 1; i < index.m_timestamps.size(); ++i)
      gaps.append(index.m_timestamps.at(i) - index.m_timestamps.at(i - 1));
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    index.m_gopMs = qMax<qint64>(1, gaps.at(gaps.size() / 2));
  } else {
    index.m_gopMs = kDefaultGopMs;
  }
  return index;
}

KeyframeIndex KeyframeIndex::estimated(qint64 gopMs, qint64 durationMs) {
  KeyframeIndex index;
  index.m_durationMs = qMax<qint64>(0, durationMs);
  index.m_gopMs = qMax<qint64>(1, gopMs > 0 ? gopMs : kDefaultGopMs);
  index.m_exact = false;
  return index;
}

qint64 KeyframeIndex::keyframeAtOrBefore(qint64 positionMs) const {
  const qint64 clamped = qMax<qint64>(0, positionMs);
  if (m_timestamps.isEmpty()) {
    const qint64 span = qMax<qint64>(1, m_gopMs);
    return clamped - (clamped % span);
  }
  // upper_bound gives the first entry strictly after the position, so the entry
  // before it is the one a backward seek lands on.
  const auto after =
      std::upper_bound(m_timestamps.cbegin(), m_timestamps.cend(), clamped);
  if (after == m_timestamps.cbegin())
    return m_timestamps.first();
  return *(after - 1);
}

qint64 KeyframeIndex::keyframeAfter(qint64 positionMs) const {
  const qint64 clamped = qMax<qint64>(0, positionMs);
  if (m_timestamps.isEmpty()) {
    const qint64 span = qMax<qint64>(1, m_gopMs);
    const qint64 next = clamped - (clamped % span) + span;
    if (m_durationMs > 0 && next >= m_durationMs)
      return -1;
    return next;
  }
  const auto after =
      std::upper_bound(m_timestamps.cbegin(), m_timestamps.cend(), clamped);
  if (after == m_timestamps.cend())
    return -1;
  return *after;
}

qint64 KeyframeIndex::toleranceMs() const {
  return qMax<qint64>(1, gopSpanMs() / 2);
}

QString KeyframeIndex::describe() const {
  if (m_exact)
    return QStringLiteral("%1 keyframes, ~%2 ms apart")
        .arg(m_timestamps.size())
        .arg(m_gopMs);
  return QStringLiteral("estimated, %1 ms apart").arg(m_gopMs);
}
