#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class SequenceExportBuilder {
public:
  static QStringList build(const QVariantList &media, const QVariantList &clips,
                           const QStringList &mutedTracks,
                           const QVariantList &trackStates, qint64 durationMs,
                           const QVariantMap &colorSettings,
                           const QVariantMap &settings,
                           const QString &outputPath,
                           QString *error);
};
