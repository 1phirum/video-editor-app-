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
