#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QList>

namespace KhmerSupport {
struct RasterSubtitle {
  QString path;
  double start = 0.0;
  double end = 0.0;
};

QString fontDirectory();
QString preferredFont(const QString &requested = {});
QString normalize(const QString &text);
bool renderRaster(const QVariantList &segments, const QVariantMap &style,
                  int width, int height, QList<RasterSubtitle> *rendered,
                  QString *error);
bool writeAss(const QString &path, const QVariantList &segments,
              const QVariantMap &style, int width, int height, QString *error);
}
