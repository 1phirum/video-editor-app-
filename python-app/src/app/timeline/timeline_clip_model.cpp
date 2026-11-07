#include "app/timeline/timeline_clip_model.h"

#include "app/performance/span_order.h"
#include "app/preview/gui_thread_watchdog.h"

#include <QHash>
#include <QSet>
#include <QtGlobal>
#include <cmath>
#include <limits>
#include <utility>

namespace {
// Keep a small time margin so scrolling does not constantly destroy and
// recreate delegates at the viewport edge.
constexpr qint64 kMarginMs = 30000;
constexpr qint64 kMaxMs = std::numeric_limits<qint64>::max();
// How far apart two narrow clips may sit and still be drawn as one bar. Any
// larger and the bar would cover silence that has no clips in it at all.
constexpr int kGapFactor = 1;
} // namespace

TimelineClipModel::TimelineClipModel(QObject *parent)
    : QAbstractListModel(parent) {}

int TimelineClipModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_visibleClips.size();
}

QVariant TimelineClipModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleClips.size())
    return {};
  if (role == ModelDataRole || role == Qt::DisplayRole)
    return m_visibleClips.at(index.row());
  return {};
}

QHash<int, QByteArray> TimelineClipModel::roleNames() const {
  return {{ModelDataRole, QByteArrayLiteral("modelData")}};
}

void TimelineClipModel::setClips(const QVariantList &clips) {
  CUTPRO_GUI_SCOPE("TimelineClipModel::setClips");
  m_allClips = clips;
  m_spans.resize(m_allClips.size());
  QVector<cutpro::perf::LaneSpan> lanes(m_allClips.size());
  m_lanes.clear();
  for (int i = 0; i < m_allClips.size(); ++i) {
    const QVariantMap clip = m_allClips.at(i).toMap();
    const qint64 start = clip.value(QStringLiteral("startMs")).toLongLong();
    const qint64 duration =
        qMax<qint64>(1, clip.value(QStringLiteral("durationMs")).toLongLong());
    const QString track = clip.value(QStringLiteral("track")).toString();
    const QString kind = clip.value(QStringLiteral("kind")).toString();
    m_spans[i] = {start, start + duration, track, kind};
    lanes[i] = {start, start + duration, m_lanes.keyFor(track, kind), i};
  }
  // Into the order the collapse rule reads, before the table sees them. Rows
  // point back at m_spans through LaneSpan::index, so nothing downstream needs
  // table position to still be clip position - and everything downstream needs
  // the collapse to work. See span_order.h for what an unsorted list costs.
  m_spansReordered = cutpro::perf::orderSpansByLane(&lanes);
  m_table.setSpans(std::move(lanes));
  rebuild(true);
}

void TimelineClipModel::refreshPayload(const QVariantList &clips) {
  CUTPRO_GUI_SCOPE("TimelineClipModel::refreshPayload");
  m_allClips = clips;
  // No span rebuild or re-sort: a payload-only edit changes a clip's inner
  // fields, not its start, duration, track or kind, so m_spans and the span
  // table still describe this list. rebuild(true) re-projects the window and
  // refreshes the visible rows' payloads in place, bounded by the viewport.
  rebuild(true);
}

void TimelineClipModel::setViewport(qint64 startMs, qint64 endMs,
                                    double msPerPixel) {
  startMs = qMax<qint64>(0, startMs);
  endMs = qMax(startMs + 1, endMs);
  const double scale = msPerPixel > 0.0 ? msPerPixel : 0.0;
  if (m_viewStartMs == startMs && m_viewEndMs == endMs &&
      qFuzzyCompare(m_msPerPixel + 1.0, scale + 1.0))
    return;
  m_viewStartMs = startMs;
  m_viewEndMs = endMs;
  m_msPerPixel = scale;
  rebuild(false);
}

void TimelineClipModel::clearViewport() {
  m_viewStartMs = 0;
  m_viewEndMs = kMaxMs;
  rebuild(false);
}

qint64 TimelineClipModel::keyFor(const Row &row) {
  return row.clipIndex;
}

