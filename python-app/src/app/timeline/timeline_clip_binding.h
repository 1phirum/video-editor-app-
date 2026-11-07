#pragma once

#include <QVariantList>

class TimelineClipBinding {
public:
  static void collapseLegacyEmbeddedAudio(QVariantList *clips,
                                          const QVariantList &media);
};
