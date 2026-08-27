#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariant>
#include <QHash>

class KeyframeEngine final : public QObject {
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

signals:
  void keyframesChanged(const QString &clipId);

private:
  QString key(const QString &clipId, const QString &property) const;
  QHash<QString, QVariantList> m_channels;
};
