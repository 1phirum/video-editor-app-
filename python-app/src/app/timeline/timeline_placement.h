#pragma once

#include <QString>

// Track naming and clip-kind placement rules shared by timeline operations.
class TimelinePlacement {
public:
  static QString normalizedTrack(const QString &track);
  static QString defaultTrackForKind(const QString &kind);
  static bool trackAcceptsKind(const QString &track, const QString &kind);
  static int trackNumber(const QString &track);
  static QString shiftedTrack(const QString &track, int delta);

  // Row order as the timeline draws it, top to bottom: V<count> … V1, A1 …
  // A<count>. Placement questions are questions about rows on screen - "which
  // track is under the pointer", "which is the nearest one that can take this
  // clip" - so they are answered in row space rather than in track numbers,
  // where V1 and A1 are neighbours in the name but a whole stack apart.
  static QString trackForRow(int row, int videoCount, int audioCount);
  static int rowForTrack(const QString &track, int videoCount, int audioCount);

  // The track a clip of `kind` should land on when the pointer asked for
  // `requested`. Returns `requested` when it already accepts the kind, else the
  // nearest row that does, else an empty string. Never silently rewrites a
  // request to track 1: a video dropped over the audio block lands on the
  // bottom video track because that is the adjacent one, not because V1 is the
  // default.
  static QString nearestCompatibleTrack(const QString &kind,
                                        const QString &requested,
                                        int videoCount, int audioCount);
};
