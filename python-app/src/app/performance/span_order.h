#pragma once

// The order SpanTable's collapse rule needs, and why a list that is not in it
// costs the timeline everything the collapse was written to save.
//
// SpanTable::projectRange folds a narrow span into the row before it only when
// the two are *adjacent in the array it was given*, on the same lane, and close
// in time:
//
//     const bool contiguous = lastMember + 1 == i;
//     const bool sameLane = lastSpan.lane == span.lane;
//     const bool adjacent = span.startMs - lastSpan.endMs <= gapMs;
//
// TimelineClipModel hands it the spans in clip-list order, and for anything
// built by an editing session that is close enough to (lane, time) order for the
// rule to fire. A generated voice track is not. Cues come back from the
// still-running worker in completion order - identical text is synthesized once,
// so cue 19000 finishes beside cue 40 - and each one is placed on the lowest
// free lane, so consecutive entries are neither on one lane nor next to each
// other in time. Every one of the three tests above fails, and the consequences
// compound:
//
//   * nothing folds, so a window over 2581 voices projects 2581 rows of the
//     heaviest delegate in the app instead of a few hundred bars;
//   * solve() cannot reach a fitting row count by widening its threshold - the
//     rule it is widening never applies - so it quadruples 32 times and bisects,
//     each pass a full scan, and still returns the row count it started with;
//   * the prefix bounds in the table prune nothing on an unordered list, so each
//     of those passes visits every span rather than the window's few hundred.
//
// That is one projection. It runs again on every viewport change - a scroll or
// zoom delivers one per frame - and again on every clip list publish, which
// during a live import is several times a second.
//
// Sorting the spans by (lane, startMs) restores all three tests at once: a
// lane's spans become one contiguous, time-ordered run, exactly the shape the
// folding rule reads, so the projection collapses and solve() settles in two or
// three passes over a pruned range. Position in the sorted table is not clip
// order any more, which is why LaneSpan carries `index`.
//
// Ordering by lane first and time second, rather than the other way round: the
// contiguity test is per lane, and a purely chronological order interleaves the
// tracks - with voices on A1..A3 the same-lane neighbour is three entries away
// and folding fails again for a different reason.

#include "app/performance/span_table.h"

#include <QVector>

namespace cutpro::perf {

// (lane, startMs, index) of `a` before `b`. `index` only breaks ties, so the
// order is total and two projections of the same clips agree on which span is a
// row's first - which is what lets a row keep its delegate across a rebuild.
bool spanOrderBefore(const LaneSpan &a, const LaneSpan &b);

// True when `spans` is already in that order. One linear pass, so a list that
// was built in it - a caption track imported in time order, a project loaded
// from disk - pays a comparison per span and no sort at all.
bool spansOrderedByLane(const QVector<LaneSpan> &spans);

// Puts `spans` in that order, returning true when it had to move something.
// The caller can report that: a sequence whose spans needed reordering is one
// whose clip list is not in timeline order, which is worth knowing when reading
// the table's own "scanned N of M" statistics.
bool orderSpansByLane(QVector<LaneSpan> *spans);

} // namespace cutpro::perf
