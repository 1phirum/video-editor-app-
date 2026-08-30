#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class CustomBlurPipeline {
public:
  // `keyframes` is the clip's animation channels, keyed by channel name
  // ("fx:<instanceId>:amount" for an effect-instance parameter). When a Custom
  // Blur has a keyframed Blurriness, the returned mask carries the frame list so
  // the filter chain can follow it instead of baking in one value.
  static QVariantList enabledMasks(const QVariantList &effectStack,
                                   const QVariantMap &keyframes = {});
  // The same masks, but only alive between `startMs` and `endMs` (timeline ms).
  // This is what an effect-track item contributes to a clip it covers: outside
  // the window the region is composited back untouched, so the extent of the bar
  // on the timeline is what decides when the blur is on.
  static QVariantList windowedMasks(const QVariantList &effectStack,
                                    const QVariantMap &keyframes,
                                    qint64 startMs, qint64 endMs);
  static QString appendFilters(QStringList *filters, const QString &inputLabel,
                               const QString &labelPrefix,
                               const QVariantList &masks);

private:
  // Both public collectors differ only in whether the amount curve is clipped to
  // a window, so the mask geometry and the "is this worth emitting" test live in
  // one place - a divergence between the two would show up as a blur that
  // exports from the effect track but not from the clip, or the reverse.
  static QVariantList collectMasks(const QVariantList &effectStack,
                                   const QVariantMap &keyframes, bool windowed,
                                   qint64 startMs, qint64 endMs);
};
