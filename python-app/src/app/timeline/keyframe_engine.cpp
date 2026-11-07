#include "app/timeline/keyframe_engine.h"

#include "core/easing.h"

#include <algorithm>
#include <cmath>


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
    // Ease is a property of the segment: the departure influence belongs to the
    // keyframe being left, the arrival influence to the one being reached.
    const double eased = core::cubicBezierEase(
        t, a.value("easeOut").toDouble(), b.value("easeIn").toDouble());
    return av + (bv - av) * eased;
  }
  return frames.last().toMap().value("value");
}

double KeyframeEngine::valueAt(const QString &clipId, const QString &property,
                               qint64 timeMs, double fallback) const {
  // Cheap fast path: an un-keyframed channel is the overwhelmingly common case
  // (every parameter on every clip until the stopwatch is switched on), and it
  // must cost nothing extra on the per-frame render path.
  const QVariantList frames = keyframesFor(clipId, property);
  if (frames.isEmpty())
    return fallback;
  const QVariant value = interpolatedValue(clipId, property, timeMs);
  bool ok = false;
  const double result = value.toDouble(&ok);
  return ok ? result : fallback;
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

QString KeyframeEngine::instanceChannel(const QString &instanceId,
                                        const QString &parameterId) const {
  if (instanceId.isEmpty())
    return parameterId;
  return QStringLiteral("fx:%1:%2").arg(instanceId, parameterId);
}

double KeyframeEngine::instanceValueAt(const QString &clipId,
                                       const QString &instanceId,
                                       const QString &parameterId,
                                       qint64 timeMs, double fallback) const {
  return valueAt(clipId, instanceChannel(instanceId, parameterId), timeMs,
                 fallback);
}

QVariantList KeyframeEngine::serialize() const {
  QVariantList result;
  for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
    if (it.value().isEmpty())
      continue;
    const int separator = it.key().indexOf(QChar('\x1f'));
    if (separator <= 0)
      continue;
    result.append(QVariantMap{{"clipId", it.key().left(separator)},
                              {"property", it.key().mid(separator + 1)},
                              {"frames", it.value()}});
  }
  return result;
}

void KeyframeEngine::restore(const QVariantList &channels) {
  m_channels.clear();
  for (const QVariant &value : channels) {
    const QVariantMap channel = value.toMap();
    const QString clipId = channel.value("clipId").toString();
    const QString property = channel.value("property").toString();
    const QVariantList frames = channel.value("frames").toList();
    if (clipId.isEmpty() || property.isEmpty() || frames.isEmpty())
      continue;
    m_channels.insert(key(clipId, property), frames);
  }
}

QVariantMap KeyframeEngine::channelsForClip(const QString &clipId) const {
  QVariantMap result;
  if (clipId.isEmpty())
    return result;
  const QString prefix = clipId + QChar('\x1f');
  for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
    if (it.value().isEmpty() || !it.key().startsWith(prefix))
      continue;
    result.insert(it.key().mid(prefix.size()), it.value());
  }
  return result;
}

void KeyframeEngine::forgetClip(const QString &clipId) {
  if (clipId.isEmpty())
    return;
  const QString prefix = clipId + QChar('\x1f');
  bool removed = false;
  for (const QString &channel : m_channels.keys()) {
    if (channel.startsWith(prefix)) {
      m_channels.remove(channel);
      removed = true;
    }
  }
  if (removed)
    emit keyframesChanged(clipId);
}

bool KeyframeEngine::hasAnyChannel(const QString &clipId) const {
  if (clipId.isEmpty())
    return false;
  const QString prefix = clipId + QChar('\x1f');
  for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
    if (!it.value().isEmpty() && it.key().startsWith(prefix))
      return true;
  }
  return false;
}

