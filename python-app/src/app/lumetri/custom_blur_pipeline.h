#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

class CustomBlurPipeline {
public:
  static QVariantList enabledMasks(const QVariantList &effectStack);
  static QString appendFilters(QStringList *filters, const QString &inputLabel,
                               const QString &labelPrefix,
                               const QVariantList &masks);
};
