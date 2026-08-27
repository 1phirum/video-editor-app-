
#include "app/lumetri/custom_blur_pipeline.h"

#include <QVariantMap>
#include <QtGlobal>

namespace {
double bounded(const QVariantMap &values, const QString &key, double fallback,
               double minimum, double maximum) {
  return qBound(minimum, values.value(key, fallback).toDouble(), maximum);
}
} // namespace

QVariantList CustomBlurPipeline::enabledMasks(const QVariantList &effectStack) {
  QVariantList result;
  for (const QVariant &value : effectStack) {
    const QVariantMap instance = value.toMap();
    if (!instance.value("enabled", true).toBool() ||
        instance.value("definitionId").toString() !=
            QStringLiteral("custom_blur"))
      continue;

    const QVariantMap parameters = instance.value("parameters").toMap();
    const double amount = bounded(parameters, "amount", 12.0, 0.0, 30.0);
    const QVariantMap mask = parameters.value("mask").toMap();
    const double x = bounded(mask, "x", 0.30, 0.0, 0.98);
    const double y = bounded(mask, "y", 0.35, 0.0, 0.98);
    const double width =
        qMin(bounded(mask, "width", 0.40, 0.02, 1.0), 1.0 - x);
    const double height =
        qMin(bounded(mask, "height", 0.30, 0.02, 1.0), 1.0 - y);
    if (amount > 0.001 && width >= 0.02 && height >= 0.02)
      result.append(QVariantMap{{"amount", amount},
                                {"x", x},
                                {"y", y},
                                {"width", width},
                                {"height", height}});
  }
  return result;
}

QString CustomBlurPipeline::appendFilters(QStringList *filters,
                                          const QString &inputLabel,
                                          const QString &labelPrefix,
                                          const QVariantList &masks) {
  if (!filters || masks.isEmpty())
    return inputLabel;

  QString current = inputLabel;
  for (int i = 0; i < masks.size(); ++i) {
    const QVariantMap mask = masks.at(i).toMap();
    const QString keep = QStringLiteral("%1_keep%2").arg(labelPrefix).arg(i);
    const QString blurSource =
        QStringLiteral("%1_blursrc%2").arg(labelPrefix).arg(i);
    const QString region =
        QStringLiteral("%1_region%2").arg(labelPrefix).arg(i);
    const QString output =
        QStringLiteral("%1_masked%2").arg(labelPrefix).arg(i);
    const double x = mask.value("x").toDouble();
    const double y = mask.value("y").toDouble();
    const double width = mask.value("width").toDouble();
    const double height = mask.value("height").toDouble();
    const double amount = mask.value("amount").toDouble();

    filters->append(QStringLiteral("[%1]split=2[%2][%3]")
                        .arg(current, keep, blurSource));
    filters->append(
        QStringLiteral(
            "[%1]crop=w='max(2,min(iw,trunc(iw*%2/2)*2))':"
            "h='max(2,min(ih,trunc(ih*%3/2)*2))':"
            "x='min(iw-ow,max(0,iw*%4))':y='min(ih-oh,max(0,ih*%5))',"
            "gblur=sigma=%6[%7]")
            .arg(blurSource)
            .arg(width, 0, 'f', 6)
            .arg(height, 0, 'f', 6)
            .arg(x, 0, 'f', 6)
            .arg(y, 0, 'f', 6)
            .arg(amount, 0, 'f', 3)
            .arg(region));
    filters->append(
        QStringLiteral("[%1][%2]overlay=x='main_w*%3':y='main_h*%4':"
                       "eof_action=pass:shortest=1[%5]")
            .arg(keep, region)
            .arg(x, 0, 'f', 6)
            .arg(y, 0, 'f', 6)
            .arg(output));
    current = output;
  }
  return current;
}
