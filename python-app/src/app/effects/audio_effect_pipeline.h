#pragma once

#include <QString>
#include <QVariantList>
#include <QtGlobal>

class AudioEffectPipeline {
public:
  // clipDurationMs is how long the clip plays for, needed only by the edge
  // transitions. Note the clock here is not the one the video chain runs on:
  // the export graph builds audio as
  // `atrim,asetpts=PTS-STARTPTS,aresample,<this chain>,adelay=<clipStart>`, so
  // `t` inside these filters starts at 0 at the head of the clip and where the
  // clip sits on the sequence is irrelevant. 0 means "not a clip" - the fades
  // then emit nothing instead of guessing an edge.
  static QString filters(const QVariantList &effectStack, int channels,
                         qint64 clipDurationMs = 0);
};
