#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "core/module_api.h"

// The step the diagnostics layer was missing: turning what was measured into
// what it means.
//
// Everything underneath already produced numbers - the watchdog's stall
// durations, the census item counts, the model guard's clamps, the playback
// trace's event stream - and reading them still required knowing which
// combination of numbers is a bug. "9824 scene items" is fine in one project and
// a Repeater rebuild disaster in another; "worst stall 1825 ms, scope -" is
// unattributed launch cost if it happened before the window existed and a
// freeze if it did not.
//
// DiagnosticAnalyzer holds those judgements as rules, each one derived from a
// defect this project actually shipped, so the overlay and the snapshot both say
// "this is wrong, here is the evidence, here is what to do" instead of printing
// a table and leaving the reader to remember the thresholds.
//
// Deliberately pure: it takes the statistics map and the playback history, and
// touches no subsystem. That is what lets the crash reporter run the same rules
// over a channel dump from a process that is already dead.
class CUTPRO_DIAGNOSTICS_API DiagnosticAnalyzer {
public:
  enum class Severity {
    // Worth knowing, nothing to do: a measurement that confirms a subsystem is
    // behaving, or a cost that is real but outside this codebase.
    Info,
    // A real defect, or a number that will become one as the project grows.
    Warning,
    // Either broken now, or a diagnostic capability that is silently absent -
    // which is worse than a bug, because it hides the next one.
    Critical,
  };

  struct Finding {
    Severity severity = Severity::Info;
    // Stable token ("gui.stall.severe"), so a finding can be referred to in a
    // commit message and grepped for in a snapshot.
    QString id;
    QString title;
    // The numbers this rests on, quoted rather than summarised. A finding whose
    // evidence cannot be checked is an opinion.
    QString evidence;
    // What it implies for the running app, in one sentence.
    QString meaning;
    // The next concrete step. Empty when there is nothing to do.
    QString action;
  };

  // `stats` is DiagnosticsBridge::statistics(); `playbackHistory` is
  // PlaybackTrace::history(). Both may be empty - a finding is produced saying
  // so, because an empty trace usually means the env var was not set rather than
  // that playback is healthy.
  static QList<Finding> analyze(const QVariantMap &stats,
                                const QStringList &playbackHistory);

  // The same findings as QML sees them: one map per finding with `severity` as a
  // lowercase string plus the four text fields.
  static QVariantList asVariantList(const QList<Finding> &findings);

  // One line summing the run up: "2 critical, 3 warnings" or "nothing wrong
  // found". Shown as the report's headline so a glance is enough.
  static QString verdict(const QList<Finding> &findings);

  // The whole diagnosis as plain text, for the snapshot file and the crash
  // report. Ordered worst first, same as the window.
  static QString text(const QList<Finding> &findings);

  static QString severityName(Severity severity);
};
