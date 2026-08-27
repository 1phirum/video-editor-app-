#include "app/timeline/timeline_placement.h"

#include <QRegularExpression>

QString TimelinePlacement::normalizedTrack(const QString &track) {
  static const QRegularExpression pattern(QStringLiteral("^([VAS])(\\d+)$"));
  const QString candidate = track.trimmed().toUpper();
  const auto match = pattern.match(candidate);
  if (!match.hasMatch())
    return {};

  const int number = match.captured(2).toInt();
  if (number < 1 || number > 64)
    return {};
  if (match.captured(1) == QStringLiteral("S") && number != 1)
    return {};
  return match.captured(1) + QString::number(number);
}

QString TimelinePlacement::defaultTrackForKind(const QString &kind) {
  const QString normalizedKind = kind.trimmed().toLower();
  if (normalizedKind == QStringLiteral("audio"))
    return QStringLiteral("A1");
  if (normalizedKind == QStringLiteral("subtitle"))
    return QStringLiteral("S1");
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
  if (normalizedKind == QStringLiteral("audio"))
    return normalized.startsWith(QLatin1Char('A'));
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
