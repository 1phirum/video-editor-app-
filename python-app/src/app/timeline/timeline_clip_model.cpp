#include "app/timeline/timeline_clip_model.h"

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
// Separates a collapsed row's key from a clip index. Indices are small
// positives, so one high bit is enough to keep the two spaces apart.
constexpr qint64 kClusterKeyBit = qint64(1) << 62;
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
  for (int i = 0; i < m_allClips.size(); ++i) {
    const QVariantMap clip = m_allClips.at(i).toMap();
    const qint64 start = clip.value(QStringLiteral("startMs")).toLongLong();
    const qint64 duration =
        qMax<qint64>(1, clip.value(QStringLiteral("durationMs")).toLongLong());
    m_spans[i] = {start, start + duration,
                  clip.value(QStringLiteral("track")).toString(),
                  clip.value(QStringLiteral("kind")).toString()};
  }
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
  if (row.count <= 1)
    return row.firstIndex;
  return kClusterKeyBit | (qint64(row.firstIndex) << 21) |
         qint64(qMin(row.count, (1 << 21) - 1));
}

QVariant TimelineClipModel::payloadFor(const Row &row) const {
  if (row.count <= 1)
    return m_allClips.at(row.firstIndex);

  qint64 startMs = kMaxMs;
  qint64 endMs = 0;
  for (int i = row.firstIndex; i < row.firstIndex + row.count; ++i) {
    startMs = qMin(startMs, m_spans.at(i).startMs);
    endMs = qMax(endMs, m_spans.at(i).endMs);
  }
  const Span &first = m_spans.at(row.firstIndex);
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

QList<TimelineClipModel::Row>
TimelineClipModel::project(qint64 lowMs, qint64 highMs, qint64 minSpanMs) const {
  QList<Row> rows;
  rows.reserve(qMin<qsizetype>(m_spans.size(), qsizetype(kMaxRows) * 2) + 16);
  for (int i = 0; i < m_spans.size(); ++i) {
    const Span &span = m_spans.at(i);
    if (span.endMs < lowMs || span.startMs > highMs)
      continue;

    // Wide enough to be seen, clicked and trimmed: it stays a row of its own,
    // whatever its neighbours do.
    const bool narrow = minSpanMs > 0 && span.endMs - span.startMs < minSpanMs;
    if (narrow && !rows.isEmpty()) {
      Row &last = rows.last();
      const int lastMember = last.firstIndex + last.count - 1;
      const Span &lastSpan = m_spans.at(lastMember);
      const bool contiguous = lastMember + 1 == i;
      const bool sameLane =
          lastSpan.track == span.track && lastSpan.kind == span.kind;
      const bool lastNarrow = lastSpan.endMs - lastSpan.startMs < minSpanMs;
      const bool adjacent =
          span.startMs - lastSpan.endMs <= minSpanMs * kGapFactor;
      if (contiguous && sameLane && lastNarrow && adjacent) {
        ++last.count;
        continue;
      }
    }
    rows.append(Row{i, 1});
  }
  return rows;
}

void TimelineClipModel::rebuild(bool clipDataChanged) {
  // Saturating on both ends. clearViewport() parks the end at qint64's maximum,
  // and adding the margin to that overflowed - the comparison then rejected
  // every clip, so "no viewport" silently meant "no rows".
  const qint64 lowMs = m_viewStartMs - qMin(m_viewStartMs, kMarginMs);
  const qint64 highMs =
      m_viewEndMs > kMaxMs - kMarginMs ? kMaxMs : m_viewEndMs + kMarginMs;

  qint64 minSpanMs = 0;
  if (m_msPerPixel > 0.0)
    minSpanMs =
        qMax<qint64>(1, qint64(std::llround(m_msPerPixel * kMinRowPixels)));
  QList<Row> next = project(lowMs, highMs, minSpanMs);

  if (next.size() > kMaxRows && minSpanMs <= 0) {
    // The viewport was set without a scale - the backend does this once at
    // startup. Rather than hand the view an unbounded row count, assume the
    // window is about a thousand pixels wide, which is wrong only in degree.
    const qint64 windowMs =
        highMs == kMaxMs ? qMax<qint64>(1, m_spans.isEmpty() ? 1 : highMs - lowMs)
                         : qMax<qint64>(1, highMs - lowMs);
    minSpanMs = qMax<qint64>(1, windowMs / 1000);
    next = project(lowMs, highMs, minSpanMs);
  }
  // Widening rather than truncating: a cap that dropped rows would hide clips
  // the user placed, while a wider bar still accounts for every one of them.
  for (int guard = 0; next.size() > kMaxRows && minSpanMs > 0 && guard < 12;
       ++guard) {
    minSpanMs *= 3;
    next = project(lowMs, highMs, minSpanMs);
  }
  m_minSpanMs = minSpanMs;
  m_collapsedClips = 0;
  for (const Row &row : next)
    if (row.count > 1)
      m_collapsedClips += row.count;

  if (next != m_visibleRows) {
    applyVisible(next);
    return;
  }
  // Same rows as before. Scrolling inside the margin ends here, having told the
  // view nothing at all - that is the common case and the point of the margin.
  if (!clipDataChanged)
    return;
  // The projection is unchanged but the clips behind it may not be (one was
  // moved, trimmed or renamed). Refresh in place: every delegate stays alive,
  // where a reset would have rebuilt all of them.
  QVariantList refreshed;
  refreshed.reserve(next.size());
  for (const Row &row : next)
    refreshed.append(payloadFor(row));
  if (refreshed == m_visibleClips)
    return;
  m_visibleClips = std::move(refreshed);
  if (!m_visibleClips.isEmpty())
    emit dataChanged(index(0, 0), index(m_visibleClips.size() - 1, 0),
                     {ModelDataRole, Qt::DisplayRole});
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
}

QVariantMap TimelineClipModel::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("timelineClips")] = m_allClips.size();
  stats[QStringLiteral("timelineClipRows")] = m_visibleClips.size();
  stats[QStringLiteral("timelineClipsCollapsed")] = m_collapsedClips;
  stats[QStringLiteral("timelineClipMinSpanMs")] = m_minSpanMs;
  return stats;
}
