#pragma once

#include <QString>
#include <QVariantMap>

class ClipEffectsPipeline {
public:
  static QString videoFilters(const QVariantMap &effects);
  static QString overlayX(const QVariantMap &effects);
  static QString overlayY(const QVariantMap &effects);
  // FFmpeg `blend` filter spec (the text after "blend=") for the clip's blend
  // mode, e.g. "all_mode=multiply". Empty means a plain source-over composite
  // (the "normal" fast path, and the fallback for modes FFmpeg cannot express).
  static QString blendMode(const QVariantMap &effects);
  static QString audioFilters(const QVariantMap &effects, int channels);
};
