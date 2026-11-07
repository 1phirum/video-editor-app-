#pragma once

#include <QJSEngine>
#include <QLoggingCategory>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include "core/module_api.h"

Q_DECLARE_LOGGING_CATEGORY(playbackTraceLog)

// Why this exists: the monitor's picture and its playhead disagreed, and the
// existing logs could say *that* a playback session restarted but never *who*
// restarted it. Backend::playing is a QML-writable property with eight writers
// spread over three panels, and by the time setPlaying() runs the QML frame that
// wrote it is gone from every C++ backtrace - the write arrives through the
// metaobject system, so GuiStallTracer shows QQmlPropertyData::writeProperty and
// stops there.
//
// PlaybackTrace closes that gap. It asks the QML engine itself for the
// JavaScript stack at the moment of the write, which names the .qml file and
// line of the real caller, and it keeps every playback-relevant transition in
// one ordered, delta-timestamped stream so a restart storm can be read as cause
// and effect instead of being reconstructed from three log categories.
//
// It is deliberately in cutpro_diagnostics: every subsystem may report into it,
// and it reaches into none of them.
class CUTPRO_DIAGNOSTICS_API PlaybackTrace {
public:
  static PlaybackTrace &instance();

  // Off unless CUTPRO_PLAYBACK_TRACE is set to something other than 0, so a
  // normal run pays one atomic load per event and nothing else. Read once and
  // cached; changing the variable mid-run does nothing.
  static bool enabled();

  // The engine whose JavaScript stack is worth asking for. Called once from the
  // launcher after the engine exists. Only used from the engine's own thread.
  void attachEngine(QJSEngine *engine);

  // One line in the stream. `event` is a short stable token ("playing",
  // "session.start"), `detail` the values that moved. Safe from any thread; the
  // QML call site is only appended when called from the engine's thread, since
  // evaluating JavaScript anywhere else would be undefined behaviour.
  void record(const char *event, const QString &detail = QString());

  // A frame reached the screen. Separate from record() because the decode thread
  // calls it, it must not evaluate JavaScript, and it feeds the drift figure.
  void recordPresentedFrame(qint64 sourceMs);

  // Playhead position versus the position of the picture actually on screen.
  // Positive means the playhead is ahead of the image - the symptom this whole
  // file was written for, reported as a number instead of a feeling.
  void recordDrift(qint64 playheadMs, qint64 presentedTimelineMs);

  // The last `maxEntries` lines, oldest first, for the Ctrl+Shift+D snapshot and
  // for the crash report. Safe from any thread.
  QStringList history(int maxEntries = 128) const;

private:
  PlaybackTrace() = default;

  // "MonitorPanel.qml:684 <- TimelinePanel.qml:171", or an empty string when
  // there is no engine, no JavaScript frame on the stack (a C++-initiated
  // write), or this is not the engine's thread.
  QString qmlCallSite();

  void append(const QString &line);

  QJSEngine *m_engine = nullptr;
  // The engine's thread, captured at attach time. Comparing against it is what
  // makes record() safe to call from the decode and audio threads.
  Qt::HANDLE m_engineThread = nullptr;

  mutable QMutex m_mutex;
  QStringList m_history;
  qint64 m_lastEventNs = -1;

  // Last frame handed to the screen, for recordDrift() and for the "since last
  // frame" figure on a session teardown.
  qint64 m_presentedSourceMs = -1;
  qint64 m_presentedAtNs = -1;

  // Drift is sampled on a UI tick, so it would otherwise emit fifteen lines a
  // second saying the same thing. Only a change worth seeing is logged.
  qint64 m_lastDriftLogged = 0;
  qint64 m_lastDriftLoggedAtNs = -1;
};

// Report a playback transition with its QML caller. A macro rather than a call
// so the argument expressions - which are usually QString concatenations - are
// not built at all on a normal run.
#define CUTPRO_PLAYBACK_TRACE(event, detail)                                   \
  do {                                                                         \
    if (PlaybackTrace::enabled())                                              \
      PlaybackTrace::instance().record(event, detail);                          \
  } while (false)
