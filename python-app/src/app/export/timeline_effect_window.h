#pragma once

#include <QString>
#include <QVariantList>
#include <QtGlobal>

// Turns an effect-track bar's stack into a filter chain that only acts over the
// stretch the bar covers.
//
// The obvious implementations do not survive contact with ffmpeg. Cutting the
// window out of the stream (split -> trim -> effect -> overlay/concat) stalls:
// both branches are pulled from one decode, so the branch that is not being
// consumed yet buffers every frame up to the window - minutes of frames on the
// multi-hour sources this app is built for. So the window is expressed with each
// filter's own `enable=` option instead, which is exactly what ffmpeg's timeline
// support is for and costs nothing.
//
// Not every filter has that support, so `supportsWindowing` is what the drop
// path asks before it puts a bar on the lane: an effect that cannot be gated is
// refused with a message rather than silently applied to the whole clip.
class TimelineEffectWindow {
public:
  // Empty when the stack has nothing to apply, which includes a bar carrying
  // only Custom Blur - that one is a crop/blur/overlay of a sub-rectangle, and
  // CustomBlurPipeline windows it through its own keyframe curve.
  static QString gatedFilters(const QVariantList &effectStack,
                              qint64 windowStartMs, qint64 windowEndMs);

  // Whether every filter this effect emits can carry `enable=`. Derived from the
  // effect's own default chain rather than from a hand-kept list of ids, so an
  // effect added to the registry later is classified without touching this file.
  static bool supportsWindowing(const QString &effectId);
};
