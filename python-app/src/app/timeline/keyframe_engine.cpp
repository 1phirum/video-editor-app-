#include "app/timeline/keyframe_engine.h"

#include <algorithm>

KeyframeEngine::KeyframeEngine(QObject *parent) : QObject(parent) {}

QString KeyframeEngine::key(const QString &clipId,
                            const QString &property) const {
  return clipId + QChar('\x1f') + property;
}

bool KeyframeEngine::addKeyframe(const QString &clipId, const QString &property,
                                 qint64 timeMs, const QVariant &value) {
  if (clipId.isEmpty() || property.isEmpty() || timeMs < 0)
    return false;
  QVariantList &frames = m_channels[key(clipId, property)];
  for (QVariant &frame : frames) {
    QVariantMap item = frame.toMap();
    if (item.value("timeMs").toLongLong() == timeMs) {
      if (item.value("value") == value)
        return false;
      item["value"] = value;
      frame = item;
      emit keyframesChanged(clipId);
      return true;
    }
  }
  frames.append(QVariantMap{{"timeMs", timeMs}, {"value", value},
                            {"interpolation", QStringLiteral("linear")},
                            {"easeIn", 0.0}, {"easeOut", 0.0}});
  std::sort(frames.begin(), frames.end(), [](const QVariant &left,
                                             const QVariant &right) {
    return left.toMap().value("timeMs").toLongLong() <
           right.toMap().value("timeMs").toLongLong();
  });
  emit keyframesChanged(clipId);
  return true;
}

bool KeyframeEngine::removeKeyframe(const QString &clipId,
                                    const QString &property, qint64 timeMs) {
  const QString channel = key(clipId, property);
  auto it = m_channels.find(channel);
  if (it == m_channels.end())
    return false;
  QVariantList &frames = it.value();
  for (int i = 0; i < frames.size(); ++i) {
    if (frames.at(i).toMap().value("timeMs").toLongLong() == timeMs) {
      frames.removeAt(i);
      if (frames.isEmpty())
        m_channels.erase(it);
      emit keyframesChanged(clipId);
      return true;
    }
  }
  return false;
}

bool KeyframeEngine::toggleKeyframing(const QString &clipId,
                                      const QString &property, qint64 timeMs,
                                      const QVariant &value) {
  if (!isKeyframed(clipId, property))
    return addKeyframe(clipId, property, timeMs, value);
  return removeKeyframe(clipId, property, timeMs);
}

QVariantList KeyframeEngine::keyframesFor(const QString &clipId,
                                          const QString &property) const {
  return m_channels.value(key(clipId, property));
}

bool KeyframeEngine::isKeyframed(const QString &clipId,
                                 const QString &property) const {
  return !m_channels.value(key(clipId, property)).isEmpty();
}

QVariant KeyframeEngine::interpolatedValue(const QString &clipId,
                                           const QString &property,
                                           qint64 timeMs) const {
  const QVariantList frames = keyframesFor(clipId, property);
  if (frames.isEmpty())
    return {};
  if (timeMs <= frames.first().toMap().value("timeMs").toLongLong())
    return frames.first().toMap().value("value");
  if (timeMs >= frames.last().toMap().value("timeMs").toLongLong())
    return frames.last().toMap().value("value");
  for (int i = 1; i < frames.size(); ++i) {
    const QVariantMap a = frames.at(i - 1).toMap();
    const QVariantMap b = frames.at(i).toMap();
    const qint64 at = a.value("timeMs").toLongLong();
    const qint64 bt = b.value("timeMs").toLongLong();
    if (timeMs > bt)
      continue;
    if (a.value("interpolation").toString() == QStringLiteral("hold"))
      return a.value("value");
    const double t = double(timeMs - at) / double(qMax<qint64>(1, bt - at));
    const double av = a.value("value").toDouble();
    const double bv = b.value("value").toDouble();
    return av + (bv - av) * t;
  }
  return frames.last().toMap().value("value");
}

qint64 KeyframeEngine::previousTime(const QString &clipId,
                                    const QString &property,
                                    qint64 timeMs) const {
  qint64 result = -1;
  for (const QVariant &value : keyframesFor(clipId, property)) {
    const qint64 time = value.toMap().value("timeMs").toLongLong();
    if (time < timeMs)
      result = time;
  }
  return result;
}

qint64 KeyframeEngine::nextTime(const QString &clipId,
                                const QString &property,
                                qint64 timeMs) const {
  for (const QVariant &value : keyframesFor(clipId, property)) {
    const qint64 time = value.toMap().value("timeMs").toLongLong();
    if (time > timeMs)
      return time;
  }
  return -1;
}
