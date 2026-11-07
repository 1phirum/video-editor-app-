#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QPair>

// Pure timeline operations shared by the Qt backend and focused tests. The
// Backend owns history, ids, persistence, and signals; this class only applies
// time-domain edits to value-type clip data.
class TimelineEditor {
public:
  // What a moving edge locked onto. A snapped time on its own is not enough for
  // a live drag: the view also has to know that a pull happened at all, and
  // where to stand the guide line, which is the target's time and not the
  // clip's new start whenever it was the tail edge that won.
  struct SnapResult {
    bool snapped = false;
    qint64 deltaMs = 0;    // add to the requested start to apply the snap
    qint64 guideMs = 0;    // the join itself: where the indicator belongs
    qint64 distanceMs = 0; // how far the edge was pulled
    QString edge;          // "start" or "end", the edge of the moving clip
    QString target;        // clip | playhead | marker | sequenceStart/End
  };

  static qint64 snapTime(qint64 requestedMs, const QVariantList &clips,
                         const QVariantList &markers,
                         const QStringList &excludedClipIds, qint64 playheadMs,
                         qint64 thresholdMs, qint64 durationMs);

  // Nearest target for one moving edge.
  static SnapResult snapEdge(qint64 requestedMs, const QVariantList &clips,
                             const QVariantList &markers,
                             const QStringList &excludedClipIds,
                             qint64 playheadMs, qint64 thresholdMs,
                             qint64 sequenceDurationMs);

  // Nearest target for either edge of a clip being dragged whole. Both edges
  // compete, so a clip whose tail lands on the next clip's head snaps there
  // even though its head is nowhere near anything.
  static SnapResult snapClip(qint64 startMs, qint64 clipDurationMs,
                             const QVariantList &clips,
                             const QVariantList &markers,
                             const QStringList &excludedClipIds,
                             qint64 playheadMs, qint64 thresholdMs,
                             qint64 sequenceDurationMs);

  static bool rippleDelete(QVariantList &clips, const QStringList &clipIds,
                           const QStringList &syncLockedTracks,
                           QStringList *removedIds = nullptr);
  static bool rippleTrimEnd(QVariantList &clips, const QString &clipId,
                            qint64 requestedEnd,
                            const QStringList &syncLockedTracks,
                            qint64 *deltaMs = nullptr);
  static bool closeGap(QVariantList &clips, const QString &track,
                       qint64 gapStartMs, qint64 gapEndMs);

  // Shift clips left to close a set of removed timeline intervals, the same way
  // rippleDelete closes the gap a removed clip leaves - but driven by raw
  // intervals rather than by clip ids. Each clip on `track` (or on any
  // sync-locked track) moves left by the total length of the intervals that lie
  // entirely to its left. Used by the silence cut, which has already replaced
  // the analysed clip with its surviving pieces and now needs everything after
  // each removed span pulled back. Overlapping intervals are merged first, so a
  // clip is never shifted by more than the real removed duration.
  static void rippleRemoveIntervals(QVariantList &clips, const QString &track,
                                    QVector<QPair<qint64, qint64>> intervals,
                                    const QStringList &syncLockedTracks);

  // What is under the pointer when the track there is empty. The bounds are the
  // end of the clip before the hole and the start of the clip after it, which is
  // exactly the pair closeGap() shifts by - so a gap this reports can always be
  // closed, and one it does not report never can.
  //
  // "found" is false for empty space that is not a gap at all: inside a clip,
  // and after the last clip on the track, where there is nothing on the right to
  // pull back. Leading space before the first clip does count - startMs is 0
  // there - because sliding the whole track up to the head of the sequence is a
  // real edit.
  struct GapResult {
    bool found = false;
    qint64 startMs = 0;
    qint64 endMs = 0;
  };

  static GapResult gapAt(const QVariantList &clips, const QString &track,
                         qint64 positionMs);

  // Every gap on the track, closed from the left. Repeated because closing one
  // moves all the later clips, so the next hole is somewhere new each time; the
  // count is what the UI reports back.
  static int closeAllGaps(QVariantList &clips, const QString &track);
};
