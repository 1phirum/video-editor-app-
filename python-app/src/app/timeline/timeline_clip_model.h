#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <limits>

#include "app/performance/span_table.h"
#include "core/module_api.h"

// Viewport projection of the project's clip list. The backend keeps the full
// list for editing/serialization, while QML receives only rows near the
// visible timeline window.
//
// Row changes are reported as inserts, removes and dataChanged rather than as a
// model reset. That distinction is the difference between a responsive timeline
// and a frozen one: a reset makes the view destroy and recreate every delegate,
// and the timeline delegate is a 500-line item tree. Scrolling calls setViewport
// on every content-x change, so a resetting model tore down and rebuilt the
// whole visible set several times a second - which is exactly the bulk
// deferred-delete teardown the GUI watchdog caught the main thread inside of.
class CUTPRO_TIMELINE_API TimelineClipModel final : public QAbstractListModel {
  Q_OBJECT

public:
  enum Role { ModelDataRole = Qt::UserRole + 1 };
  explicit TimelineClipModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setClips(const QVariantList &clips);
  // Payload-only update: the clips changed value but not structure (none moved,
  // resized, changed track or kind, was added or removed). Skips the span
  // rebuild and re-sort that setClips() does - the span table still describes
  // this list - and just re-projects the window, refreshing the visible rows'
  // payloads in place (dataChanged), which is bounded by the viewport.
  void refreshPayload(const QVariantList &clips);
  // msPerPixel is how much time one timeline pixel covers. Zero keeps the old
  // one-row-per-clip projection; a real value lets the model collapse runs of
  // clips too narrow to be seen individually, which is the difference between
  // 2581 delegates and a bounded number of them. An imported subtitle track at
  // Fit zoom is the case: every clip is a fraction of a pixel wide, and the view
  // instantiated a full clip delegate - MouseAreas, filmstrip, waveform - for
  // each one inside a single endInsertRows(), which the GUI watchdog caught as a
  // 2033 ms stall in QQuickRepeater::modelUpdated.
  Q_INVOKABLE void setViewport(qint64 startMs, qint64 endMs,
                               double msPerPixel = 0.0);
  Q_INVOKABLE void clearViewport();

  // Rows the projection actually produced, and how many clips they stand for.
  // Read by the diagnostics overlay: "2581 clips in 180 rows" is the one number
  // that says the collapse is working.
  Q_INVOKABLE QVariantMap statistics() const;

  // A clip narrower than this many pixels cannot be read, clicked or trimmed, so
  // it is a candidate for being drawn as part of a bar instead of on its own.
  static constexpr double kMinRowPixels = 7.0;
  // Ceiling on the projection. Reached only by absurd density; the collapse
  // widens its own threshold until the row count fits under this.
  static constexpr int kMaxRows = 420;

private:
  // `clipDataChanged` says the underlying clips may differ even where the set of
  // visible rows does not, so an unchanged projection still has to be refreshed
  // in place instead of being skipped.
  void rebuild(bool clipDataChanged);
  // One projected row. `count == 1` is a real clip and carries the clip's own
  // map; a larger count is a collapsed run drawn as a single bar.
  struct Row {
    // Where the run starts in the span table. The table is in (lane, startMs)
    // order, so this renumbers whenever a clip is added - it is read for the
    // row's geometry and never for its identity.
    int position = 0;
    // The run's first clip in m_allClips order, which is what identifies the
    // row. Appending a clip renumbers table positions but not clips, so a bar
    // that gained a member stays the same row and only its payload changes.
    // Neither `position` nor `count` may take part in that: a bar grows by one
    // member several times a second during a live import, and a row whose
    // identity changed is a delegate destroyed and built again.
    int clipIndex = 0;
    int count = 1;
    friend bool operator==(const Row &a, const Row &b) {
      return a.clipIndex == b.clipIndex;
    }
    friend bool operator!=(const Row &a, const Row &b) { return !(a == b); }
  };
  // Rows for the window, at the tightest collapse threshold at or above
  // `minSpanMs` whose result fits under kMaxRows. The search and the window
  // filter both live in SpanTable: it binary-searches the two prefix arrays for
  // the range that can touch the window and probes thresholds over that range
  // only, where this class used to walk all m_spans up to fourteen times per
  // call. Returns the threshold it settled on.
  qint64 project(qint64 lowMs, qint64 highMs, qint64 minSpanMs,
                 QList<Row> *rows) const;
  // Stable identity for a row, so the diff below can tell "the same row moved"
  // from "a different row arrived": the index of the row's first clip, whether
  // it stands for that clip alone or for a run beginning with it. A bar that
  // grew, shrank, or became its own clip again is the same row with a different
  // payload, which costs a dataChanged instead of a delegate.
  static qint64 keyFor(const Row &row);
  QVariant payloadFor(const Row &row) const;
  // Moves the projection to `next` using row-level signals only.
  void applyVisible(const QList<Row> &next);

  QVariantList m_allClips;
  QVariantList m_visibleClips;
  // startMs/endMs of every m_allClips entry, in the same order. Scrolling asks
  // "which clips touch this window" on every content-x change, and doing that
  // through QVariantMap lookups meant ~25,000 string-keyed hash lookups per
  // scroll event on a transcript-length track. These are read instead.
  struct Span {
    qint64 startMs = 0;
    qint64 endMs = 0;
    // Kept alongside the times because a run may only be collapsed while it
    // stays on one track and one clip kind - a subtitle bar must not swallow the
    // audio clip beside it.
    QString track;
    QString kind;
  };
  QList<Span> m_spans;
  // The same spans in the form the windowed search reads: times, an interned
  // (track, kind) lane, and the index back into m_spans. Held separately because
  // payloadFor() still needs the track and kind as strings to build a cluster's
  // map, and interning them is the point of the lane id.
  //
  // Sorted into (lane, startMs) order on the way in - see span_order.h. The
  // table's collapse rule folds only spans that are neighbours in the array it
  // was given, so clip-list order costs the collapse entirely for anything
  // placed out of time order across several tracks, which is what a generated
  // voice track is.
  cutpro::perf::LaneKeys m_lanes;
  cutpro::perf::SpanTable m_table;
  // Reused across calls so a scroll event does not allocate: SpanTable::solve
  // clears and refills it.
  mutable QVector<cutpro::perf::SpanTable::Cluster> m_clusters;
  // The row each visible row came from, in order. Kept so a new projection can
  // be diffed against the current one.
  QList<Row> m_visibleRows;
  qint64 m_viewStartMs = 0;
  qint64 m_viewEndMs = std::numeric_limits<qint64>::max();
  double m_msPerPixel = 0.0;
  // Last projection's shape, for statistics() only.
  int m_collapsedClips = 0;
  qint64 m_minSpanMs = 0;
  // Whether the last setClips() had to sort the spans. A sequence that always
  // needs it is one whose clip list is not in timeline order, which is the
  // context for the table's own "scanned N of M".
  bool m_spansReordered = false;
};
