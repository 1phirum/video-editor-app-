#pragma once

#include <QtGlobal>
#include <QVariantMap>

// Centralizes thresholds and UI-facing flags for media that should not be
// eagerly expanded or opened by the timeline and monitor. The source file is
// never modified; playback and export continue to use the original media.
class LargeMediaPolicy final {
public:
  // Decimal "1 GB" files are smaller than one GiB. Start the lightweight
  // path earlier so typical 700 MB-1 GB downloads never enter Qt Multimedia's
  // expensive eager-open path. Long-form sources also benefit even if highly
  // compressed.
  static constexpr qint64 kLongDurationMs = 15LL * 60 * 1000;
  static constexpr qint64 kLargeFileBytes = 512LL * 1024 * 1024;

  static bool requiresLightweightHandling(const QVariantMap &media);
  static void applyPresentationFlags(QVariantMap *media);
};
