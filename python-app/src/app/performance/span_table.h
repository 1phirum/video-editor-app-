#pragma once

// Windowed span projection: deciding which of N clips deserve a row in the view,
// without looking at the ones that cannot possibly be on screen.
//
// TimelineClipModel already does the hard part - it projects only the visible
// window and collapses runs of unreadably narrow clips, which is what keeps the
// delegate count bounded at 420 instead of 19 831. What it still does is find
// that window by walking the whole list:
//
//     for (int i = 0; i < m_spans.size(); ++i)
//         if (span.endMs < lowMs || span.startMs > highMs) continue;
//
// and rebuild() runs that walk up to fourteen times per call - once, once more
// when the viewport arrived without a scale, and up to twelve more while it
// widens the collapse threshold looking for a row count that fits. setViewport()
// is called on every content-x change, so a scroll costs about 14 x 19 831 span
// visits per event, and a scroll delivers an event per frame. That is a quarter
// of a million comparisons per frame to draw at most 420 rows.
//
// This table answers the same question over the same rule and returns the same
// rows - it only stops looking sooner. Two prefix arrays do it:
//
//   maxEndPrefix[i]   largest endMs over spans[0..i]
//   minStartSuffix[i] smallest startMs over spans[i..n-1]
//
// Both are monotone, so a binary search finds the first position that could
// reach lowMs and the first position past which nothing can still start before
// highMs. Everything outside that range fails the window test the old loop was
// applying by hand, so dropping it cannot change the answer. For a transcript
// track - cues in time order - the range is the window and nothing else, so the
// scan goes from 19 831 to a few hundred. For a list in no particular order the
// bounds simply do not prune, and the cost is what it is today.
//
// Not thread safe: the statistics counters and solve()'s scratch buffer are
// mutable. It is meant to live beside a model on the GUI thread.

#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>

namespace cutpro::perf {

// A (track, kind) pair as one integer, because the clustering rule compares that
// pair for every span it visits and QString::operator== on two short strings is
// a length check, a branch and a memcmp where this is a 32-bit compare. Two
// small tables intern the halves separately - a sequence has a handful of tracks
// and four kinds, so both stay tiny and the pair never needs concatenating.
class LaneKeys {
public:
  // Interns, assigning a new id when unseen. Ids start at 1 so 0 can mean "no
  // lane" for callers that need a null.
  quint32 keyFor(const QString &track, const QString &kind);
  // Does not intern; returns 0 when either half is unknown, which is the useful
  // answer for a query about a lane no clip is on.
  quint32 lookup(const QString &track, const QString &kind) const;
  void clear();
  int trackCount() const { return m_tracks.size(); }
  int kindCount() const { return m_kinds.size(); }

private:
  static quint32 pack(quint16 track, quint16 kind) {
    return (quint32(track) << 16) | quint32(kind);
  }
  QHash<QString, quint16> m_tracks;
  QHash<QString, quint16> m_kinds;
};

// A half-open span [startMs, endMs) on one lane, plus where it came from.
struct LaneSpan {
  qint64 startMs = 0;
  qint64 endMs = 0;
  // Interned (track, kind). Only ever compared, never decoded.
  quint32 lane = 0;
  // Position in the caller's own list, so a projected row can be turned back
  // into a clip without a second lookup.
  int index = -1;

  qint64 lengthMs() const { return endMs - startMs; }
};

class SpanTable {
public:
  // One projected row: a single span when count == 1, otherwise a run of spans
  // drawn as one bar. `first` is a position in this table, which is the caller's
  // list order only if the caller handed the spans over unfiltered - read
  // at(first).index for the real thing.
  struct Cluster {
    int first = 0;
    int count = 1;

    bool isCluster() const { return count > 1; }
    friend bool operator==(const Cluster &a, const Cluster &b) {
      return a.first == b.first && a.count == b.count;
    }
    friend bool operator!=(const Cluster &a, const Cluster &b) {
      return !(a == b);
    }
  };

