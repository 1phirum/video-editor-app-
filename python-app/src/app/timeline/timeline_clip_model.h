#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QVariantList>
#include <limits>

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
class TimelineClipModel final : public QAbstractListModel {
  Q_OBJECT

public:
  enum Role { ModelDataRole = Qt::UserRole + 1 };
  explicit TimelineClipModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setClips(const QVariantList &clips);
  Q_INVOKABLE void setViewport(qint64 startMs, qint64 endMs);
  Q_INVOKABLE void clearViewport();

private:
  // `clipDataChanged` says the underlying clips may differ even where the set of
  // visible rows does not, so an unchanged projection still has to be refreshed
  // in place instead of being skipped.
  void rebuild(bool clipDataChanged);
  // Moves the projection to `nextIndices` (ascending indices into m_allClips)
  // using row-level signals only.
  void applyVisible(const QList<int> &nextIndices);

  QVariantList m_allClips;
  QVariantList m_visibleClips;
  // startMs/endMs of every m_allClips entry, in the same order. Scrolling asks
  // "which clips touch this window" on every content-x change, and doing that
  // through QVariantMap lookups meant ~25,000 string-keyed hash lookups per
  // scroll event on a transcript-length track. These are read instead.
  struct Span {
    qint64 startMs;
    qint64 endMs;
  };
  QList<Span> m_spans;
  // Which m_allClips entry each visible row came from, ascending. Kept so a new
  // projection can be diffed against the current one.
  QList<int> m_visibleIndices;
  qint64 m_viewStartMs = 0;
  qint64 m_viewEndMs = std::numeric_limits<qint64>::max();
};
