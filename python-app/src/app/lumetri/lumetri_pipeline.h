#pragma once

#include <QString>
#include <QVariantMap>

// Produces the FFmpeg video-filter chain for one timeline clip. Keeping this
// out of the QML bridge makes export deterministic and testable from C++.
class LumetriPipeline {
public:
  static QString filterForClip(const QVariantMap &clip,
                               const QVariantMap &media,
                               const QVariantMap &project);
};
