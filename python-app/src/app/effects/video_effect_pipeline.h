#pragma once

#include <QString>
#include <QVariantList>
#include <QtGlobal>

class VideoEffectPipeline {
public:
  // clipStartMs/clipDurationMs are where the clip sits on the sequence, and
  // they matter because of where this chain is spliced in: the export graph
  // runs it after `setpts=PTS-STARTPTS+clipStart/TB`, so `t` inside any filter
  // here is sequence time, not time from the head of the clip. An effect that
  // has to act on the clip's own edges - the transitions - cannot work out
  // where those edges are from the stack alone.
  //
  // The default of 0 means "no clip to speak of", which is the honest answer
  // for the effect-lane and thumbnail-preview callers: they hand over a bare
  // stack. Edge transitions emit nothing in that case rather than fading at a
  // time that belongs to some other clip.
  static QString filters(const QVariantList &effectStack,
                         qint64 clipStartMs = 0, qint64 clipDurationMs = 0);
};