QVariant TimelineClipModel::payloadFor(const Row &row) const {
  if (row.count <= 1)
    return m_allClips.at(row.clipIndex);

  // Through the table, not through m_spans: the run is contiguous in table
  // order, and table order is (lane, startMs), so its members are not
  // consecutive clips and cannot be walked as if they were.
  qint64 startMs = kMaxMs;
  qint64 endMs = 0;
  const int last = qMin(row.position + row.count, m_table.size());
  for (int i = row.position; i < last; ++i) {
    const cutpro::perf::LaneSpan &span = m_table.at(i);
    startMs = qMin(startMs, span.startMs);
    endMs = qMax(endMs, span.endMs);
  }
  if (startMs > endMs)
    startMs = endMs;
  const Span &first = m_spans.at(row.clipIndex);
  const qint64 spanMs = qMax<qint64>(1, endMs - startMs);

  QVariantMap cluster;
  // Namespaced so it can never match a real clip id. The delegate looks
  // selection, drag state and trim state up by id, and a bar standing for
  // hundreds of clips is none of those things.
  cluster[QStringLiteral("id")] =
      QStringLiteral("cluster:%1:%2").arg(first.track).arg(startMs);
  cluster[QStringLiteral("kind")] = first.kind;
  cluster[QStringLiteral("track")] = first.track;
  cluster[QStringLiteral("startMs")] = startMs;
  cluster[QStringLiteral("durationMs")] = spanMs;
  cluster[QStringLiteral("sourceInMs")] = qint64(0);
  cluster[QStringLiteral("sourceDurationMs")] = spanMs;
  // No media: a bar has no one source to draw a filmstrip or a waveform from,
  // and leaving these empty is what keeps it from asking for either.
  cluster[QStringLiteral("mediaId")] = QString();
  cluster[QStringLiteral("isCluster")] = true;
  cluster[QStringLiteral("clipCount")] = row.count;
  const QString label = QStringLiteral("%1 clips").arg(row.count);
  cluster[QStringLiteral("name")] = label;
  cluster[QStringLiteral("label")] = label;
  cluster[QStringLiteral("text")] = label;
  return cluster;
}

qint64 TimelineClipModel::project(qint64 lowMs, qint64 highMs, qint64 minSpanMs,
                                  QList<Row> *rows) const {
  // solve() does three things this function used to do by hand, and does all of
  // them over the windowed range instead of the whole list: the projection at the
  // requested threshold, the assumed threshold when the viewport arrived without
  // a scale, and the widening search for a row count that fits under kMaxRows.
  const qint64 settled = m_table.solve(lowMs, highMs, minSpanMs, kGapFactor,
                                       kMaxRows, &m_clusters);
  rows->clear();
  rows->reserve(m_clusters.size());
  for (const cutpro::perf::SpanTable::Cluster &cluster : m_clusters)
    rows->append(
        Row{cluster.first, m_table.at(cluster.first).index, cluster.count});
  return settled;
}

void TimelineClipModel::rebuild(bool clipDataChanged) {
  // Saturating on both ends. clearViewport() parks the end at qint64's maximum,
  // and adding the margin to that overflowed - the comparison then rejected
  // every clip, so "no viewport" silently meant "no rows".
  const qint64 lowMs = m_viewStartMs - qMin(m_viewStartMs, kMarginMs);
  qint64 highMs =
      m_viewEndMs > kMaxMs - kMarginMs ? kMaxMs : m_viewEndMs + kMarginMs;
  // Pull an unbounded end back to the last clip. clearViewport() and the
  // backend's one startup call both park it at qint64's maximum, and the
  // collapse search sizes its first guess at the threshold from the window
  // width - so an unbounded window asked it to start from a threshold of a
  // hundred thousand years, which folds the whole sequence into one bar per lane
  // and then takes twenty passes to work back down to something readable.
  if (highMs == kMaxMs)
    highMs = qMax(lowMs + 1, m_table.totalEndMs());

  qint64 minSpanMs = 0;
  if (m_msPerPixel > 0.0)
    minSpanMs =
        qMax<qint64>(1, qint64(std::llround(m_msPerPixel * kMinRowPixels)));

  QList<Row> next;
  m_minSpanMs = project(lowMs, highMs, minSpanMs, &next);
  m_collapsedClips = 0;
  for (const Row &row : next)
    if (row.count > 1)
      m_collapsedClips += row.count;

  if (next != m_visibleRows || clipDataChanged) {
    // One path for both cases. applyVisible() diffs row by row: rows that left
    // are removed, rows that arrived are inserted, and a row present in both
    // keeps its delegate and is refreshed only if its payload actually differs.
    // The bulk "same rows, refresh everything" branch that used to live here
    // emitted one dataChanged across all 420 rows whenever any clip changed,
    // which during a live import is several times a second.
    applyVisible(next);
    return;
  }
  // Same rows and nothing behind them moved. Scrolling inside the margin ends
  // here, having told the view nothing at all - that is the common case and the
  // point of the margin.
}

