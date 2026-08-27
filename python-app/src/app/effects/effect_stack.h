#pragma once

#include <QVariantList>
#include <QVariantMap>

class EffectStack {
public:
  static QVariantMap create(const QVariantMap &definition,
                            const QString &instanceId);
  static bool append(QVariantList *stack, const QVariantMap &definition,
                     const QString &instanceId);
  static bool remove(QVariantList *stack, const QString &instanceId);
  static bool move(QVariantList *stack, const QString &instanceId, int offset);
  static bool setEnabled(QVariantList *stack, const QString &instanceId,
                         bool enabled);
  static bool setParameter(QVariantList *stack, const QString &instanceId,
                           const QVariantMap &definition,
                           const QString &parameterId, const QVariant &value);
  static bool reset(QVariantList *stack, const QString &instanceId,
                    const QVariantMap &definition);
  static QVariantList normalized(const QVariantList &stack);
};
