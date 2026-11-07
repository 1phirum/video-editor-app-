#include "app/performance/span_order.h"

#include <algorithm>

namespace cutpro::perf {

bool spanOrderBefore(const LaneSpan &a, const LaneSpan &b) {
  if (a.lane != b.lane)
    return a.lane < b.lane;
  if (a.startMs != b.startMs)
    return a.startMs < b.startMs;
  return a.index < b.index;
}

bool spansOrderedByLane(const QVector<LaneSpan> &spans) {
  for (int i = 1; i < spans.size(); ++i)
    if (spanOrderBefore(spans.at(i), spans.at(i - 1)))
      return false;
  return true;
}

bool orderSpansByLane(QVector<LaneSpan> *spans) {
  if (!spans || spansOrderedByLane(*spans))
    return false;
  // std::sort, not stable_sort: the comparison is a total order already - index
  // is its last term - so there are no equal elements whose order could be worth
  // preserving, and this way the sort needs no scratch buffer.
  std::sort(spans->begin(), spans->end(), spanOrderBefore);
  return true;
}

} // namespace cutpro::perf
