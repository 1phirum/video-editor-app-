#include "app/performance/lane_neighbors.h"

#include <algorithm>
#include <limits>

namespace cutpro::perf {
namespace {

// About 35 000 years in milliseconds. Tolerances and snap windows are derived
// from a zoom level, and a zoom level near zero produces an enormous one; this
// keeps `time - tolerance` from wrapping without capping anything a real
// sequence could ask for.
constexpr qint64 kTimeGuard = qint64(1) << 50;

qint64 safeWindow(qint64 windowMs) {
  return windowMs < 0 ? 0 : std::min(windowMs, kTimeGuard);
}

// Nearest value in a sorted, ascending array within `windowMs` of timeMs.
bool nearestIn(const QVector<qint64> &values, qint64 timeMs, qint64 windowMs,
               qint64 *outValue) {
  if (values.isEmpty())
    return false;
  const auto it = std::lower_bound(values.cbegin(), values.cend(), timeMs);
  bool found = false;
  qint64 best = 0;
  qint64 bestDelta = 0;
  if (it != values.cend() && *it - timeMs <= windowMs) {
    found = true;
    best = *it;
    bestDelta = *it - timeMs;
  }
  if (it != values.cbegin()) {
    const qint64 previous = *(it - 1);
    const qint64 delta = timeMs - previous;
    // Strictly closer, so an exact tie keeps the later edge. Arbitrary but
    // deterministic, which is what matters when a drag re-asks every frame.
    if (delta <= windowMs && (!found || delta < bestDelta)) {
      found = true;
      best = previous;
    }
  }
  if (found && outValue)
    *outValue = best;
  return found;
}

} // namespace

void LaneNeighbors::build(const QVector<LaneSpan> &spans) {
  clear();
  m_spans = spans;
  m_edges.reserve(m_spans.size() * 2);

  for (int i = 0; i < m_spans.size(); ++i) {
    const LaneSpan &span = m_spans.at(i);
    // Covers no time: no edge to snap to, no interior to collide with.
    if (span.endMs <= span.startMs)
      continue;
    int laneIdx = m_laneIndex.value(span.lane, -1);
    if (laneIdx < 0) {
      laneIdx = int(m_laneData.size());
      m_laneData.append(Lane{});
      m_laneIndex.insert(span.lane, laneIdx);
    }
    Lane &lane = m_laneData[laneIdx];
    lane.byStart.append(i);
    lane.byEnd.append(i);
    m_edges.append(span.startMs);
    m_edges.append(span.endMs);
  }

  const QVector<LaneSpan> &all = m_spans;
  for (Lane &lane : m_laneData) {
    std::sort(lane.byStart.begin(), lane.byStart.end(),
              [&all](int left, int right) {
                const LaneSpan &a = all.at(left);
                const LaneSpan &b = all.at(right);
                if (a.startMs != b.startMs)
                  return a.startMs < b.startMs;
                return a.endMs < b.endMs;
              });
    std::sort(lane.byEnd.begin(), lane.byEnd.end(),
              [&all](int left, int right) {
                const LaneSpan &a = all.at(left);
                const LaneSpan &b = all.at(right);
                if (a.endMs != b.endMs)
                  return a.endMs < b.endMs;
                return a.startMs < b.startMs;
              });

    const int count = int(lane.byStart.size());
    lane.startKeys.resize(count);
    lane.endKeys.resize(count);
    lane.maxEndPrefix.resize(count);
    qint64 runningMax = std::numeric_limits<qint64>::min();
    for (int k = 0; k < count; ++k) {
      const LaneSpan &byStart = all.at(lane.byStart.at(k));
      lane.startKeys[k] = byStart.startMs;
      runningMax = std::max(runningMax, byStart.endMs);
      lane.maxEndPrefix[k] = runningMax;
      lane.endKeys[k] = all.at(lane.byEnd.at(k)).endMs;
    }
  }

  std::sort(m_edges.begin(), m_edges.end());
  m_edges.erase(std::unique(m_edges.begin(), m_edges.end()), m_edges.end());
}

void LaneNeighbors::clear() {
  m_spans.clear();
  m_laneData.clear();
  m_laneIndex.clear();
  m_edges.clear();
}

const LaneNeighbors::Lane *LaneNeighbors::laneFor(quint32 lane) const {
  const auto it = m_laneIndex.constFind(lane);
  return it == m_laneIndex.constEnd() ? nullptr : &m_laneData.at(*it);
}

int LaneNeighbors::lastEndingAtOrBefore(quint32 laneKey, qint64 timeMs,
                                        int exceptPos) const {
  const Lane *lane = laneFor(laneKey);
  if (!lane)
    return -1;
  const auto it =
      std::upper_bound(lane->endKeys.cbegin(), lane->endKeys.cend(), timeMs);
  for (int k = int(it - lane->endKeys.cbegin()) - 1; k >= 0; --k) {
    const int position = lane->byEnd.at(k);
    if (position != exceptPos)
      return position;
  }
  return -1;
}

int LaneNeighbors::firstStartingAtOrAfter(quint32 laneKey, qint64 timeMs,
                                          int exceptPos) const {
  const Lane *lane = laneFor(laneKey);
  if (!lane)
    return -1;
  const auto it =
      std::lower_bound(lane->startKeys.cbegin(), lane->startKeys.cend(), timeMs);
  const int count = int(lane->byStart.size());
  for (int k = int(it - lane->startKeys.cbegin()); k < count; ++k) {
    const int position = lane->byStart.at(k);
    if (position != exceptPos)
      return position;
  }
  return -1;
}

bool LaneNeighbors::touchesBefore(quint32 laneKey, qint64 startMs,
                                  qint64 toleranceMs, int exceptPos) const {
  const Lane *lane = laneFor(laneKey);
  if (!lane)
    return false;
  const qint64 tolerance = safeWindow(toleranceMs);
  const qint64 lowMs = startMs - tolerance;
  const qint64 highMs = startMs + tolerance;
  const auto it =
      std::lower_bound(lane->endKeys.cbegin(), lane->endKeys.cend(), lowMs);
  const int count = int(lane->endKeys.size());
  for (int k = int(it - lane->endKeys.cbegin());
       k < count && lane->endKeys.at(k) <= highMs; ++k) {
    if (lane->byEnd.at(k) != exceptPos)
      return true;
  }
  return false;
}

bool LaneNeighbors::touchesAfter(quint32 laneKey, qint64 endMs,
                                 qint64 toleranceMs, int exceptPos) const {
  const Lane *lane = laneFor(laneKey);
  if (!lane)
    return false;
  const qint64 tolerance = safeWindow(toleranceMs);
  const qint64 lowMs = endMs - tolerance;
  const qint64 highMs = endMs + tolerance;
  const auto it =
      std::lower_bound(lane->startKeys.cbegin(), lane->startKeys.cend(), lowMs);
  const int count = int(lane->startKeys.size());
  for (int k = int(it - lane->startKeys.cbegin());
       k < count && lane->startKeys.at(k) <= highMs; ++k) {
    if (lane->byStart.at(k) != exceptPos)
      return true;
  }
  return false;
}

bool LaneNeighbors::occupied(quint32 laneKey, qint64 startMs, qint64 endMs,
                             int exceptPos) const {
  if (endMs <= startMs)
    return false;
  const Lane *lane = laneFor(laneKey);
  if (!lane)
    return false;
  // Everything at or after this position starts at or after endMs, so it cannot
  // overlap a half-open range ending there.
  const auto it =
      std::lower_bound(lane->startKeys.cbegin(), lane->startKeys.cend(), endMs);
  for (int k = int(it - lane->startKeys.cbegin()) - 1; k >= 0; --k) {
    // Nothing in [0..k] reaches startMs, so nothing earlier can either.
    if (lane->maxEndPrefix.at(k) <= startMs)
      break;
    const int position = lane->byStart.at(k);
    if (position != exceptPos && m_spans.at(position).endMs > startMs)
      return true;
  }
  return false;
}

int LaneNeighbors::overlapping(quint32 laneKey, qint64 fromMs, qint64 toMs,
                               QVector<int> *out) const {
  if (out)
    out->clear();
  if (!out || toMs <= fromMs)
    return 0;
  const Lane *lane = laneFor(laneKey);
  if (!lane)
    return 0;
  const auto it =
      std::lower_bound(lane->startKeys.cbegin(), lane->startKeys.cend(), toMs);
  for (int k = int(it - lane->startKeys.cbegin()) - 1; k >= 0; --k) {
    if (lane->maxEndPrefix.at(k) <= fromMs)
      break;
    const int position = lane->byStart.at(k);
    if (m_spans.at(position).endMs > fromMs)
      out->append(position);
  }
  // The walk went backwards; callers want ascending order.
  std::reverse(out->begin(), out->end());
  return int(out->size());
}

qint64 LaneNeighbors::nearestEdge(qint64 timeMs, qint64 windowMs,
                                  qint64 fallback) const {
  qint64 edge = 0;
  return nearestIn(m_edges, timeMs, safeWindow(windowMs), &edge) ? edge
                                                                 : fallback;
}

qint64 LaneNeighbors::nearestEdgeOn(quint32 laneKey, qint64 timeMs,
                                    qint64 windowMs, qint64 fallback) const {
  const Lane *lane = laneFor(laneKey);
  if (!lane)
    return fallback;
  const qint64 window = safeWindow(windowMs);
  qint64 fromStarts = 0;
  qint64 fromEnds = 0;
  const bool hasStart = nearestIn(lane->startKeys, timeMs, window, &fromStarts);
  const bool hasEnd = nearestIn(lane->endKeys, timeMs, window, &fromEnds);
  if (!hasStart && !hasEnd)
    return fallback;
  if (!hasStart)
    return fromEnds;
  if (!hasEnd)
    return fromStarts;
  return qAbs(fromStarts - timeMs) <= qAbs(fromEnds - timeMs) ? fromStarts
                                                             : fromEnds;
}

} // namespace cutpro::perf