void KeyframeEngine::cloneClip(const QString &from, const QString &to,
                               qint64 shiftMs) {
  if (from.isEmpty() || to.isEmpty() || from == to)
    return;
  const QString prefix = from + QChar('\x1f');
  bool added = false;
  // Snapshot the source keys first: inserting into m_channels while walking it
  // would rehash the table under the iterator.
  const QList<QString> sourceKeys = m_channels.keys();
  for (const QString &channel : sourceKeys) {
    if (!channel.startsWith(prefix))
      continue;
    const QVariantList frames = m_channels.value(channel);
    if (frames.isEmpty())
      continue;
    QVariantList shifted;
    shifted.reserve(frames.size());
    for (const QVariant &frame : frames) {
      QVariantMap item = frame.toMap();
      const qint64 t = item.value("timeMs").toLongLong() + shiftMs;
      if (t < 0)
        continue;
      item["timeMs"] = t;
      shifted.append(item);
    }
    if (shifted.isEmpty())
      continue;
    m_channels.insert(key(to, channel.mid(prefix.size())), shifted);
    added = true;
  }
  if (added)
    emit keyframesChanged(to);
}

void KeyframeEngine::clampClip(const QString &clipId, qint64 fromMs,
                               qint64 toMs, qint64 shiftMs) {
  if (clipId.isEmpty() || toMs < fromMs)
    return;
  const QString prefix = clipId + QChar('\x1f');
  bool changed = false;
  const QList<QString> channels = m_channels.keys();
  for (const QString &channel : channels) {
    if (!channel.startsWith(prefix))
      continue;
    const QVariantList frames = m_channels.value(channel);
    QVariantList kept;
    kept.reserve(frames.size());
    for (const QVariant &frame : frames) {
      QVariantMap item = frame.toMap();
      const qint64 t = item.value("timeMs").toLongLong();
      if (t < fromMs || t > toMs)
        continue;
      const qint64 moved = t + shiftMs;
      if (moved < 0)
        continue;
      item["timeMs"] = moved;
      kept.append(item);
    }
    if (kept.size() == frames.size() && shiftMs == 0)
      continue;
    if (kept.isEmpty())
      m_channels.remove(channel);
    else
      m_channels.insert(channel, kept);
    changed = true;
  }
  if (changed)
    emit keyframesChanged(clipId);
}

void KeyframeEngine::shiftClip(const QString &clipId, qint64 shiftMs) {
  if (clipId.isEmpty() || shiftMs == 0)
    return;
  const QString prefix = clipId + QChar('\x1f');
  bool changed = false;
  const QList<QString> channels = m_channels.keys();
  for (const QString &channel : channels) {
    if (!channel.startsWith(prefix))
      continue;
    QVariantList frames = m_channels.value(channel);
    for (QVariant &frame : frames) {
      QVariantMap item = frame.toMap();
      item["timeMs"] =
          qMax<qint64>(0, item.value("timeMs").toLongLong() + shiftMs);
      frame = item;
    }
    m_channels.insert(channel, frames);
    changed = true;
  }
  if (changed)
    emit keyframesChanged(clipId);
}

bool KeyframeEngine::setInterpolation(const QString &clipId,
                                      const QString &property, qint64 timeMs,
                                      const QString &mode, double easeIn,
                                      double easeOut) {
  const QString channel = key(clipId, property);
  auto it = m_channels.find(channel);
  if (it == m_channels.end())
    return false;
  QVariantList &frames = it.value();
  for (QVariant &frame : frames) {
    QVariantMap item = frame.toMap();
    if (item.value("timeMs").toLongLong() != timeMs)
      continue;
    const QString normalized =
        (mode == QStringLiteral("hold") || mode == QStringLiteral("bezier") ||
         mode == QStringLiteral("linear"))
            ? mode
            : QStringLiteral("linear");
    item["interpolation"] = normalized;
    // A linear keyframe carries no ease, so switching back to it clears the
    // influences instead of leaving them to shape a curve the UI calls straight.
    item["easeIn"] = normalized == QStringLiteral("linear")
                         ? 0.0
                         : std::clamp(easeIn, 0.0, 1.0);
    item["easeOut"] = normalized == QStringLiteral("linear")
                          ? 0.0
                          : std::clamp(easeOut, 0.0, 1.0);
    frame = item;
    emit keyframesChanged(clipId);
    return true;
  }
  return false;
}

QVariantMap KeyframeEngine::keyframeAt(const QString &clipId,
                                       const QString &property,
                                       qint64 timeMs) const {
  const QVariantList frames = keyframesFor(clipId, property);
  for (const QVariant &frame : frames) {
    const QVariantMap item = frame.toMap();
    if (item.value("timeMs").toLongLong() == timeMs)
      return item;
  }
  return {};
}
