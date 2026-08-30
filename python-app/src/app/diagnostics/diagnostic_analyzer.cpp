#include "app/diagnostics/diagnostic_analyzer.h"

#include <QRegularExpression>
#include <algorithm>

namespace {

using Severity = DiagnosticAnalyzer::Severity;
using Finding = DiagnosticAnalyzer::Finding;

qint64 number(const QVariantMap &stats, const char *key, qint64 fallback = 0) {
  const QVariant value = stats.value(QString::fromLatin1(key));
  if (!value.isValid())
    return fallback;
  bool ok = false;
  const qint64 result = value.toLongLong(&ok);
  return ok ? result : fallback;
}

QString string(const QVariantMap &stats, const char *key) {
  return stats.value(QString::fromLatin1(key)).toString().trimmed();
}

bool flag(const QVariantMap &stats, const char *key, bool fallback = false) {
  const QVariant value = stats.value(QString::fromLatin1(key));
  return value.isValid() ? value.toBool() : fallback;
}

// Thresholds, in one place and named, because a bare 1500 in a condition is a
// number nobody can argue with. Each is the point at which this project has
// observed the symptom become visible to a user.
constexpr qint64 kFreezeMs = 1500;      // Windows paints the ghost window
constexpr qint64 kHitchMs = 400;        // a dropped frame the eye catches
constexpr qint64 kBusyItems = 8000;     // a full Repeater rebuild costs ~2 s here
constexpr qint64 kWideParent = 500;     // one parent, hundreds of children
constexpr qint64 kDriftMs = 80;         // two frames of playhead-versus-picture
constexpr int kSessionsPerMinute = 20;  // more Plays than a human presses

void appendGuiFindings(const QVariantMap &stats, QList<Finding> &out) {
  if (!flag(stats, "guiWatchdogRunning", true))
    out.append({Severity::Critical, QStringLiteral("gui.watchdog.off"),
                QStringLiteral("The GUI responsiveness watchdog is not running"),
                QStringLiteral("guiWatchdogRunning = false"),
                QStringLiteral("Nothing is measuring freezes, so the next one "
                               "will be reported as \"the app hung\" and "
                               "nothing else."),
                QStringLiteral("Check GuiThreadWatchdog::start() in main().")});

  const qint64 worst = number(stats, "guiWorstStallMs");
  const QString scope = string(stats, "guiWorstStallScope");
  const QString verdictText = string(stats, "guiWorstStallVerdict");
  const QString chain = string(stats, "guiWorstStallChain");
  const QStringList trace =
      stats.value(QStringLiteral("guiWorstStallTrace")).toStringList();

  if (worst > 0) {
    // A stall before the first window exists is launch cost, not a freeze: there
    // is nothing on screen for Windows to grey out. The watchdog says so in its
    // own verdict, and repeating that as a warning would train the reader to
    // ignore the category.
    const bool preWindow =
        verdictText.contains(QStringLiteral("cannot show as Not Responding"),
                             Qt::CaseInsensitive) ||
        verdictText.contains(QStringLiteral("no window"), Qt::CaseInsensitive);
    QString evidence = QStringLiteral("worst stall %1 ms in scope \"%2\"")
                           .arg(worst)
                           .arg(scope.isEmpty() ? QStringLiteral("(unmarked)")
                                                : scope);
    if (!chain.isEmpty())
      evidence += QStringLiteral(", chain %1").arg(chain);
    if (!trace.isEmpty())
      evidence += QStringLiteral(", top frame %1").arg(trace.first());
    if (!verdictText.isEmpty())
      evidence += QStringLiteral(", verdict \"%1\"").arg(verdictText);

    if (preWindow)
      out.append({Severity::Info, QStringLiteral("gui.stall.launch"),
                  QStringLiteral("The longest stall happened before the window "
                                 "existed"),
                  evidence,
                  QStringLiteral("This is launch cost - loading the graphics "
                                 "driver and compiling QML - not a freeze the "
                                 "user can see."),
                  QString()});
    else if (worst >= kFreezeMs)
      out.append({Severity::Critical, QStringLiteral("gui.stall.freeze"),
                  QStringLiteral("The GUI thread was blocked long enough for "
                                 "Windows to mark the window unresponsive"),
                  evidence,
                  QStringLiteral("The window stopped painting and stopped "
                                 "accepting input for over %1 ms.")
                      .arg(kFreezeMs),
                  scope.isEmpty()
                      ? QStringLiteral("The scope is unmarked, so wrap the "
                                       "suspect call in CUTPRO_GUI_SCOPE and "
                                       "capture again - the trace above names "
                                       "the function but not the intent.")
                      : QStringLiteral("Move the work in \"%1\" off the GUI "
                                       "thread, or make it incremental.")
                            .arg(scope)});
    else if (worst >= kHitchMs)
      out.append({Severity::Warning, QStringLiteral("gui.stall.hitch"),
                  QStringLiteral("The GUI thread stalled long enough to drop "
                                 "frames"),
                  evidence,
                  QStringLiteral("Visible as a stutter rather than a freeze, and "
                                 "it grows with project size."),
                  scope.isEmpty()
                      ? QStringLiteral("Mark the scope with CUTPRO_GUI_SCOPE so "
                                       "the next capture names it.")
                      : QStringLiteral("Profile \"%1\".").arg(scope)});
  }

  const qint64 severe = number(stats, "guiSevereStalls");
  const qint64 stalls = number(stats, "guiStalls");
  if (severe > 1)
    out.append({Severity::Critical, QStringLiteral("gui.stall.repeated"),
                QStringLiteral("Severe stalls are happening repeatedly"),
                QStringLiteral("%1 stalls over %2 ms, out of %3 total")
                    .arg(severe)
                    .arg(kFreezeMs)
                    .arg(stalls),
                QStringLiteral("One long stall can be a cold cache; several "
                               "means a code path that blocks every time it "
                               "runs."),
                QStringLiteral("Compare the scopes of the reports in the log - "
                               "category cutpro.gui.watchdog.")});

  if (worst >= number(stats, "guiStallTraceThresholdMs", 250) &&
      number(stats, "guiStallTraces") == 0)
    out.append({Severity::Warning, QStringLiteral("gui.tracer.silent"),
                QStringLiteral("A stall passed the trace threshold but no "
                               "backtrace was captured"),
                QStringLiteral("worst %1 ms, threshold %2 ms, captures %3, "
                               "tracer ready %4")
                    .arg(worst)
                    .arg(number(stats, "guiStallTraceThresholdMs", 250))
                    .arg(number(stats, "guiStallTraces"))
                    .arg(flag(stats, "guiStallTracerReady") ? QStringLiteral("yes")
                                                            : QStringLiteral("no")),
                QStringLiteral("Stalls are being counted but not attributed, so "
                               "the next freeze will be a number with no cause "
                               "attached."),
                QStringLiteral("Check that dbghelp is loadable and that "
                               "CUTPRO_STALL_TRACE_MS is not set above the "
                               "stalls being seen.")});

  if (number(stats, "guiScopeOverflows") > 0)
    out.append({Severity::Warning, QStringLiteral("gui.scope.overflow"),
                QStringLiteral("The GUI scope stack overflowed"),
                QStringLiteral("guiScopeOverflows = %1, depth now %2")
                    .arg(number(stats, "guiScopeOverflows"))
                    .arg(number(stats, "guiScopeDepthNow")),
                QStringLiteral("Scopes past the overflow are not recorded, so a "
                               "stall inside one is reported as unmarked."),
                QStringLiteral("Either the nesting is deeper than the fixed "
                               "stack, or a scope is being entered without "
                               "leaving - look for an early return past a "
                               "manual push.")});

  const qint64 posts = number(stats, "guiPosts");
  const qint64 deferred = number(stats, "guiDeferredDrains");
  if (posts > 0 && deferred > posts / 4)
    out.append({Severity::Warning, QStringLiteral("gui.dispatch.deferred"),
                QStringLiteral("GUI work is being deferred often"),
                QStringLiteral("%1 of %2 drains deferred, %3 superseded, %4 "
                               "cancelled")
                    .arg(deferred)
                    .arg(number(stats, "guiDrains"))
                    .arg(number(stats, "guiSupersededPosts"))
                    .arg(number(stats, "guiCancelledPosts")),
                QStringLiteral("A deferred drain means the GUI thread was busy "
                               "when the result arrived, so previews and "
                               "thumbnails land late."),
                QStringLiteral("Look for a long-running handler on the GUI "
                               "thread rather than at the dispatcher itself.")});
}

void appendSceneFindings(const QVariantMap &stats, QList<Finding> &out) {
  const qint64 items = number(stats, "censusItems");
  const qint64 peak = number(stats, "censusPeakItems");
  const QString topClass = string(stats, "censusTopClass");
  const qint64 topCount = number(stats, "censusTopCount");

  if (number(stats, "censusGrowthAlarms") > 0)
    out.append({Severity::Critical, QStringLiteral("scene.growth"),
                QStringLiteral("The scene graph keeps growing without shrinking"),
                QStringLiteral("%1 growth alarms, now %2 items, peak %3, most "
                               "common %4 x%5")
                    .arg(number(stats, "censusGrowthAlarms"))
                    .arg(items)
                    .arg(peak)
                    .arg(topClass.isEmpty() ? QStringLiteral("-") : topClass)
                    .arg(topCount),
                QStringLiteral("Items are being created and never freed - a leak "
                               "that ends in the whole UI slowing down and then "
                               "running out of memory."),
                QStringLiteral("The class above is where to look: find who "
                               "creates it and what should have destroyed it.")});
  else if (items >= kBusyItems)
    out.append({Severity::Warning, QStringLiteral("scene.large"),
                QStringLiteral("The scene graph is large enough that a full "
                               "rebuild is visible"),
                QStringLiteral("%1 items (peak %2), deepest chain %3, most "
                               "common %4 x%5, walk took %6 us")
                    .arg(items)
                    .arg(peak)
                    .arg(number(stats, "censusMaxDepth"))
                    .arg(topClass.isEmpty() ? QStringLiteral("-") : topClass)
                    .arg(topCount)
                    .arg(number(stats, "censusWalkUs")),
                QStringLiteral("Writing a Repeater's model re-incubates every "
                               "delegate, so at this size one model assignment "
                               "blocks the GUI thread for on the order of a "
                               "second."),
                QStringLiteral("Prefer changing the model's contents over "
                               "replacing the model, and window long tracks so "
                               "only what is on screen exists.")});

  const qint64 widest = number(stats, "censusWidestChildren");
  if (widest >= kWideParent)
    out.append({Severity::Warning, QStringLiteral("scene.wide.parent"),
                QStringLiteral("One item has hundreds of direct children"),
                QStringLiteral("%1 children under %2")
                    .arg(widest)
                    .arg(string(stats, "censusWidestParent")),
                QStringLiteral("Layout and hit-testing are linear in the child "
                               "count, so every mouse move over this parent "
                               "costs the whole list."),
                QStringLiteral("Window it: create only the children whose time "
                               "range is on screen.")});

  if (flag(stats, "censusTruncated"))
    out.append({Severity::Info, QStringLiteral("scene.census.truncated"),
                QStringLiteral("The scene walk hit its own limit"),
                QStringLiteral("censusTruncated = true at %1 items").arg(items),
                QStringLiteral("The counts above are a floor, not a total."),
                QString()});
}

void appendModelFindings(const QVariantMap &stats, QList<Finding> &out) {
  const qint64 clamps = number(stats, "guardClamps");
  if (clamps > 0)
    out.append({Severity::Warning, QStringLiteral("model.clamped"),
                QStringLiteral("A model was clamped before it reached a "
                               "Repeater"),
                QStringLiteral("%1 clamps across %2 guarded keys, worst \"%3\" "
                               "wanted %4")
                    .arg(clamps)
                    .arg(number(stats, "guardKeys"))
                    .arg(string(stats, "guardWorstKey"))
                    .arg(number(stats, "guardWorstWanted")),
                QStringLiteral("The guard stopped a delegate explosion, so the "
                               "UI is showing fewer items than the data has - "
                               "correct behaviour hiding a design problem."),
                QStringLiteral("Give that view a windowed model instead of "
                               "relying on the clamp.")});
}

void appendCrashFindings(const QVariantMap &stats, QList<Finding> &out) {
  const QString problem = string(stats, "crashReporterProblem");
  if (!problem.isEmpty())
    out.append({Severity::Critical, QStringLiteral("crash.reporter.problem"),
                QStringLiteral("The crash reporter reported a problem"),
                problem,
                QStringLiteral("A crash or hang in this run will leave no report "
                               "and no minidump."),
                QStringLiteral("Fix before trusting any \"it just closed\" "
                               "report from this build.")});
  else if (!flag(stats, "crashReporterRunning"))
    out.append({Severity::Critical, QStringLiteral("crash.reporter.absent"),
                QStringLiteral("The crash reporter is not running"),
                QStringLiteral("crashReporterRunning = false, channel ready %1")
                    .arg(flag(stats, "crashChannelReady")
                             ? QStringLiteral("yes")
                             : QStringLiteral("no")),
                QStringLiteral("Nothing is watching this process, so a crash "
                               "leaves nothing behind to read."),
                QStringLiteral("Check that cutpro_crash_report.exe sits beside "
                               "cutpro.exe and started.")});
}

// The trace's own line format, parsed rather than re-derived, so the rules below
// rest on exactly what was written to the log and to the snapshot.
QString callerOf(const QString &line) {
  const int from = line.indexOf(QStringLiteral(" from "));
  return from < 0 ? QString() : line.mid(from + 6).trimmed();
}

QString mostCommon(const QStringList &values) {
  QMap<QString, int> counts;
  for (const QString &value : values) {
    if (!value.isEmpty())
      ++counts[value];
  }
  QString best;
  int bestCount = 0;
  for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
    if (it.value() > bestCount) {
      bestCount = it.value();
      best = it.key();
    }
  }
  return best;
}

