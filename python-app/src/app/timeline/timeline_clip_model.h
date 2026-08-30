#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <limits>

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
    int firstIndex = 0;
    int count = 1;
    friend bool operator==(const Row &a, const Row &b) {
      return a.firstIndex == b.firstIndex && a.count == b.count;
    }
    friend bool operator!=(const Row &a, const Row &b) { return !(a == b); }
  };
  // Rows for the window, collapsing runs of unreadably narrow clips when
  // msPerPixel is known. `minSpanMs` is widened by the caller until the result
  // fits under kMaxRows.
  QList<Row> project(qint64 lowMs, qint64 highMs, qint64 minSpanMs) const;
  // Stable identity for a row, so the diff below can tell "the same row moved"
  // from "a different row arrived". Clip rows key on their index; cluster rows
  // key on their first index and length, so a run that grows or shrinks is
  // correctly reported as a replacement.
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
  // The row each visible row came from, in order. Kept so a new projection can
  // be diffed against the current one.
  QList<Row> m_visibleRows;
  qint64 m_viewStartMs = 0;
  qint64 m_viewEndMs = std::numeric_limits<qint64>::max();
  double m_msPerPixel = 0.0;
  // Last projection's shape, for statistics() only.
  int m_collapsedClips = 0;
  qint64 m_minSpanMs = 0;
};
