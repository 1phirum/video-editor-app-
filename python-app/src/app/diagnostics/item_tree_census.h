#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

class QObject;
class QQmlApplicationEngine;

// Counts what is actually in the scene graph, by type, on a timer.
//
// This exists because of a specific dead end. A freeze was traced - with gdb, on
// a live hung process - to QQuickRepeater::setModel > clear() >
// QQmlDelegateModel::cancel force-completing every pending asynchronous
// incubation on the GUI thread, with the process at 2.1 GB and climbing. That
// names the *mechanism* exactly and the *culprit* not at all: the app has
// dozens of Repeaters, and reading QML to guess which one's model expression can
// reach six figures is precisely the guessing this project keeps paying for.
//
// A histogram of the live item tree answers it directly. If one delegate type
// has 300,000 instances, that is the Repeater, no reading required - and the
// ancestor chain of a representative instance names the .qml file it came from.
//
// Three design points:
//
//  * it samples on a timer rather than only at the moment of the freeze. The
//    tree cannot be walked while the GUI thread is wedged - the walk runs on
//    that thread - so the useful sample is the one taken a second or two
//    *before*. Memory in the observed freeze climbed over minutes, so a 2 s
//    cadence catches the growth in progress;
//  * every sample is published into the crash channel, so a reporter attached
//    to a hung or crashed process can read the last one without the app doing
//    anything;
//  * a sample that grows abruptly is logged at once. That turns "it froze
//    again" into a line naming the type that multiplied, in the ordinary log,
//    with no debugger attached.
//
// Cost: one tree walk per interval. A healthy Cut Pro window is a few thousand
// items, which is tens of microseconds. The walk is bounded by kItemBudget so a
// tree that is already pathological cannot make the sampler part of the problem.
class ItemTreeCensus final {
public:
  struct Entry {
    QString className;
    int count = 0;
    // Ancestor chain of one instance, outermost first. The evidence that names
    // the QML file; only filled for the few busiest types, because building it
    // costs a walk back up the tree.
    QString samplePath;
  };

  struct Result {
    int items = 0;
    int maxDepth = 0;
    // Hit kItemBudget: the counts are a floor, not a total.
    bool truncated = false;
    // Descending by count, at most kReportedClasses entries.
    QList<Entry> byClass;
    // The single item with the most direct children, which is what a runaway
    // Repeater's parent looks like from here.
    QString worstParentPath;
    int worstParentChildren = 0;
    long long elapsedUs = 0;

    bool isEmpty() const { return items == 0; }
    // Compact, newline separated, latin-1 safe. This is what goes into the
    // crash channel and into the crash report file.
    QString text() const;
  };

  // Walks `root` and everything below it. `root` may be a QQuickWindow, a
  // QQuickItem, or any QObject - children are followed through both
  // QQuickItem::childItems() and QObject::children(), because a Repeater's
  // delegates are QObject children of the Repeater but QQuickItem children of
  // its parent, and missing either would undercount.
  static Result walk(QObject *root);

  // Starts the repeating sample. Must be called on the GUI thread, after the
  // engine has root objects. Calling twice restarts the timer with the new
  // interval.
  //
  // An interval of 0 or less registers the engine for on-demand sampling and
  // starts no timer. That is the default now, and it is a performance fix rather
  // than a preference: the walk visits every QObject and every QQuickItem in the
  // scene on the GUI thread, and this app's window measured 9801 items / 38 ms
  // per walk. Repeating that every 2 s drops two frames of a 30 fps preview
  // twice a second, forever - the instrument was a measurable part of the
  // slowness it was installed to find. Set CUTPRO_CENSUS_MS to bring the timer
  // back for a session that is hunting scene growth.
  static void startSampling(QQmlApplicationEngine *engine, int intervalMs);
  static void stopSampling();

  // Walks now and publishes. Safe to call from QML.
  static Result sampleNow();
  static Result last();

  // For the debug overlay and for Backend::previewDecodeStatistics.
  static QVariantMap statistics();

  // A window with more items than this is already broken; stop walking rather
  // than spend a frame counting them.
  static constexpr int kItemBudget = 400000;
  static constexpr int kReportedClasses = 24;
  // Ancestor chains are built for this many of the busiest types.
  static constexpr int kPathedClasses = 4;
  // A sample this much bigger than the previous one is logged immediately.
  static constexpr int kGrowthAlarmItems = 20000;
  static constexpr int kDefaultIntervalMs = 2000;

private:
  ItemTreeCensus() = delete;
};