  // Keeps the order it is given. Clustering means "adjacent in this array", so
  // if the caller filtered on the way in, adjacency is adjacency after the
  // filter - which is what a per-track table wants.
  void setSpans(QVector<LaneSpan> spans);
  void clear();

  bool isEmpty() const { return m_spans.isEmpty(); }
  int size() const { return m_spans.size(); }
  const QVector<LaneSpan> &spans() const { return m_spans; }
  const LaneSpan &at(int position) const { return m_spans.at(position); }
  // Largest endMs over every span, which for a clip list is the sequence length.
  qint64 totalEndMs() const { return m_totalEndMs; }

  // First position that could still end at or after lowMs. Everything below it
  // ended earlier and can never be in a window starting there.
  int firstTouching(qint64 lowMs) const;
  // One past the last position that could still start at or before highMs.
  int endTouching(qint64 highMs) const;

  // Rows for [lowMs, highMs], inclusive on both ends to match the test the model
  // has always applied. A span shorter than minSpanMs is a candidate for being
  // folded into the row before it, but only when the two are adjacent in this
  // array, on the same lane, the previous one is also narrow, and the gap
  // between them is at most minSpanMs * gapFactor. minSpanMs <= 0 disables
  // folding entirely and gives one row per span. Returns the row count and
  // fills `out` - which is cleared first and reused, so a caller that keeps one
  // vector around allocates once and never again.
  int project(qint64 lowMs, qint64 highMs, qint64 minSpanMs, int gapFactor,
              QVector<Cluster> *out) const;

  // The smallest minSpanMs at or above the one given whose projection fits in
  // maxRows, with `out` left holding that projection. Widening rather than
  // truncating, for the reason the model already documents: a cap that dropped
  // rows would hide clips the user placed, while a wider bar still accounts for
  // every one of them.
  //
  // Grows by quadrupling until something fits, then bisects for the smallest
  // value that still fits - about a dozen windowed passes worst case instead of
  // twelve whole-list ones, and it lands on a threshold two to four times
  // tighter than repeated tripling does, which is two to four times more clips
  // drawn as themselves rather than as part of a bar.
  //
  // Row count is assumed to fall as minSpanMs rises. It does, for any lane
  // layout this app produces; if it ever did not, the bisection would return a
  // fitting projection it had already seen rather than a wrong one.
  qint64 solve(qint64 lowMs, qint64 highMs, qint64 minSpanMs, int gapFactor,
               int maxRows, QVector<Cluster> *out) const;

  // Spans actually visited by the last project() or solve(), and how many passes
  // it took. "scanned 380 of 19831 in 4 passes" is the one line that says this
  // class is doing its job; a scanned count near size() means the list is not in
  // time order and the prefix bounds have nothing to prune.
  int lastScanned() const { return m_lastScanned; }
  int lastPasses() const { return m_lastPasses; }
  QVariantMap statistics() const;

private:
  // The single implementation of the folding rule. `out` may be null, in which
  // case the rows are counted and not built - that is what lets solve() try a
  // dozen thresholds without allocating.
  int projectRange(int from, int to, qint64 lowMs, qint64 highMs,
                   qint64 minSpanMs, int gapFactor,
                   QVector<Cluster> *out) const;

  QVector<LaneSpan> m_spans;
  // maxEndPrefix[i] = largest endMs over m_spans[0..i]. Non-decreasing.
  QVector<qint64> m_maxEndPrefix;
  // minStartSuffix[i] = smallest startMs over m_spans[i..n-1]. Non-decreasing,
  // because dropping the front of a range can only raise its minimum.
  QVector<qint64> m_minStartSuffix;
  qint64 m_totalEndMs = 0;
  mutable int m_lastScanned = 0;
  mutable int m_lastPasses = 0;
  mutable qint64 m_solveCalls = 0;
  mutable qint64 m_solvePasses = 0;
};

} // namespace cutpro::perf
