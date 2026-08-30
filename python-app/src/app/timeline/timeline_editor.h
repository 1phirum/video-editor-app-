#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

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
};
