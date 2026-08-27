#pragma once

#include <QVariantList>

class SubtitleTimeline {
public:
  static QVariantList clipsFromTranscript(const QVariantList &transcript);
};
