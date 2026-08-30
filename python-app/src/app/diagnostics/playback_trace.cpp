#include "app/diagnostics/playback_trace.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QThread>

Q_LOGGING_CATEGORY(playbackTraceLog, "cutpro.playback.trace")

namespace {

// One clock for the whole stream, so every line's delta is comparable. Started
// on first use rather than at load time: a delta measured from static
// initialisation would be dominated by process startup.
QElapsedTimer &traceClock() {
  static QElapsedTimer timer;
  if (!timer.isValid())
    timer.start();
  return timer;
}

constexpr int kMaxHistory = 512;

// A JavaScript stack frame from QJSEngine looks like
//   expression for onPlayingChanged@file:///C:/.../MonitorPanel.qml:684
// and the file part is the only useful half at a glance. Keep the function name
// when it is a handler, because "onPlayingChanged" is the answer to "who wrote
// this" more often than the line number is.
QString compactFrame(const QString &frame) {
  const int at = frame.lastIndexOf(QLatin1Char('@'));
  QString function = at > 0 ? frame.left(at) : QString();
  QString location = at >= 0 ? frame.mid(at + 1) : frame;
  const int slash = location.lastIndexOf(QLatin1Char('/'));
  if (slash >= 0)
    location = location.mid(slash + 1);
  function.remove(QStringLiteral("expression for "));
  function = function.trimmed();
  if (function.isEmpty() || function == QStringLiteral("%entry"))
    return location;
  return location + QStringLiteral(" (") + function + QStringLiteral(")");
}

} // namespace

PlaybackTrace &PlaybackTrace::instance() {
  static PlaybackTrace trace;
  return trace;
}

bool PlaybackTrace::enabled() {
  static const bool on = [] {
    const QByteArray value = qgetenv("CUTPRO_PLAYBACK_TRACE");
    return !value.isEmpty() && value != "0";
  }();
  return on;
}

void PlaybackTrace::attachEngine(QJSEngine *engine) {
  QMutexLocker lock(&m_mutex);
  m_engine = engine;
  m_engineThread = engine ? QThread::currentThreadId() : nullptr;
}

QString PlaybackTrace::qmlCallSite() {
  QJSEngine *engine = nullptr;
  {
    QMutexLocker lock(&m_mutex);
    // Evaluating JavaScript off the engine's thread corrupts the engine, and a
    // trace that crashes the app it is explaining is worse than no trace.
    if (!m_engine || QThread::currentThreadId() != m_engineThread)
      return QString();
    engine = m_engine;
  }

  // There is no public API for "the QML stack right now", but the engine will
  // build one for an Error object, and that is exactly the JavaScript stack of
  // the frame that reached this C++ setter. Cheap enough for an event that
  // happens on user input; this is why the whole file is behind an env var.
  const QJSValue stack = engine->evaluate(QStringLiteral("new Error().stack"));
  if (!stack.isString())
    return QString();

  const QStringList frames = stack.toString().split(QLatin1Char('\n'),
                                                    Qt::SkipEmptyParts);
  QStringList useful;
  for (const QString &frame : frames) {
    const QString trimmed = frame.trimmed();
    // The evaluate() call above is the top frame and says nothing.
    if (trimmed.isEmpty() || trimmed.contains(QStringLiteral("<expression>")) ||
        trimmed.startsWith(QStringLiteral("%entry")))
      continue;
    useful << compactFrame(trimmed);
    if (useful.size() >= 4)
      break;
  }
  return useful.join(QStringLiteral(" <- "));
}

void PlaybackTrace::append(const QString &line) {
  QMutexLocker lock(&m_mutex);
  m_history << line;
  while (m_history.size() > kMaxHistory)
    m_history.removeFirst();
}

void PlaybackTrace::record(const char *event, const QString &detail) {
  if (!enabled())
    return;

  const qint64 now = traceClock().nsecsElapsed();
  qint64 sinceLast = -1;
  {
    QMutexLocker lock(&m_mutex);
    if (m_lastEventNs >= 0)
      sinceLast = (now - m_lastEventNs) / 1000000;
    m_lastEventNs = now;
  }

  const QString site = qmlCallSite();
  QString line = QStringLiteral("%1 %2").arg(QString::fromLatin1(event), detail);
  if (sinceLast >= 0)
    line += QStringLiteral(" [+%1 ms]").arg(sinceLast);
  // No JavaScript frame means the write did not come from QML: a C++ caller, or
  // a worker thread. Saying so is worth a word, because it halves the search.
  line += QStringLiteral(" from ") + (site.isEmpty() ? QStringLiteral("C++")
                                                     : site);

  append(line);
  qCInfo(playbackTraceLog).noquote() << "playback:" << line;
}

void PlaybackTrace::recordPresentedFrame(qint64 sourceMs) {
  if (!enabled())
    return;
  const qint64 now = traceClock().nsecsElapsed();
  qint64 sincePrevious = -1;
  qint64 previous = -1;
  {
    QMutexLocker lock(&m_mutex);
    if (m_presentedAtNs >= 0)
      sincePrevious = (now - m_presentedAtNs) / 1000000;
    previous = m_presentedSourceMs;
    m_presentedSourceMs = sourceMs;
    m_presentedAtNs = now;
  }

  // Only frames that are not the smooth continuation of the previous one are
  // worth a line. A 24 fps stream advancing ~42 ms per frame every ~42 ms is the
  // expected case and would drown everything else; a frame that arrives late, or
  // whose source position jumps, is the event being hunted.
  const qint64 advance = previous >= 0 ? sourceMs - previous : 0;
  const bool interesting =
      previous < 0 || sincePrevious < 0 || sincePrevious > 120 ||
      advance < 0 || (sincePrevious > 0 && qAbs(advance - sincePrevious) > 80);
  if (!interesting)
    return;

  append(QStringLiteral("frame at source %1 ms (advance %2 ms in %3 ms)")
             .arg(sourceMs)
             .arg(advance)
             .arg(sincePrevious));
  qCInfo(playbackTraceLog).nospace()
      << "playback: frame at source " << sourceMs << " ms, advanced " << advance
      << " ms of media in " << sincePrevious << " ms of wall clock";
}

void PlaybackTrace::recordDrift(qint64 playheadMs, qint64 presentedTimelineMs) {
  if (!enabled())
    return;
  const qint64 drift = playheadMs - presentedTimelineMs;
  const qint64 now = traceClock().nsecsElapsed();
  {
    QMutexLocker lock(&m_mutex);
    // A drift that has not moved by more than a frame, and was reported within
    // the last second, is the same fact as the previous line.
    const bool recent =
        m_lastDriftLoggedAtNs >= 0 && (now - m_lastDriftLoggedAtNs) < 1000000000;
    if (recent && qAbs(drift - m_lastDriftLogged) < 40)
      return;
    m_lastDriftLogged = drift;
    m_lastDriftLoggedAtNs = now;
  }

  append(QStringLiteral("drift %1 ms (playhead %2, picture %3)")
             .arg(drift)
             .arg(playheadMs)
             .arg(presentedTimelineMs));
  qCInfo(playbackTraceLog).nospace()
      << "playback: playhead is " << drift << " ms "
      << (drift >= 0 ? "AHEAD of" : "behind") << " the picture (playhead "
      << playheadMs << " ms, picture " << presentedTimelineMs << " ms)";
}

QStringList PlaybackTrace::history(int maxEntries) const {
  QMutexLocker lock(&m_mutex);
  if (maxEntries <= 0 || m_history.size() <= maxEntries)
    return m_history;
  return m_history.mid(m_history.size() - maxEntries);
}
