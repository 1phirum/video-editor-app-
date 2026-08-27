#include "app/timeline/timeline_clip_model.h"

#include <QHash>
#include <QSet>
#include <QtGlobal>
#include <limits>
#include <utility>

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
  m_allClips = clips;
  m_spans.resize(m_allClips.size());
  for (int i = 0; i < m_allClips.size(); ++i) {
    const QVariantMap clip = m_allClips.at(i).toMap();
    const qint64 start = clip.value(QStringLiteral("startMs")).toLongLong();
    const qint64 duration =
        qMax<qint64>(1, clip.value(QStringLiteral("durationMs")).toLongLong());
    m_spans[i] = {start, start + duration};
  }
  rebuild(true);
}

void TimelineClipModel::setViewport(qint64 startMs, qint64 endMs) {
  startMs = qMax<qint64>(0, startMs);
  endMs = qMax(startMs + 1, endMs);
  if (m_viewStartMs == startMs && m_viewEndMs == endMs)
    return;
  m_viewStartMs = startMs;
  m_viewEndMs = endMs;
  rebuild(false);
}

void TimelineClipModel::clearViewport() {
  m_viewStartMs = 0;
  m_viewEndMs = std::numeric_limits<qint64>::max();
  rebuild(false);
}

void TimelineClipModel::rebuild(bool clipDataChanged) {
  // Keep a small time margin so scrolling does not constantly destroy and
  // recreate delegates at the viewport edge.
  constexpr qint64 kMarginMs = 30000;
  constexpr qint64 kMaxMs = std::numeric_limits<qint64>::max();
  // Saturating on both ends. clearViewport() parks the end at qint64's maximum,
  // and adding the margin to that overflowed - the comparison then rejected
  // every clip, so "no viewport" silently meant "no rows".
  const qint64 lowMs = m_viewStartMs - qMin(m_viewStartMs, kMarginMs);
  const qint64 highMs =
      m_viewEndMs > kMaxMs - kMarginMs ? kMaxMs : m_viewEndMs + kMarginMs;

  QList<int> next;
  next.reserve(m_visibleIndices.size() + 16);
  for (int i = 0; i < m_spans.size(); ++i) {
    if (m_spans.at(i).endMs >= lowMs && m_spans.at(i).startMs <= highMs)
      next.append(i);
  }

  if (next != m_visibleIndices) {
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
  for (int i : next)
    refreshed.append(m_allClips.at(i));
  if (refreshed == m_visibleClips)
    return;
  m_visibleClips = std::move(refreshed);
  if (!m_visibleClips.isEmpty())
    emit dataChanged(index(0, 0), index(m_visibleClips.size() - 1, 0),
                     {ModelDataRole, Qt::DisplayRole});
}

void TimelineClipModel::applyVisible(const QList<int> &nextIndices) {
  const QSet<int> keep(nextIndices.constBegin(), nextIndices.constEnd());

  // Rows that left, in contiguous runs and taken from the back, so the row
  // numbers still ahead of each run are the ones the view currently knows.
  for (int row = m_visibleIndices.size() - 1; row >= 0;) {
    if (keep.contains(m_visibleIndices.at(row))) {
      --row;
      continue;
    }
    int first = row;
    while (first > 0 && !keep.contains(m_visibleIndices.at(first - 1)))
      --first;
    beginRemoveRows({}, first, row);
    m_visibleIndices.remove(first, row - first + 1);
    m_visibleClips.remove(first, row - first + 1);
    endRemoveRows();
    row = first - 1;
  }

  // What is left is a subsequence of nextIndices, so one forward walk places
  // each run of arrivals at the row it belongs on. Rows present in both lists
  // are never touched structurally - their delegates survive the scroll.
  int row = 0;
  for (int pos = 0; pos < nextIndices.size();) {
    if (row < m_visibleIndices.size() &&
        m_visibleIndices.at(row) == nextIndices.at(pos)) {
      const QVariant &clip = m_allClips.at(nextIndices.at(pos));
      if (m_visibleClips.at(row) != clip) {
        m_visibleClips[row] = clip;
        emit dataChanged(index(row, 0), index(row, 0),
                         {ModelDataRole, Qt::DisplayRole});
      }
      ++row;
      ++pos;
      continue;
    }
    int count = 0;
    while (pos + count < nextIndices.size() &&
           (row >= m_visibleIndices.size() ||
            m_visibleIndices.at(row) != nextIndices.at(pos + count)))
      ++count;
    beginInsertRows({}, row, row + count - 1);
    for (int k = 0; k < count; ++k) {
      m_visibleIndices.insert(row + k, nextIndices.at(pos + k));
      m_visibleClips.insert(row + k, m_allClips.at(nextIndices.at(pos + k)));
    }
    endInsertRows();
    row += count;
    pos += count;
  }
}
