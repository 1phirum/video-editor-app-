#include "app/diagnostics/item_tree_census.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWindow>

#include <algorithm>

#include "app/diagnostics/crash_channel.h"

namespace {

// QML types arrive as "EffectTimeRuler_QMLTYPE_88" or
// "TimelinePanel_QMLTYPE_12". The suffix is a per-engine registration index -
// it changes between runs and carries no information - but the prefix is the
// .qml file name, which is the whole point of reading this list.
QString readableClassName(const QObject *object) {
  if (!object)
    return QStringLiteral("(null)");
  QString name = QString::fromLatin1(object->metaObject()->className());
  const int marker = name.indexOf(QLatin1String("_QMLTYPE_"));
  if (marker > 0)
    name.truncate(marker);
  // QQmlComponent-created anonymous types show up as "QQuickItem" already, so
  // there is nothing to strip for them.
  return name;
}

QString pathOf(const QObject *object, int maxLevels = 12) {
  QStringList parts;
  const QObject *walk = object;
  while (walk && parts.size() < maxLevels) {
    QString part = readableClassName(walk);
    if (!walk->objectName().isEmpty())
      part += QLatin1Char('#') + walk->objectName();
    parts.prepend(part);
    walk = walk->parent();
  }
  if (walk)
    parts.prepend(QStringLiteral("..."));
  return parts.join(QLatin1String(" > "));
}

struct SamplerState {
  QTimer *timer = nullptr;
  QQmlApplicationEngine *engine = nullptr;
  ItemTreeCensus::Result last;
  int samples = 0;
  int alarms = 0;
  int peakItems = 0;
};

SamplerState &sampler() {
  static SamplerState s;
  return s;
}

} // namespace

QString ItemTreeCensus::Result::text() const {
  QStringList lines;
  lines << QStringLiteral("items %1%2  depth %3  walk %4us")
               .arg(items)
               .arg(truncated ? QStringLiteral("+ (budget hit)") : QString())
               .arg(maxDepth)
               .arg(elapsedUs);
  if (worstParentChildren > 0)
    lines << QStringLiteral("widest parent: %1 children of %2")
                 .arg(worstParentChildren)
                 .arg(worstParentPath);
  for (const Entry &entry : byClass) {
    lines << QStringLiteral("  %1  %2").arg(entry.count, 7).arg(entry.className);
    if (!entry.samplePath.isEmpty())
      lines << QStringLiteral("          at %1").arg(entry.samplePath);
  }
  return lines.join(QLatin1Char('\n'));
}

ItemTreeCensus::Result ItemTreeCensus::walk(QObject *root) {
  Result result;
  if (!root)
    return result;

  QElapsedTimer clock;
  clock.start();

  QHash<QString, int> counts;
  // One representative per class, kept for the ancestor chain. Storing the
  // pointer rather than the path is what keeps the walk cheap: only a handful
  // of chains are ever built, at the end, for the classes that turned out to
  // matter.
  QHash<QString, QObject *> representative;

  struct Frame {
    QObject *object;
    int depth;
  };
  QList<Frame> stack;
  stack.append({root, 0});
  // A delegate is reachable twice: as a QObject child of the Repeater that made
  // it and as a visual child of the item it was parented into. Both edges have
  // to be followed - see below - so the set is what stops every delegate being
  // counted twice and the histogram reading double.
  QSet<const QObject *> seen;
  seen.reserve(4096);

  QObject *worstParent = nullptr;
  int worstChildren = 0;

  while (!stack.isEmpty()) {
    const Frame frame = stack.takeLast();
    QObject *object = frame.object;
    if (!object || seen.contains(object))
      continue;
    seen.insert(object);

    ++result.items;
    result.maxDepth = std::max(result.maxDepth, frame.depth);
    const QString name = readableClassName(object);
    if (++counts[name] == 1)
      representative.insert(name, object);

    if (result.items >= kItemBudget) {
      result.truncated = true;
      break;
    }

    // Both child sets. A Repeater's delegates are QObject children of the
    // delegate machinery and QQuickItem children of the Repeater's parent;
    // following only one of the two either misses the delegates or misses the
    // non-visual objects a delegate also creates (Timer, Connections, model
    // objects) - and those are memory too.
    int childCount = 0;
    if (QQuickItem *item = qobject_cast<QQuickItem *>(object)) {
      const QList<QQuickItem *> kids = item->childItems();
      for (QQuickItem *kid : kids) {
        if (!kid || seen.contains(kid))
          continue;
        ++childCount;
        stack.append({kid, frame.depth + 1});
      }
    } else if (QQuickWindow *window = qobject_cast<QQuickWindow *>(object)) {
      if (QQuickItem *content = window->contentItem())
        stack.append({content, frame.depth + 1});
    }
    const QObjectList kids = object->children();
    for (QObject *kid : kids) {
      if (!kid || seen.contains(kid))
        continue;
      ++childCount;
      stack.append({kid, frame.depth + 1});
    }

    if (childCount > worstChildren) {
      worstChildren = childCount;
      worstParent = object;
    }
  }

  QList<Entry> entries;
  entries.reserve(counts.size());
  for (auto it = counts.cbegin(); it != counts.cend(); ++it)
    entries.append({it.key(), it.value(), QString()});
  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b) { return a.count > b.count; });
  if (entries.size() > kReportedClasses)
    entries.resize(kReportedClasses);
  for (int i = 0; i < entries.size() && i < kPathedClasses; ++i) {
    QObject *sample = representative.value(entries[i].className);
    if (sample)
      entries[i].samplePath = pathOf(sample);
  }
  result.byClass = entries;
  if (worstParent) {
    result.worstParentChildren = worstChildren;
    result.worstParentPath = pathOf(worstParent);
  }
  result.elapsedUs = clock.nsecsElapsed() / 1000;
  return result;
}

