#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariant>
#include <QHash>

#include "core/module_api.h"

class CUTPRO_TIMELINE_API KeyframeEngine final : public QObject {
  Q_OBJECT
public:
  explicit KeyframeEngine(QObject *parent = nullptr);

  Q_INVOKABLE bool addKeyframe(const QString &clipId, const QString &property,
                               qint64 timeMs, const QVariant &value);
  Q_INVOKABLE bool removeKeyframe(const QString &clipId,
                                  const QString &property, qint64 timeMs);
  Q_INVOKABLE bool toggleKeyframing(const QString &clipId,
                                    const QString &property, qint64 timeMs,
                                    const QVariant &value);
  Q_INVOKABLE QVariant interpolatedValue(const QString &clipId,
                                         const QString &property,
                                         qint64 timeMs) const;
  // Rendering entry point: the interpolated value at timeMs when the channel is
  // keyframed, otherwise `fallback` (the clip's static effect value). Returns a
  // plain double so the monitor transform and the export builder can bind to it
  // directly. This is what makes a keyframed parameter actually animate - the
  // value the preview draws now follows the playhead instead of standing still.
  Q_INVOKABLE double valueAt(const QString &clipId, const QString &property,
                             qint64 timeMs, double fallback) const;
  Q_INVOKABLE QVariantList keyframesFor(const QString &clipId,
                                        const QString &property) const;
  Q_INVOKABLE bool isKeyframed(const QString &clipId,
                               const QString &property) const;
  Q_INVOKABLE qint64 previousTime(const QString &clipId,
                                  const QString &property,
                                  qint64 timeMs) const;
  Q_INVOKABLE qint64 nextTime(const QString &clipId,
                              const QString &property,
                              qint64 timeMs) const;

  // Interpolation of the segment *leaving* this keyframe, plus the temporal
  // ease influences used to shape it. `mode` is "linear", "hold" or "bezier";
  // easeOut shapes the departure from this keyframe and easeIn the arrival at
  // it, each 0..1, exactly like Premiere's temporal-ease percentages.
  Q_INVOKABLE bool setInterpolation(const QString &clipId,
                                    const QString &property, qint64 timeMs,
                                    const QString &mode, double easeIn,
                                    double easeOut);
  Q_INVOKABLE QVariantMap keyframeAt(const QString &clipId,
                                     const QString &property,
                                     qint64 timeMs) const;

  // Copy every channel of `from` onto `to`, shifting each keyframe time by
  // `shiftMs`. Keyframe times are absolute timeline positions, so any edit that
  // gives a piece of a clip a new id (split, silence removal, transcript
  // deletion) has to carry the curves across or the animation is silently lost
  // - and silently is the word: nothing shows it until export.
  void cloneClip(const QString &from, const QString &to, qint64 shiftMs = 0);
  // Keep only the keyframes inside [fromMs, toMs] and shift the survivors by
  // `shiftMs`. Used for a piece that survives an edit which removed spans
  // around it, so its curve keeps the shape it had over that span.
  void clampClip(const QString &clipId, qint64 fromMs, qint64 toMs,
                 qint64 shiftMs);
  // Shift every keyframe of one clip, for a ripple that moved it in time.
  void shiftClip(const QString &clipId, qint64 shiftMs);
  bool hasAnyChannel(const QString &clipId) const;

  // Channel name for a parameter of one effect *instance*. Built-in clip
  // properties are their own channel name; an instance parameter needs the
  // instance id in the name, or two Custom Blurs on one clip - or a Gaussian
  // Blur and a Custom Blur, which both call their parameter "amount" - would
  // share a single animation curve.
  Q_INVOKABLE QString instanceChannel(const QString &instanceId,
                                      const QString &parameterId) const;
  Q_INVOKABLE double instanceValueAt(const QString &clipId,
                                     const QString &instanceId,
                                     const QString &parameterId, qint64 timeMs,
                                     double fallback) const;

  // Project persistence. A list rather than a map so the clip id and the
  // property stay separate fields instead of being packed into one key.
  QVariantList serialize() const;
  void restore(const QVariantList &channels);
  // Every channel of one clip, keyed by property. This is what goes to the
  // export builder, so a rendered file animates the same way the monitor does.
  QVariantMap channelsForClip(const QString &clipId) const;
  void forgetClip(const QString &clipId);

signals:
  void keyframesChanged(const QString &clipId);

private:
  QString key(const QString &clipId, const QString &property) const;
  QHash<QString, QVariantList> m_channels;
};
