#include "app/performance/span_table.h"

#include <algorithm>
#include <limits>

namespace cutpro::perf {
namespace {

constexpr qint64 kMaxInt64 = std::numeric_limits<qint64>::max();
// Ceiling on a collapse threshold. solve() quadruples, and projectRange()
// multiplies the threshold again by the gap factor, so leaving headroom below
// the type's maximum is what keeps both of those from wrapping into a negative
// gap that would fold two clips an hour apart into one bar.
constexpr qint64 kMaxThreshold = kMaxInt64 / 8;

qint64 saturatingMul(qint64 value, int factor) {
  if (value <= 0 || factor <= 0)
    return 0;
  return value > kMaxInt64 / factor ? kMaxInt64 : value * qint64(factor);
}

quint16 internInto(QHash<QString, quint16> *table, const QString &text) {
  const auto it = table->constFind(text);
  if (it != table->constEnd())
    return *it;
  // Ids start at 1 so that pack() can never produce 0 for a real lane, which
  // leaves 0 free to mean "no such lane" for lookup(). Saturating at the top
  // rather than wrapping: 65 534 distinct tracks is not a state this app can
  // reach, but wrapping would silently merge two lanes and a saturated id only
  // merges the last two.
  const qsizetype next = table->size() + 1;
  const quint16 id = next < 0xffff ? quint16(next) : quint16(0xffff);
  table->insert(text, id);
  return id;
}

} // namespace

quint32 LaneKeys::keyFor(const QString &track, const QString &kind) {
  return pack(internInto(&m_tracks, track), internInto(&m_kinds, kind));
}

quint32 LaneKeys::lookup(const QString &track, const QString &kind) const {
  const auto trackIt = m_tracks.constFind(track);
  if (trackIt == m_tracks.constEnd())
    return 0;
  const auto kindIt = m_kinds.constFind(kind);
  if (kindIt == m_kinds.constEnd())
    return 0;
  return pack(*trackIt, *kindIt);
}

void LaneKeys::clear() {
  m_tracks.clear();
  m_kinds.clear();
}

void SpanTable::setSpans(QVector<LaneSpan> spans) {
  m_spans = std::move(spans);
  const int count = m_spans.size();
  m_maxEndPrefix.resize(count);
  m_minStartSuffix.resize(count);

  qint64 runningMax = std::numeric_limits<qint64>::min();
  for (int i = 0; i < count; ++i) {
    runningMax = std::max(runningMax, m_spans.at(i).endMs);
    m_maxEndPrefix[i] = runningMax;
  }
  qint64 runningMin = kMaxInt64;
  for (int i = count - 1; i >= 0; --i) {
    runningMin = std::min(runningMin, m_spans.at(i).startMs);
    m_minStartSuffix[i] = runningMin;
  }

  m_totalEndMs = count > 0 ? runningMax : 0;
  m_lastScanned = 0;
  m_lastPasses = 0;
}

void SpanTable::clear() {
  m_spans.clear();
  m_maxEndPrefix.clear();
  m_minStartSuffix.clear();
  m_totalEndMs = 0;
  m_lastScanned = 0;
  m_lastPasses = 0;
  m_solveCalls = 0;
  m_solvePasses = 0;
}

int SpanTable::firstTouching(qint64 lowMs) const {
  const auto it = std::lower_bound(m_maxEndPrefix.cbegin(),
                                   m_maxEndPrefix.cend(), lowMs);
  return int(it - m_maxEndPrefix.cbegin());
}

int SpanTable::endTouching(qint64 highMs) const {
  const auto it = std::upper_bound(m_minStartSuffix.cbegin(),
                                   m_minStartSuffix.cend(), highMs);
  return int(it - m_minStartSuffix.cbegin());
}

// One rule, one implementation. `out` may be null, in which case this counts the
// rows a projection would produce without building it - which is what lets
// solve() probe a dozen thresholds without allocating or copying anything.
int SpanTable::projectRange(int from, int to, qint64 lowMs, qint64 highMs,
                            qint64 minSpanMs, int gapFactor,
                            QVector<Cluster> *out) const {
  if (out)
    out->clear();
  m_lastScanned = std::max(0, to - from);
  if (from >= to)
    return 0;

  const qint64 gapMs =
      minSpanMs > 0 ? saturatingMul(minSpanMs, std::max(0, gapFactor)) : 0;
  int rows = 0;
  int lastFirst = -1;
  int lastCount = 0;

  for (int i = from; i < to; ++i) {
    const LaneSpan &span = m_spans.at(i);
    // The same inclusive test the model has always applied. The prefix bounds
    // above only removed positions that could not pass it; this still has to
    // reject the ones inside the range that fall in a gap.
    if (span.endMs < lowMs || span.startMs > highMs)
      continue;

    // Wide enough to be seen, clicked and trimmed: it stays a row of its own,
    // whatever its neighbours do.
    if (minSpanMs > 0 && rows > 0 && span.lengthMs() < minSpanMs) {
      const int lastMember = lastFirst + lastCount - 1;
      const LaneSpan &lastSpan = m_spans.at(lastMember);
      const bool contiguous = lastMember + 1 == i;
      const bool sameLane = lastSpan.lane == span.lane;
      const bool lastNarrow = lastSpan.lengthMs() < minSpanMs;
      const bool adjacent = span.startMs - lastSpan.endMs <= gapMs;
      if (contiguous && sameLane && lastNarrow && adjacent) {
        ++lastCount;
        if (out)
          ++out->last().count;
        continue;
      }
    }

    lastFirst = i;
    lastCount = 1;
    ++rows;
    if (out)
      out->append(Cluster{i, 1});
  }
  return rows;
}

int SpanTable::project(qint64 lowMs, qint64 highMs, qint64 minSpanMs,
                       int gapFactor, QVector<Cluster> *out) const {
  m_lastPasses = 1;
  return projectRange(firstTouching(lowMs), endTouching(highMs), lowMs, highMs,
                      minSpanMs, gapFactor, out);
}

qint64 SpanTable::solve(qint64 lowMs, qint64 highMs, qint64 minSpanMs,
                        int gapFactor, int maxRows,
                        QVector<Cluster> *out) const {
  if (!out)
    return minSpanMs;

  ++m_solveCalls;
  const int from = firstTouching(lowMs);
  const int to = endTouching(highMs);
  int passes = 0;

  // The requested threshold, which is what the caller wants if it fits.
  qint64 tight = std::max<qint64>(0, minSpanMs);
  int rows = projectRange(from, to, lowMs, highMs, tight, gapFactor, nullptr);
  ++passes;

  if (maxRows <= 0 || rows <= maxRows) {
    projectRange(from, to, lowMs, highMs, tight, gapFactor, out);
    ++passes;
    m_lastPasses = passes;
    m_solvePasses += passes;
    return tight;
  }

  // Too many rows at the requested threshold. A threshold of zero means folding
  // was switched off - the backend sets the viewport once at startup without a
  // scale - so there is no value to grow from and one has to be assumed. A
  // thousand pixels of window width is wrong only in degree, and it is what the
  // model has always assumed in this case.
  qint64 wide = tight > 0 ? tight : std::max<qint64>(1, (highMs - lowMs) / 1000);
  if (wide <= tight)
    wide = tight + 1;

  bool fits = false;
  for (int guard = 0; guard < 32; ++guard) {
    rows = projectRange(from, to, lowMs, highMs, wide, gapFactor, nullptr);
    ++passes;
    if (rows <= maxRows) {
      fits = true;
      break;
    }
    if (wide >= kMaxThreshold)
      break;
    tight = wide;
    wide = std::min(kMaxThreshold, saturatingMul(wide, 4));
  }

  if (fits) {
    // Something between the last threshold known to overflow and the first one
    // known to fit is the tightest that still fits. Bisecting for it is worth
    // the passes: every halving of the threshold is clips drawn as themselves
    // instead of swallowed by a bar.
    while (wide - tight > 1 && passes < 24) {
      const qint64 mid = tight + (wide - tight) / 2;
      rows = projectRange(from, to, lowMs, highMs, mid, gapFactor, nullptr);
      ++passes;
      if (rows <= maxRows)
        wide = mid;
      else
        tight = mid;
    }
  }

  projectRange(from, to, lowMs, highMs, wide, gapFactor, out);
  ++passes;
  m_lastPasses = passes;
  m_solvePasses += passes;
  return wide;
}

QVariantMap SpanTable::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("spans")] = int(m_spans.size());
  stats[QStringLiteral("scanned")] = m_lastScanned;
  stats[QStringLiteral("passes")] = m_lastPasses;
  stats[QStringLiteral("solveCalls")] = m_solveCalls;
  stats[QStringLiteral("solvePasses")] = m_solvePasses;
  stats[QStringLiteral("totalEndMs")] = m_totalEndMs;
  return stats;
}

} // namespace cutpro::perf
