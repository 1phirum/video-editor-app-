#pragma once

// Neighbour and edge queries over the clip list, per lane, by binary search.
//
// This is for the questions a drag asks, and it exists because the current
// answers are linear scans inside QML bindings. Two of them:
//
//   TimelinePanel.qml, drawn twice per visible clip:
//       visible: root.dragTouchesClip(clipItem, "left")
//   and dragTouchesClip() reads Backend.clips and walks it. Reading that
//   property marshals every clip into a JS object, so at 19 831 cues one seam
//   test builds 19 831 objects, the binding runs twice per delegate, and there
//   are up to 420 delegates. That is the drag being slow, and no amount of
//   caching inside the binding fixes it, because the cost is the scan itself.
//
//   TimelineEditor::snapTime(requestedMs, const QVariantList &clips, ...) takes
//   the whole list too, and snappedTime() calls it on every pointer move.
//
// Both are "what is near this time on this lane", which is a binary search over
// sorted edges. The tables here are built once per clip-list change and answer
// in a handful of comparisons with no allocation and no marshalling:
//
//   touchesBefore / touchesAfter   the seam test, existence only, exits on the
//                                  first hit
//   occupied                       placement and collision, ignoring the clip
//                                  being moved
//   nearestEdge                    snapping, across every lane at once
//   overlapping                    ripple and range selection
//
// Positions are indices into the span vector handed to build(), so a hit turns
// back into a clip through LaneSpan::index without a second lookup. `exceptPos`
// is how a drag excludes itself: the clip being moved must not snap to, or
// collide with, where it used to be.
//
// Spans covering no time are dropped at build. They have no edges anyone can
// snap to and no interior anything can collide with, and keeping them would only
// lengthen every walk.
//
// Not thread safe, and not automatically invalidated - rebuild it when the clip
// list changes. Pair it with the buffer-address test SpanTable's callers use if
// you want that to be automatic.

#include <QHash>
#include <QVector>
#include <QtGlobal>

#include "app/performance/span_table.h"

namespace cutpro::perf {

class LaneNeighbors {
public:
  void build(const QVector<LaneSpan> &spans);
  void clear();

  bool isEmpty() const { return m_spans.isEmpty(); }
  // Spans given to build(), including the ones dropped for covering no time.
  int size() const { return int(m_spans.size()); }
  int laneCount() const { return int(m_laneData.size()); }
  int edgeCount() const { return int(m_edges.size()); }
  const LaneSpan &at(int position) const { return m_spans.at(position); }
  // Every distinct boundary in the whole list, ascending and deduplicated.
  const QVector<qint64> &edges() const { return m_edges; }

  // Nearest span on `lane` ending at or before timeMs, and starting at or after
  // timeMs. -1 when there is none.
  int lastEndingAtOrBefore(quint32 lane, qint64 timeMs,
                           int exceptPos = -1) const;
  int firstStartingAtOrAfter(quint32 lane, qint64 timeMs,
                             int exceptPos = -1) const;

  // Does anything on `lane` end within toleranceMs of startMs? This is the left
  // seam a dragged clip draws when it lands against a neighbour. Existence only,
  // so it stops at the first hit however dense the lane is.
  bool touchesBefore(quint32 lane, qint64 startMs, qint64 toleranceMs,
                     int exceptPos = -1) const;
  // The right seam: anything on `lane` starting within toleranceMs of endMs.
  bool touchesAfter(quint32 lane, qint64 endMs, qint64 toleranceMs,
                    int exceptPos = -1) const;

  // Would a clip occupying [startMs, endMs) overlap anything already on `lane`?
  // Half-open, so two clips meeting exactly at a boundary do not count as
  // overlapping - butt-joined clips are the normal case on a subtitle track.
  bool occupied(quint32 lane, qint64 startMs, qint64 endMs,
                int exceptPos = -1) const;

  // Positions on `lane` overlapping [fromMs, toMs), ascending by start. Returns
  // the count; `out` is cleared first and reused.
  int overlapping(quint32 lane, qint64 fromMs, qint64 toMs,
                  QVector<int> *out) const;

  // Nearest boundary to timeMs anywhere in the list, within windowMs, or
  // `fallback`. Snapping crosses tracks on purpose: a caption should be able to
  // land on a cut in the video below it.
  qint64 nearestEdge(qint64 timeMs, qint64 windowMs, qint64 fallback = -1) const;
  // The same, restricted to one lane.
  qint64 nearestEdgeOn(quint32 lane, qint64 timeMs, qint64 windowMs,
                       qint64 fallback = -1) const;

private:
  struct Lane {
    // Positions, ascending by startMs and by endMs respectively.
    QVector<int> byStart;
    QVector<int> byEnd;
    // The keys those two orders were sorted on, stored separately so a search
    // reads one contiguous array of integers instead of chasing a position into
    // the span vector for every comparison.
    QVector<qint64> startKeys;
    QVector<qint64> endKeys;
    // maxEndPrefix[k] = largest endMs over byStart[0..k]. Stops the backwards
    // walk in occupied() the moment nothing earlier can still reach.
    QVector<qint64> maxEndPrefix;
  };

  const Lane *laneFor(quint32 lane) const;

  QVector<LaneSpan> m_spans;
  QVector<Lane> m_laneData;
  QHash<quint32, int> m_laneIndex;
  QVector<qint64> m_edges;
};

} // namespace cutpro::perf
