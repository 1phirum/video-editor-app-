#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include "core/module_api.h"

// A cap on any count QML is about to turn into that many objects, and a record
// of every time the cap was needed.
//
// The pattern this exists for appears all over this UI: a Repeater whose model
// is derived from a duration and a zoom level.
//
//     readonly property int tickCount:
//         Math.floor((startMs + spanMs - firstTick) / step) + 1
//     Repeater { model: root.tickCount }
//     Repeater { model: root.tickCount * 4 }
//
// That is correct for a ten second clip and catastrophic for a five hour
// sequence: nothing in the expression is bounded by the width it is drawn in, so
// the delegate count follows the media rather than the viewport. Writing such a
// model also does not merely allocate - QQuickRepeater::setModel calls clear(),
// which makes QQmlDelegateModel::cancel force-complete every pending
// asynchronous incubation synchronously on the GUI thread. One wide model is
// therefore a frozen window, not a slow one.
//
// Two jobs, in one call:
//
//   Repeater { model: ModelGuard.bound(root.tickCount, 4096, "ruler.major") }
//
//  * it clamps, so the pathological case degrades into a wrong-looking ruler
//    instead of a hung application. A user can report the former;
//  * it records, so the *next* freeze arrives with a line naming the key, the
//    number it asked for and the number it got. The alternative - which this
//    project has now done several times - is reading QML afterwards and picking
//    the most plausible suspect.
//
// Keys are free-form and should read as file.role ("ruler.major",
// "keyframes.lane"). They are what the log line and the crash report print.
//
// Registered from main() with qmlRegisterSingletonInstance, like Backend, so it
// is reachable as `ModelGuard.bound(...)` after `import CutPro 1.0`.
class CUTPRO_DIAGNOSTICS_API ModelGuard final : public QObject {
  Q_OBJECT

public:
  explicit ModelGuard(QObject *parent = nullptr);

  static ModelGuard &instance();

  // Returns min(wanted, cap), recording the request under `key`. Negative or
  // non-finite wants come back as 0: a NaN reaching a Repeater's model is its
  // own kind of freeze, and a binding that divides by a zoom level of zero
  // produces one.
  Q_INVOKABLE int bound(int wanted, int cap, const QString &key);

  // Records without clamping, for counts that are already bounded by something
  // real but are worth watching. Returns `count` unchanged.
  Q_INVOKABLE int note(const QString &key, int count);

  // Everything recorded, worst first, as text. Goes into the crash report.
  Q_INVOKABLE QString report() const;
  Q_INVOKABLE QVariantMap statistics() const;
  Q_INVOKABLE void reset();

  // A clamp that is this many times the cap is logged even after the first
  // report for that key, because an order of magnitude is a different bug.
  static constexpr int kEscalationFactor = 8;

private:
  struct Record {
    int peakWanted = 0;
    int lastWanted = 0;
    int cap = 0;
    int clamps = 0;
    int calls = 0;
    bool reported = false;
    int reportedAt = 0;
  };

  // Guarded by nothing: every caller is a QML binding on the GUI thread, and a
  // mutex on a path evaluated once per zoom step would cost more than the
  // record it protects. statistics() is called from the same thread.
  QHash<QString, Record> m_records;
  int m_totalClamps = 0;
};