void TimelineClipModel::applyVisible(const QList<Row> &next) {
  QSet<qint64> keep;
  keep.reserve(next.size());
  for (const Row &row : next)
    keep.insert(keyFor(row));

  // Rows that left, in contiguous runs and taken from the back, so the row
  // numbers still ahead of each run are the ones the view currently knows.
  for (int row = m_visibleRows.size() - 1; row >= 0;) {
    if (keep.contains(keyFor(m_visibleRows.at(row)))) {
      --row;
      continue;
    }
    int first = row;
    while (first > 0 && !keep.contains(keyFor(m_visibleRows.at(first - 1))))
      --first;
    beginRemoveRows({}, first, row);
    m_visibleRows.remove(first, row - first + 1);
    m_visibleClips.remove(first, row - first + 1);
    endRemoveRows();
    row = first - 1;
  }

  // What is left is a subsequence of `next`, so one forward walk places each run
  // of arrivals at the row it belongs on. Rows present in both lists are never
  // touched structurally - their delegates survive the scroll.
  int row = 0;
  for (int pos = 0; pos < next.size();) {
    if (row < m_visibleRows.size() && m_visibleRows.at(row) == next.at(pos)) {
      // The same row, but not necessarily the same span table position or the
      // same member count - both change as clips are added under it. Take the
      // new one: geometry is read through it, and a stale position would build
      // the next payload out of the wrong members.
      m_visibleRows[row] = next.at(pos);
      const QVariant payload = payloadFor(next.at(pos));
      if (m_visibleClips.at(row) != payload) {
        m_visibleClips[row] = payload;
        emit dataChanged(index(row, 0), index(row, 0),
                         {ModelDataRole, Qt::DisplayRole});
      }
      ++row;
      ++pos;
      continue;
    }
    int count = 0;
    while (pos + count < next.size() &&
           (row >= m_visibleRows.size() ||
            m_visibleRows.at(row) != next.at(pos + count)))
      ++count;
    beginInsertRows({}, row, row + count - 1);
    for (int k = 0; k < count; ++k) {
      m_visibleRows.insert(row + k, next.at(pos + k));
      m_visibleClips.insert(row + k, payloadFor(next.at(pos + k)));
    }
    endInsertRows();
    row += count;
    pos += count;
  }

  // Anything the walk did not reach. Survivors are a subsequence of `next` in
  // every projection this model produces, and then this removes nothing - but
  // the ordering of two rows on different lanes follows the order their tracks
  // first appear in the clip list, and an edit that empties a track can change
  // that. Taking the tail leaves the row list exactly `next` either way, rather
  // than trusting an assumption about lane order to hold forever.
  if (row < m_visibleRows.size()) {
    beginRemoveRows({}, row, m_visibleRows.size() - 1);
    m_visibleRows.remove(row, m_visibleRows.size() - row);
    m_visibleClips.remove(row, m_visibleClips.size() - row);
    endRemoveRows();
  }
}

QVariantMap TimelineClipModel::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("timelineClips")] = m_allClips.size();
  stats[QStringLiteral("timelineClipRows")] = m_visibleClips.size();
  stats[QStringLiteral("timelineClipsCollapsed")] = m_collapsedClips;
  stats[QStringLiteral("timelineClipMinSpanMs")] = m_minSpanMs;
  // "scanned 380 of 19831 in 6 passes" is the line that says the windowed search
  // is pruning. A scanned count near the span count means the clip list is not in
  // time order, so the prefix bounds have nothing to cut.
  stats[QStringLiteral("timelineClipsScanned")] = m_table.lastScanned();
  stats[QStringLiteral("timelineClipPasses")] = m_table.lastPasses();
  // True means the clip list was not in (lane, time) order and had to be sorted
  // for the collapse to see runs at all - normal after a generated voice track,
  // whose cues arrive in the order the worker finishes them.
  stats[QStringLiteral("timelineClipsReordered")] = m_spansReordered;
  return stats;
}