ItemTreeCensus::Result ItemTreeCensus::sampleNow() {
  SamplerState &s = sampler();
  Result merged;
  QElapsedTimer clock;
  clock.start();

  QHash<QString, Entry> combined;
  const QList<QObject *> roots =
      s.engine ? s.engine->rootObjects() : QList<QObject *>();
  for (QObject *root : roots) {
    Result one = walk(root);
    merged.items += one.items;
    merged.maxDepth = std::max(merged.maxDepth, one.maxDepth);
    merged.truncated = merged.truncated || one.truncated;
    if (one.worstParentChildren > merged.worstParentChildren) {
      merged.worstParentChildren = one.worstParentChildren;
      merged.worstParentPath = one.worstParentPath;
    }
    for (const Entry &entry : one.byClass) {
      Entry &slot = combined[entry.className];
      slot.className = entry.className;
      slot.count += entry.count;
      if (slot.samplePath.isEmpty())
        slot.samplePath = entry.samplePath;
    }
  }
  QList<Entry> entries = combined.values();
  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b) { return a.count > b.count; });
  if (entries.size() > kReportedClasses)
    entries.resize(kReportedClasses);
  merged.byClass = entries;
  merged.elapsedUs = clock.nsecsElapsed() / 1000;

  const int previous = s.last.items;
  s.last = merged;
  ++s.samples;
  s.peakItems = std::max(s.peakItems, merged.items);

  diag::CrashChannel::setCensus(merged.text(), merged.items, merged.truncated);

  // The alarm is the difference between "it froze again" and a log line naming
  // the type that multiplied. Deliberately noisy: a jump of twenty thousand
  // items between two samples two seconds apart is never normal in an editor
  // that is idle or being panned.
  if (previous > 0 && merged.items - previous >= kGrowthAlarmItems) {
    ++s.alarms;
    qWarning().noquote() << "ItemTreeCensus: scene grew by"
                         << (merged.items - previous) << "items in one sample ("
                         << previous << "->" << merged.items << ")\n"
                         << merged.text();
  }
  return merged;
}

void ItemTreeCensus::startSampling(QQmlApplicationEngine *engine,
                                   int intervalMs) {
  SamplerState &s = sampler();
  s.engine = engine;
  if (!s.timer) {
    s.timer = new QTimer(engine);
    s.timer->setTimerType(Qt::VeryCoarseTimer);
    QObject::connect(s.timer, &QTimer::timeout, engine,
                     []() { ItemTreeCensus::sampleNow(); });
  }
  s.timer->start(intervalMs > 0 ? intervalMs : kDefaultIntervalMs);
}

void ItemTreeCensus::stopSampling() {
  if (QTimer *timer = sampler().timer)
    timer->stop();
}

ItemTreeCensus::Result ItemTreeCensus::last() { return sampler().last; }

QVariantMap ItemTreeCensus::statistics() {
  const SamplerState &s = sampler();
  QVariantMap map;
  map.insert(QStringLiteral("censusSamples"), s.samples);
  map.insert(QStringLiteral("censusItems"), s.last.items);
  map.insert(QStringLiteral("censusPeakItems"), s.peakItems);
  map.insert(QStringLiteral("censusGrowthAlarms"), s.alarms);
  map.insert(QStringLiteral("censusWalkUs"),
             static_cast<qint64>(s.last.elapsedUs));
  map.insert(QStringLiteral("censusTruncated"), s.last.truncated);
  if (!s.last.byClass.isEmpty()) {
    map.insert(QStringLiteral("censusTopClass"), s.last.byClass.first().className);
    map.insert(QStringLiteral("censusTopCount"), s.last.byClass.first().count);
  }
  map.insert(QStringLiteral("censusWidestParent"), s.last.worstParentPath);
  map.insert(QStringLiteral("censusWidestChildren"), s.last.worstParentChildren);
  return map;
}
