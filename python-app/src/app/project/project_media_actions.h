#pragma once

#include <QStringList>
#include <QVariantList>

namespace ProjectMediaActions {

QStringList normalizeSelection(const QVariantList &media,
                               const QStringList &requestedIds);
qint64 appendPosition(const QVariantList &clips);

} // namespace ProjectMediaActions
