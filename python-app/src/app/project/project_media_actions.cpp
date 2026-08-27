#include "app/project/project_media_actions.h"

#include <QSet>

namespace ProjectMediaActions {

QStringList normalizeSelection(const QVariantList &media,
                               const QStringList &requestedIds) {
  QSet<QString> available;
  for (const QVariant &value : media) {
    const QString id = value.toMap().value(QStringLiteral("id")).toString();
    if (!id.isEmpty())
      available.insert(id);
  }

  QStringList result;
  QSet<QString> seen;
  for (const QString &id : requestedIds) {
    if (!id.isEmpty() && available.contains(id) && !seen.contains(id)) {
      result.append(id);
      seen.insert(id);
    }
  }
  return result;
}

qint64 appendPosition(const QVariantList &clips) {
  qint64 end = 0;
  for (const QVariant &value : clips) {
    const QVariantMap clip = value.toMap();
    const qint64 start =
        qMax<qint64>(0, clip.value(QStringLiteral("startMs")).toLongLong());
    const qint64 duration = qMax<qint64>(
        0, clip.value(QStringLiteral("durationMs")).toLongLong());
    end = qMax(end, start + duration);
  }
  return end;
}

} // namespace ProjectMediaActions
