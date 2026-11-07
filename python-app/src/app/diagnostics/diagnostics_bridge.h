#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "core/module_api.h"

class QQmlApplicationEngine;

// One QML-reachable handle on everything the diagnostics layer knows.
//
// The pieces underneath are deliberately independent - the watchdog measures
// responsiveness, ItemTreeCensus counts the scene, ModelGuard caps and records
// delegate counts, CrashReporterHost owns the out-of-process reporter - and
// wiring each of them into QML separately would mean four singletons and four
// registrations. This is the one seam the UI needs.
//
// It also carries the two calls that make the reporter testable:
// forceHang() and forceCrash(). A crash reporter that has never been made to
// fire is a crash reporter that does not work, and waiting for a real freeze to
// find out is how this project lost several rounds already.
class CUTPRO_SCENE_API DiagnosticsBridge final : public QObject {
  Q_OBJECT
  // Everything at once, for a debug overlay. Refreshed on demand rather than
  // on a timer: reading it walks nothing, but a property that notifies every
  // two seconds would re-evaluate every binding that touches it.
  Q_PROPERTY(QVariantMap statistics READ statistics NOTIFY statisticsChanged)

public:
  explicit DiagnosticsBridge(QObject *parent = nullptr);

  // Starts the item census against `engine`'s root objects. Called from main().
  void attach(QQmlApplicationEngine *engine, int censusIntervalMs);

  QVariantMap statistics() const;

  // Walks the scene now and returns the fresh numbers.
  Q_INVOKABLE QVariantMap sampleCensus();
  // The census as the crash report prints it.
  Q_INVOKABLE QString censusText() const;
  // The guarded-model table, worst first.
  Q_INVOKABLE QString modelReport() const;
  // The ordered playback event stream: every play/pause, every decode session,
  // every playhead jump and every playhead-versus-picture gap, each with the QML
  // caller that caused it. Empty unless CUTPRO_PLAYBACK_TRACE is set.
  Q_INVOKABLE QString playbackTrace(int maxEntries = 128) const;

  // --- the expert diagnosis ------------------------------------------------
  // The rules in DiagnosticAnalyzer applied to everything measured right now.
  // One map per finding: severity ("critical"/"warning"/"info"), id, title,
  // evidence, meaning, action. Worst first.
  Q_INVOKABLE QVariantList findings() const;
  // One line: "2 critical, 3 warnings", or that nothing was found.
  Q_INVOKABLE QString verdict() const;
  // The same diagnosis as plain text, for copying out of the report window.
  Q_INVOKABLE QString diagnosisText() const;
  // The crash channel as the reporter would read it.
  Q_INVOKABLE QString crashChannelText() const;
  // Everything - diagnosis, statistics, census, models, playback trace, channel
  // - as one text block, identical to what writeSnapshot() puts in the file.
  Q_INVOKABLE QString fullReport(const QString &note = QString()) const;
  // Writes everything known right now to a file in the crash-report directory
  // and returns its path. This is the "capture evidence while it is happening"
  // button: no freeze, no debugger, no crash required.
  Q_INVOKABLE QString writeSnapshot(const QString &note = QString());
  Q_INVOKABLE QStringList reports(int limit = 20) const;
  Q_INVOKABLE QString reportDirectory() const;
  // Prints the same text to the console and writes it to the file, and returns
  // the path. What Ctrl+Shift+D calls now that there is no report window: the
  // console is where it gets read while the app is running, the file is what
  // survives the process.
  Q_INVOKABLE QString printReport(const QString &note = QString());

  // --- reporter self-test ------------------------------------------------
  // Blocks the GUI thread for `ms`. The reporter should notice at its hang
  // threshold and leave a hang report plus a minidump naming this call.
  Q_INVOKABLE void forceHang(int ms);
  // Raises an access violation. The last-chance filter fills the channel, the
  // reporter writes a crash dump, and the process dies. Never call this from
  // anything a user can reach.
  Q_INVOKABLE void forceCrash();

Q_SIGNALS:
  void statisticsChanged();

private:
  QQmlApplicationEngine *m_engine = nullptr;
};
