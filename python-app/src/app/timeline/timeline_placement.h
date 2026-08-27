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
};
