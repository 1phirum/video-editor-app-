#pragma once

#include <QSet>
#include <QStringList>
#include <QVariantList>

// Pure timeline operations shared by the Qt backend and focused tests. The
// Backend owns history, ids, persistence, and signals; this class only applies
// time-domain edits to value-type clip data.
class TimelineEditor {
public:
  static qint64 snapTime(qint64 requestedMs, const QVariantList &clips,
                         const QVariantList &markers,
                         const QStringList &excludedClipIds, qint64 playheadMs,
                         qint64 thresholdMs, qint64 durationMs);

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
