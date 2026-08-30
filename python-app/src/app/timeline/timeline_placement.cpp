#include "app/timeline/timeline_placement.h"

#include <QRegularExpression>

QString TimelinePlacement::normalizedTrack(const QString &track) {
  static const QRegularExpression pattern(QStringLiteral("^([VASF])(\\d+)$"));
  const QString candidate = track.trimmed().toUpper();
  const auto match = pattern.match(candidate);
  if (!match.hasMatch())
    return {};

  const int number = match.captured(2).toInt();
  if (number < 1 || number > 64)
    return {};
  // S and F are single overlay lanes: one subtitle row and one effect row, the
  // way CapCut has exactly one of each above the video stack.
  if ((match.captured(1) == QStringLiteral("S") ||
       match.captured(1) == QStringLiteral("F")) &&
      number != 1)
    return {};
  return match.captured(1) + QString::number(number);
}

QString TimelinePlacement::defaultTrackForKind(const QString &kind) {
  const QString normalizedKind = kind.trimmed().toLower();
  if (normalizedKind == QStringLiteral("audio"))
    return QStringLiteral("A1");
  if (normalizedKind == QStringLiteral("subtitle"))
    return QStringLiteral("S1");
  if (normalizedKind == QStringLiteral("effect"))
    return QStringLiteral("F1");
  return QStringLiteral("V1");
}

bool TimelinePlacement::trackAcceptsKind(const QString &track,
                                         const QString &kind) {
  const QString normalized = normalizedTrack(track);
  if (normalized.isEmpty())
    return false;

  const QString normalizedKind = kind.trimmed().toLower();
  if (normalizedKind == QStringLiteral("subtitle"))
    return normalized == QStringLiteral("S1");
  if (normalizedKind == QStringLiteral("effect"))
    return normalized == QStringLiteral("F1");
  if (normalizedKind == QStringLiteral("audio"))
    return normalized.startsWith(QLatin1Char('A'));
  // The overlay lanes only take their own kind, so a video dropped on them has
  // to be turned away rather than quietly parked on a row that cannot draw it.
  return normalized.startsWith(QLatin1Char('V'));
}

int TimelinePlacement::trackNumber(const QString &track) {
  const QString normalized = normalizedTrack(track);
  return normalized.isEmpty() ? 0 : normalized.mid(1).toInt();
}

QString TimelinePlacement::shiftedTrack(const QString &track, int delta) {
  const QString normalized = normalizedTrack(track);
  if (normalized.isEmpty())
    return {};

  const int number = trackNumber(normalized) + delta;
  if (number < 1 || number > 64)
    return {};
  if (normalized.startsWith(QLatin1Char('S')) && number != 1)
    return {};
  return normalized.left(1) + QString::number(number);
}

QString TimelinePlacement::trackForRow(int row, int videoCount,
                                       int audioCount) {
  const int videos = qMax(1, videoCount);
  const int audios = qMax(0, audioCount);
  if (row < 0 || row >= videos + audios)
    return {};
  // Video tracks count downwards: row 0 is the topmost lane, which is the
  // highest-numbered video track.
  if (row < videos)
    return QStringLiteral("V%1").arg(videos - row);
  return QStringLiteral("A%1").arg(row - videos + 1);
}

int TimelinePlacement::rowForTrack(const QString &track, int videoCount,
                                  int audioCount) {
  const QString normalized = normalizedTrack(track);
  const int number = trackNumber(normalized);
  if (number < 1)
    return -1;
  const int videos = qMax(1, videoCount);
  if (normalized.startsWith(QLatin1Char('V')))
    return number > videos ? -1 : videos - number;
  if (normalized.startsWith(QLatin1Char('A')))
    return number > qMax(0, audioCount) ? -1 : videos + number - 1;
  return -1;
}

QString TimelinePlacement::nearestCompatibleTrack(const QString &kind,
                                                  const QString &requested,
                                                  int videoCount,
                                                  int audioCount) {
  if (trackAcceptsKind(requested, kind))
    return normalizedTrack(requested);

  const int rows = qMax(1, videoCount) + qMax(0, audioCount);
  int from = rowForTrack(requested, videoCount, audioCount);
  if (from < 0) {
    // A track that does not exist yet - the V<count+1> a drop above the stack
    // asks for - still says which side of the stack the pointer was on.
    from = normalizedTrack(requested).startsWith(QLatin1Char('A')) ? rows : -1;
  }
  for (int distance = 1; distance <= rows; ++distance) {
    const QString above = trackForRow(from - distance, videoCount, audioCount);
    if (trackAcceptsKind(above, kind))
      return above;
    const QString below = trackForRow(from + distance, videoCount, audioCount);
    if (trackAcceptsKind(below, kind))
      return below;
  }
  return {};
}
