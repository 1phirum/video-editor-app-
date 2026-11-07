#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class ClipEffectsPipeline {
public:
  // `channels` is the clip's keyframe channels (property name -> frame list,
  // as KeyframeEngine::channelsForClip returns them). Where a channel can be
  // expressed as an FFmpeg expression in `t`, the parameter animates in the
  // rendered file; where it cannot, the static value in `effects` is used.
  //
  // Keyframe times are absolute timeline milliseconds and the export graph
  // setpts-es every clip onto its timeline position before these stages, so `t`
  // in the emitted expressions is the same clock the keyframes are stored in.
  //
  // Animated today: Position X/Y (overlay x/y) and Rotation (rotate angle).
  // Still static: Scale (FFmpeg's `scale` takes no `t`; it needs zoompan) and
  // Opacity (`colorchannelmixer` takes no `t`).
  static QString videoFilters(const QVariantMap &effects,
                              const QVariantMap &channels = {});
  static QString overlayX(const QVariantMap &effects,
                          const QVariantMap &channels = {});
  static QString overlayY(const QVariantMap &effects,
                          const QVariantMap &channels = {});
  // FFmpeg `blend` filter spec (the text after "blend=") for the clip's blend
  // mode, e.g. "all_mode=multiply". Empty means a plain source-over composite
  // (the "normal" fast path, and the fallback for modes FFmpeg cannot express).
  static QString blendMode(const QVariantMap &effects);
  static QString audioFilters(const QVariantMap &effects, int channels);

  // A piecewise expression in `t` (seconds) tracing one keyframe channel, or an
  // empty string when the channel cannot animate (fewer than two usable
  // keyframes). `scale` and `offset` map the stored value into the units the
  // filter wants: emitted = stored * scale + offset.
  //
  // Eased segments are subdivided into straight pieces evaluated with
  // KeyframeEngine::ease, so the curve in the file is the curve the monitor
  // drew rather than a second, differently-shaped approximation of it.
  static QString channelExpression(const QVariantList &frames,
                                   double scale = 1.0, double offset = 0.0);
  // The largest absolute value the channel reaches, for the cases where a
  // filter needs a constant bound (rotate's output canvas) even though the
  // value itself animates.
  static double channelPeakAbs(const QVariantList &frames, double fallback);
};
