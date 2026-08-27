#pragma once

#include <QString>

class MediaPreviewGenerator {
public:
  // Filmstrip geometry. The timeline draws clip thumbnails one cell at a time,
  // so it needs the cell count and pixel size that filmstrip() bakes into the
  // cached image. Changing these invalidates nothing on disk by itself — bump
  // the cache variant in filmstrip() as well.
  static constexpr int kFilmstripFrames = 12;
  static constexpr int kFilmstripFrameWidth = 160;
  static constexpr int kFilmstripFrameHeight = 90;

  static QString thumbnail(const QString &path, qint64 durationMs);
  static QString filmstrip(const QString &path, qint64 durationMs);
  static QString waveform(const QString &path);

private:
  static QString cachedOutput(const QString &path, const QString &variant,
                              const QString &extension);
  static QString run(const QString &path, const QString &outputPath,
                     const QStringList &arguments, int timeoutMs,
                     bool seekBeforeInput = false);
};