void appendPlaybackFindings(const QStringList &history, QList<Finding> &out) {
  if (history.isEmpty()) {
    out.append({Severity::Info, QStringLiteral("playback.trace.off"),
                QStringLiteral("No playback events were recorded"),
                QStringLiteral("PlaybackTrace history is empty"),
                QStringLiteral("Either nothing has been played, or the trace is "
                               "off - it is not evidence that playback is "
                               "healthy."),
                QStringLiteral("Relaunch with CUTPRO_PLAYBACK_TRACE=1 and play "
                               "a few seconds.")});
    return;
  }

  static const QRegularExpression driftRe(
      QStringLiteral("^drift (-?\\d+) ms"));
  static const QRegularExpression jumpRe(
      QStringLiteral("^playhead\\.jump \\d+ -> \\d+ ms \\((-?\\+?-?\\d+)\\)"));
  static const QRegularExpression advanceRe(
      QStringLiteral("advance (-?\\d+) ms in (-?\\d+) ms"));

  int sessionStarts = 0;
  int playTransitions = 0;
  qint64 worstDrift = 0;
  QString worstDriftLine;
  qint64 worstJump = 0;
  QString worstJumpLine;
  int backwardFrames = 0;
  QStringList sessionCallers;
  QStringList playCallers;
  qint64 spanMs = 0;

  static const QRegularExpression deltaRe(QStringLiteral("\\[\\+(\\d+) ms\\]"));
  for (const QString &line : history) {
    const auto delta = deltaRe.match(line);
    if (delta.hasMatch())
      spanMs += delta.captured(1).toLongLong();

    if (line.startsWith(QStringLiteral("session.start"))) {
      ++sessionStarts;
      sessionCallers << callerOf(line);
    } else if (line.startsWith(QStringLiteral("playing"))) {
      if (line.contains(QStringLiteral("-> true"))) {
        ++playTransitions;
        playCallers << callerOf(line);
      }
    } else if (line.startsWith(QStringLiteral("drift"))) {
      const auto match = driftRe.match(line);
      if (match.hasMatch()) {
        const qint64 value = match.captured(1).toLongLong();
        if (qAbs(value) > qAbs(worstDrift)) {
          worstDrift = value;
          worstDriftLine = line;
        }
      }
    } else if (line.startsWith(QStringLiteral("playhead.jump"))) {
      const auto match = jumpRe.match(line);
      if (match.hasMatch()) {
        const qint64 value =
            match.captured(1).remove(QLatin1Char('+')).toLongLong();
        if (qAbs(value) > qAbs(worstJump)) {
          worstJump = value;
          worstJumpLine = line;
        }
      }
    } else if (line.startsWith(QStringLiteral("frame at source"))) {
      const auto match = advanceRe.match(line);
      if (match.hasMatch() && match.captured(1).toLongLong() < 0)
        ++backwardFrames;
    }
  }

  // More decode sessions than Play presses means something other than the user
  // is restarting playback, and each restart costs a container open and a seek -
  // roughly 200 ms of frozen picture. This is the rule that turned "the video
  // stutters" into a named handler.
  if (sessionStarts > playTransitions + 1) {
    out.append({Severity::Critical, QStringLiteral("playback.session.storm"),
                QStringLiteral("Playback sessions are being restarted without a "
                               "Play press"),
                QStringLiteral("%1 decode sessions for %2 Play transitions; most "
                               "common starter: %3")
                    .arg(sessionStarts)
                    .arg(playTransitions)
                    .arg(mostCommon(sessionCallers)),
                QStringLiteral("Every restart re-opens the container and seeks, "
                               "so the picture freezes for about 200 ms and the "
                               "media falls behind the clock."),
                QStringLiteral("The caller above is the handler to fix - it is "
                               "tearing the session down mid-playback.")});
  } else if (spanMs > 0 && sessionStarts > 1 &&
             (sessionStarts * 60000) / spanMs > kSessionsPerMinute) {
    out.append({Severity::Info, QStringLiteral("playback.session.churn"),
                QStringLiteral("Playback was started and stopped rapidly"),
                QStringLiteral("%1 sessions over %2 ms, each from: %3")
                    .arg(sessionStarts)
                    .arg(spanMs)
                    .arg(mostCommon(playCallers)),
                QStringLiteral("Attributed to the transport button, so this is a "
                               "person clicking rather than a defect - each "
                               "click legitimately costs one open and seek."),
                QString()});
  }

  if (qAbs(worstDrift) > kDriftMs)
    out.append({Severity::Warning, QStringLiteral("playback.drift"),
                worstDrift > 0
                    ? QStringLiteral("The playhead runs ahead of the picture")
                    : QStringLiteral("The playhead lags behind the picture"),
                worstDriftLine,
                QStringLiteral("The timecode, the subtitle overlay and the still "
                               "drawn on pause all follow the playhead, so they "
                               "describe a frame that is not on screen."),
                QStringLiteral("Anchor the playhead to the decoder's published "
                               "frame pts and interpolate only between frames.")});

  if (qAbs(worstJump) >= 200)
    out.append({Severity::Warning, QStringLiteral("playback.playhead.jump"),
                QStringLiteral("The playhead moved by more than three frames in "
                               "one step"),
                worstJumpLine,
                QStringLiteral("Either the UI tick ran late and the "
                               "interpolation caught up in one step, or "
                               "something seeked during playback."),
                QStringLiteral("Compare the caller on that line against the UI "
                               "tick - a jump from the tick is catch-up, a jump "
                               "from anywhere else is a seek.")});

  if (backwardFrames > 0)
    out.append({Severity::Warning, QStringLiteral("playback.frames.backward"),
                QStringLiteral("Frames arrived out of order"),
                QStringLiteral("%1 published frames had a source position "
                               "earlier than the frame before them")
                    .arg(backwardFrames),
                QStringLiteral("The picture jumps backwards, and anything "
                               "anchored to frame pts jumps with it."),
                QStringLiteral("Check the generation counter: frames from a "
                               "superseded session must be dropped, not "
                               "published.")});
}

} // namespace

