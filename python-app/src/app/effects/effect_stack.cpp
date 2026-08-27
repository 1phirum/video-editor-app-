#include "app/effects/effect_stack.h"

#include "app/effects/effect_registry.h"

#include <QtGlobal>

namespace {
QVariantMap defaultParameters(const QVariantMap &definition) {
  QVariantMap result;
  for (const auto &value : definition.value("parameters").toList()) {
    const QVariantMap parameter = value.toMap();
    result[parameter.value("id").toString()] = parameter.value("default");
  }
  return result;
}

QVariantMap parameterDefinition(const QVariantMap &definition,
                                const QString &parameterId) {
  for (const auto &value : definition.value("parameters").toList()) {
    const QVariantMap parameter = value.toMap();
    if (parameter.value("id").toString() == parameterId)
      return parameter;
  }
  return {};
}

QVariant normalizedParameter(const QVariantMap &parameter,
                             const QVariant &value) {
  const QString type = parameter.value("type").toString();
  if (type == QStringLiteral("bool"))
    return value.toBool();
  if (type == QStringLiteral("number")) {
    return qBound(parameter.value("minimum").toDouble(), value.toDouble(),
                  parameter.value("maximum").toDouble());
  }
  return value;
}

int instanceIndex(const QVariantList &stack, const QString &instanceId) {
  for (int i = 0; i < stack.size(); ++i) {
    if (stack.at(i).toMap().value("id").toString() == instanceId)
      return i;
  }
  return -1;
}
} // namespace

QVariantMap EffectStack::create(const QVariantMap &definition,
                                const QString &instanceId) {
  if (definition.isEmpty() || instanceId.isEmpty())
    return {};
  return {{"id", instanceId},
          {"definitionId", definition.value("id")},
          {"enabled", true},
          {"version", definition.value("version", 1)},
          {"parameters", defaultParameters(definition)}};
}

bool EffectStack::append(QVariantList *stack, const QVariantMap &definition,
                         const QString &instanceId) {
  if (!stack)
    return false;
  const QVariantMap instance = create(definition, instanceId);
  if (instance.isEmpty())
    return false;
  stack->append(instance);
  return true;
}

bool EffectStack::remove(QVariantList *stack, const QString &instanceId) {
  if (!stack)
    return false;
  const int index = instanceIndex(*stack, instanceId);
  if (index < 0)
    return false;
  stack->removeAt(index);
  return true;
}

bool EffectStack::move(QVariantList *stack, const QString &instanceId,
                       int offset) {
  if (!stack || offset == 0)
    return false;
  const int from = instanceIndex(*stack, instanceId);
  const int to = qBound(0, from + offset, stack->size() - 1);
  if (from < 0 || from == to)
    return false;
  stack->move(from, to);
  return true;
}

bool EffectStack::setEnabled(QVariantList *stack, const QString &instanceId,
                             bool enabled) {
  if (!stack)
    return false;
  const int index = instanceIndex(*stack, instanceId);
  if (index < 0)
    return false;
  QVariantMap instance = stack->at(index).toMap();
  if (instance.value("enabled", true).toBool() == enabled)
    return false;
  instance["enabled"] = enabled;
  (*stack)[index] = instance;
  return true;
}

bool EffectStack::setParameter(QVariantList *stack, const QString &instanceId,
                               const QVariantMap &definition,
                               const QString &parameterId,
                               const QVariant &value) {
  if (!stack)
    return false;
  const int index = instanceIndex(*stack, instanceId);
  const QVariantMap parameter = parameterDefinition(definition, parameterId);
  if (index < 0 || parameter.isEmpty())
    return false;
  QVariantMap instance = stack->at(index).toMap();
  QVariantMap parameters = defaultParameters(definition);
  const QVariantMap existing = instance.value("parameters").toMap();
  for (auto it = existing.cbegin(); it != existing.cend(); ++it)
    parameters[it.key()] = it.value();
  const QVariant normalized = normalizedParameter(parameter, value);
  if (parameters.value(parameterId) == normalized)
    return false;
  parameters[parameterId] = normalized;
  instance["parameters"] = parameters;
  (*stack)[index] = instance;
  return true;
}

bool EffectStack::reset(QVariantList *stack, const QString &instanceId,
                        const QVariantMap &definition) {
  if (!stack || definition.isEmpty())
    return false;
  const int index = instanceIndex(*stack, instanceId);
  if (index < 0)
    return false;
  QVariantMap instance = stack->at(index).toMap();
  const QVariantMap parameters = defaultParameters(definition);
  if (instance.value("parameters").toMap() == parameters &&
      instance.value("enabled", true).toBool())
    return false;
  instance["enabled"] = true;
  instance["parameters"] = parameters;
  (*stack)[index] = instance;
  return true;
}

QVariantList EffectStack::normalized(const QVariantList &stack) {
  QVariantList result;
  for (const auto &value : stack) {
    const QVariantMap saved = value.toMap();
    const QVariantMap definition =
        EffectRegistry::definition(saved.value("definitionId").toString());
    if (definition.isEmpty()) {
      QVariantMap missing = saved;
      missing["enabled"] = false;
      result.append(missing);
      continue;
    }

    QVariantMap instance = create(definition, saved.value("id").toString());
    if (instance.isEmpty())
      continue;
    instance["enabled"] = saved.value("enabled", true).toBool();
    QVariantMap parameters = instance.value("parameters").toMap();
    const QVariantMap savedParameters = saved.value("parameters").toMap();
    for (auto it = savedParameters.cbegin(); it != savedParameters.cend(); ++it) {
      const QVariantMap parameter = parameterDefinition(definition, it.key());
      if (!parameter.isEmpty())
        parameters[it.key()] = normalizedParameter(parameter, it.value());
    }
    instance["parameters"] = parameters;
    result.append(instance);
  }
  return result;
}