QString DiagnosticAnalyzer::severityName(Severity severity) {
  switch (severity) {
  case Severity::Critical:
    return QStringLiteral("critical");
  case Severity::Warning:
    return QStringLiteral("warning");
  case Severity::Info:
    break;
  }
  return QStringLiteral("info");
}

QVariantList DiagnosticAnalyzer::asVariantList(const QList<Finding> &findings) {
  QVariantList list;
  list.reserve(findings.size());
  for (const Finding &finding : findings) {
    QVariantMap map;
    map.insert(QStringLiteral("severity"), severityName(finding.severity));
    map.insert(QStringLiteral("id"), finding.id);
    map.insert(QStringLiteral("title"), finding.title);
    map.insert(QStringLiteral("evidence"), finding.evidence);
    map.insert(QStringLiteral("meaning"), finding.meaning);
    map.insert(QStringLiteral("action"), finding.action);
    list.append(map);
  }
  return list;
}

QString DiagnosticAnalyzer::verdict(const QList<Finding> &findings) {
  int critical = 0;
  int warning = 0;
  for (const Finding &finding : findings) {
    if (finding.severity == Severity::Critical)
      ++critical;
    else if (finding.severity == Severity::Warning)
      ++warning;
  }
  if (critical == 0 && warning == 0)
    return QStringLiteral("nothing wrong found in what is being measured");
  QStringList parts;
  if (critical > 0)
    parts << QStringLiteral("%1 critical").arg(critical);
  if (warning > 0)
    parts << QStringLiteral("%1 warning%2")
                 .arg(warning)
                 .arg(warning == 1 ? QString() : QStringLiteral("s"));
  return parts.join(QStringLiteral(", "));
}

QString DiagnosticAnalyzer::text(const QList<Finding> &findings) {
  if (findings.isEmpty())
    return QStringLiteral("no findings");
  QStringList lines;
  lines << verdict(findings) << QString();
  for (const Finding &finding : findings) {
    lines << QStringLiteral("[%1] %2")
                 .arg(severityName(finding.severity).toUpper(), finding.title);
    lines << QStringLiteral("    id        %1").arg(finding.id);
    if (!finding.evidence.isEmpty())
      lines << QStringLiteral("    evidence  %1").arg(finding.evidence);
    if (!finding.meaning.isEmpty())
      lines << QStringLiteral("    meaning   %1").arg(finding.meaning);
    if (!finding.action.isEmpty())
      lines << QStringLiteral("    action    %1").arg(finding.action);
    lines << QString();
  }
  return lines.join(QLatin1Char('\n'));
}

QList<Finding> DiagnosticAnalyzer::analyze(const QVariantMap &stats,
                                           const QStringList &playbackHistory) {
  QList<Finding> findings;
  appendGuiFindings(stats, findings);
  appendSceneFindings(stats, findings);
  appendModelFindings(stats, findings);
  appendCrashFindings(stats, findings);
  appendPlaybackFindings(playbackHistory, findings);

  // Worst first, and stable within a severity so the order does not shuffle
  // between two captures taken seconds apart.
  std::stable_sort(findings.begin(), findings.end(),
                   [](const Finding &a, const Finding &b) {
                     return static_cast<int>(a.severity) >
                            static_cast<int>(b.severity);
                   });
  return findings;
}
