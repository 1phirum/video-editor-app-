#include "app/core_app/backend.h"
#include "app/core_app/pool_manager.h"
#include "app/settings/app_settings.h"
#include "app/lumetri/color_settings.h"
#include "app/effects/effect_registry.h"
#include "app/effects/effect_stack.h"
#include "app/preview/media_preview_generator.h"
#include "app/preview/large_media_preview_job.h"
#include "app/export/sequence_export_builder.h"
#include "app/export/timeline_effect_window.h"
#include "app/subtitles/subtitle_io.h"
#include "app/subtitles/subtitle_timeline.h"
#include "app/subtitles/gemini_work_policy.h"
#include "app/timeline/timeline_clip_binding.h"
#include "app/timeline/timeline_placement.h"
#include "app/timeline/large_media_policy.h"
#include "app/timeline/long_media_timeline_handler.h"
#include "app/media/ffmpeg_runtime.h"
#include "app/media/media_metadata.h"
#include "app/media/python_runtime.h"
#include "app/media/media_path.h"
#include "app/media/media_scan.h"
#include "app/preview/audio_peak_builder.h"
#include "app/preview/audio_peak_window_service.h"
#include "app/preview/filmstrip_builder.h"
#include "app/diagnostics/playback_trace.h"
#include "app/preview/gui_thread_watchdog.h"
#include "core/ids.h"
#include "core/version.h"
#include "tools/whisper_worker/model_paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include "app/export/sequential_export_plan.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QFontDatabase>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>
#include <QPromise>
#include <algorithm>
#include <cmath>
#include <functional>
#include <exception>
#include <new>
#include <memory>

namespace {
// Keep a launched worker (and any console child it spawns before it gets a
// window of its own) from flashing a console on Windows. pythonw.exe already
// avoids this for the bundled interpreter, but a user who points "Python
// executable" at a console python.exe would otherwise see a black window pop
// up and vanish on every transcription/voice run. No effect off Windows.
void suppressConsoleWindow(QProcess &process) {
#ifdef Q_OS_WIN
  process.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
      });
#else
  Q_UNUSED(process);
#endif
}
QString id(const char *prefix) {
  return QString::fromStdString(core::new_id(prefix));
}
QString ffmpeg() {
  return FfmpegRuntime::executable();
}

// The offline Whisper models live under a writable per-user directory
// (%LOCALAPPDATA%\Cut Pro\models\whisper) - a per-machine install sits in
// Program Files, which the non-elevated app cannot write. These mirror the
// native worker's own resolution (model_paths.h) so the Requirements probe, the
// model picker and the worker all agree.
QString whisperModelsDir() { return WhisperModels::downloadDir(); }
// A cloud (Gemini) or OCR selection is not an offline model, so the Requirements
// row and its Download fall back to the small model, which is the app default.
QString defaultWhisperModelId(const QString &configured) {
  const QString id = configured.trimmed();
  if (!WhisperModels::ggmlFileName(id).isEmpty())
    return id;
  return QStringLiteral("small");
}
bool whisperModelInstalled(const QString &modelId) {
  const QString file = WhisperModels::ggmlFileName(modelId);
  if (file.isEmpty())
    return false;
  return !WhisperModels::existingFilePath(file).isEmpty();
}
QString ffprobe() {
  return FfmpegRuntime::probeExecutable();
}
QString kindFor(const QFileInfo &f) { return MediaScan::kindForFile(f); }
QString cleanConcatPath(QString p) {
  p.replace('\\', '/');
  p.replace('\'', "'\\''");
  return p;
}
QVariantMap defaultTrackState(const QString &track) {
  return {{"id", track.toUpper()}, {"visible", true},  {"locked", false},
          {"syncLocked", true},    {"targeted", true}, {"solo", false}};
}
QString safeFileName(QString value) {
  value = value.trimmed();
  value.replace(QRegularExpression(QStringLiteral(R"([\\/:*?\"<>|])")),
                QStringLiteral("_"));
  return value.isEmpty() ? QStringLiteral("Sequence 01") : value;
}

// "just now" / "5 minutes ago" / "3 days ago" / a date past a week - the "last
// edited" line on a workspace project card, so the list reads like a recent-
// files list rather than a column of raw timestamps.
QString relativeTimeLabel(const QDateTime &when) {
  if (!when.isValid())
    return QStringLiteral("Unknown");
  const qint64 secs = when.secsTo(QDateTime::currentDateTime());
  if (secs < 0)
    return QStringLiteral("just now");
  if (secs < 60)
    return QStringLiteral("just now");
  const auto plural = [](qint64 n, const char *unit) {
    return QStringLiteral("%1 %2%3 ago")
        .arg(n)
        .arg(QString::fromLatin1(unit))
        .arg(n == 1 ? QString() : QStringLiteral("s"));
  };
  if (secs < 3600)
    return plural(secs / 60, "minute");
  if (secs < 86400)
    return plural(secs / 3600, "hour");
  if (secs < 7 * 86400)
    return plural(secs / 86400, "day");
  return when.toString(QStringLiteral("d MMM yyyy"));
}

bool writableDirectory(const QString &path) {
  if (path.isEmpty() || (!QDir().mkpath(path) && !QDir(path).exists()))
    return false;
  QTemporaryFile probe(QDir(path).filePath(
      QStringLiteral(".cutpro-write-test-XXXXXX")));
  probe.setAutoRemove(true);
  return probe.open();
}
} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent), m_projectId(id("project")), m_sequenceId(id("sequence")),
      m_appSettings(AppSettings::load()),
      m_colorSettings(ColorSettings::defaults()), m_effectPreviewGenerator(this),
      m_textToSpeechEngine(this), m_previewDecoder(this),
      m_videoPreviewHelper(this), m_projectDatabase(this) {
  // Restore the last requirement-probe result so the Requirements page shows the
  // saved state at launch instead of re-probing the machine on every open.
  loadCachedRequirements();
  m_timelineClipModel.setClips(m_clips);
  // Start with a bounded initial projection. TimelinePanel replaces this with
  // the exact viewport once its geometry is available.
  m_timelineClipModel.setViewport(0, 5 * 60 * 1000);
  const auto flushProjection = [this]() {
    m_projectionQueued = false;
    // The preview index sits out a live voice import. Rebuilding it re-indexes
    // every clip and every bin entry, per publish, and what a voice import adds
    // is audio on A-lanes: it cannot change the picture at the playhead, so the
    // monitor has nothing to learn from it until the import ends and publishes
    // the finished timeline. The dirty flag is deliberately left standing, and
    // the connection below flushes it the moment the import is over.
    if (m_previewTimelineDirty && !m_ttsImportActive) {
      m_previewTimelineDirty = false;
      m_previewPayloadDirty = false;  // the full rebuild subsumes a payload refresh
      m_videoPreviewHelper.setTimeline(m_clips, m_media);
    } else if (m_previewPayloadDirty && !m_ttsImportActive) {
      m_previewPayloadDirty = false;
      // Structure unchanged: keep the per-track indexes, just re-read the clips
      // and re-resolve the active frame so the monitor picks up the new effect.
      m_videoPreviewHelper.refreshClipPayload(m_clips);
    }
    if (m_clipModelDirty) {
      m_clipModelDirty = false;
      m_clipModelPayloadDirty = false;
      m_timelineClipModel.setClips(m_clips);
    } else if (m_clipModelPayloadDirty) {
      m_clipModelPayloadDirty = false;
      // No span re-sort: the visible rows keep their identity and only their
      // payload changes, which is a dataChanged, not a delegate rebuild.
      m_timelineClipModel.refreshPayload(m_clips);
    }
    if (m_trackStatesDirty) {
      m_trackStatesDirty = false;
      m_videoPreviewHelper.setTrackStates(trackStates());
    }
  };
  // Queued rather than immediate: an edit emits clipsChanged more than once, and
  // each of these walks and re-indexes every clip on the timeline - which on an
  // imported caption track is twenty thousand of them. Coalesced onto the end of
  // the event loop iteration they run once per edit instead of once per emission.
  // Still before anything is painted, and setTimeline resolves as it always did,
  // so the monitor and the timeline see the finished state, not an older one.
  const auto queueProjection = [this, flushProjection]() {
    if (m_projectionQueued)
      return;
    m_projectionQueued = true;
    QMetaObject::invokeMethod(this, flushProjection, Qt::QueuedConnection);
  };
  connect(this, &Backend::clipsChanged, this, [this, queueProjection]() {
    if (m_clipsPayloadOnlyEdit) {
      // A live effect/transform/gain edit: structure is intact, so take the
      // windowed refresh in flushProjection rather than a whole-timeline rebuild.
      m_clipsPayloadOnlyEdit = false;
      m_previewPayloadDirty = true;
      m_clipModelPayloadDirty = true;
    } else {
      m_previewTimelineDirty = true;
      m_clipModelDirty = true;
    }
    queueProjection();
  });
  connect(this, &Backend::mediaChanged, this, [this, queueProjection]() {
    m_previewTimelineDirty = true;
    queueProjection();
  });
  connect(this, &Backend::tracksChanged, this, [this, queueProjection]() {
    m_trackStatesDirty = true;
    queueProjection();
  });
  // What the import deferred, collected the moment it stops. Every exit an import
  // has - finished, cancelled, or finished having placed nothing - clears the flag
  // through here, which is one place to catch instead of three, and the one that
  // cannot be forgotten by a fourth.
  connect(this, &Backend::timelineImportActiveChanged, this,
          [this, queueProjection]() {
            if (!m_ttsImportActive &&
                (m_previewTimelineDirty || m_previewPayloadDirty))
              queueProjection();
          });
  connect(this, &Backend::playheadChanged, this, [this]() {
    m_videoPreviewHelper.setPosition(m_playheadMs);
  });
  m_previewMonotonic.start();
  // Coarse timers can leave gaps after a display's one-tick interpolation.
  m_previewPlayheadTimer.setTimerType(Qt::PreciseTimer);
  m_previewPlayheadTimer.setInterval(m_videoPreviewHelper.uiTickInterval());
  connect(&m_previewPlayheadTimer, &QTimer::timeout, this,
          &Backend::advancePreviewPlayhead);
  connect(this, &Backend::customBlurEditChanged, this, [this]() {
    // Pinning the preview to the clip being masked only makes sense when that
    // clip has a picture. An effect-track item does not, so editing its mask
    // leaves the monitor showing whatever video is under the playhead - which is
    // the footage the mask is being drawn over.
    const int index = clipIndex(m_customBlurEditClipId);
    const bool pictureClip =
        index >= 0 &&
        m_clips.at(index).toMap().value("kind") != QStringLiteral("effect");
    m_videoPreviewHelper.setCustomPreviewClipId(
        pictureClip ? m_customBlurEditClipId : QString());
  });
  // Keyframes are part of the project now, so touching one is an unsaved change
  // like any other edit.
  connect(&m_keyframeEngine, &KeyframeEngine::keyframesChanged, this,
          [this](const QString &) { markDirty(); });
  connect(&m_videoPreviewHelper, &VideoPreviewHelper::activePictureChanged, this,
          [this]() {
            // The clip under the playhead changed, so the monitor's grade has to
            // follow it before the panel is told anything. On the picture signal
            // only: the grade is read out of the picture clip and its media, and
            // re-solving it for every audio clip that appears under the playhead
            // was pure cost during a voice import.
            refreshPreviewGrade();
            emit colorSettingsChanged();
          });
  // Caption word timing is not picture state - it follows the cue under the
  // playhead and the audio its timing is read from, so it stays on the general
  // signal.
  connect(&m_videoPreviewHelper, &VideoPreviewHelper::activeStateChanged, this,
          [this]() { refreshActiveCaptionWordTimes(); });
  // A cue's word timing read off the audio arrives on a pool thread; this is it
  // reaching the monitor, which then re-solves the highlight against it.
  m_captionSpeech.setReadyCallback([this]() {
    refreshActiveCaptionWordTimes();
  });
  m_videoPreviewHelper.setTimeline(m_clips, m_media);
  m_videoPreviewHelper.setTrackStates(trackStates());
  m_videoPreviewHelper.setPosition(m_playheadMs);
  connect(&m_timelinePlacementJob, &TimelinePlacementJob::stateChanged, this,
          &Backend::timelinePlacementChanged);
  connect(&m_timelinePlacementJob, &TimelinePlacementJob::stepRequested, this,
          &Backend::handleTimelinePlacementStep);
  connect(&m_timelinePlacementJob, &TimelinePlacementJob::finished, this,
          [this](bool success) {
            if (success && !m_timelinePlacementAddedIds.isEmpty()) {
              markDirty();
              emit clipsChanged();
              emit tracksChanged();
              emit timelineChanged();
              setSelectedClipId(m_timelinePlacementAddedIds.first());
            } else if (m_timelinePlacementActive) {
              const QSet<QString> partialIds(m_timelinePlacementAddedIds.begin(),
                                             m_timelinePlacementAddedIds.end());
              for (int index = m_clips.size() - 1; index >= 0; --index) {
                if (partialIds.contains(
                        m_clips.at(index).toMap().value("id").toString()))
                  m_clips.removeAt(index);
              }
              if (!m_undo.isEmpty()) {
                m_undo.removeLast();
                emit historyChanged();
              }
            }
            m_timelinePlacementActive = false;
            emit timelinePlacementFinished(
                success && !m_timelinePlacementAddedIds.isEmpty(),
                m_timelinePlacementAddedIds);
          });
  configureMediaImportQueue();
  // Register every downloaded and user-imported caption font (backend_fonts.cpp).
  loadFontLibrary();
  // The app always lands on the Home/workspace screen first. You enter the
  // editor only after creating or opening a project (the CapCut model). The
  // stored startupWorkspace no longer decides the initial landing screen; it
  // is kept as the editor view that createWorkspaceProject / openWorkspaceProject
  // switch to once a project is loaded.
  m_activeWorkspace = QStringLiteral("Home");
  m_layoutPreset = m_appSettings.value("startupLayout").toString();
  m_videoTrackCount = m_appSettings.value("defaultVideoTracks").toInt();
  // Audio lanes are created by content, CapCut-style: a sequence opens with
  // none, and the first audio clip (or the A+ button) is what brings A1 into
  // existence. Video keeps its configured default so V1 is always there.
  m_audioTrackCount = 0;
  m_minVideoTracks = m_videoTrackCount;
  m_minAudioTracks = 0;
  m_snappingEnabled = m_appSettings.value("timelineSnapping").toBool();
  // Verify any stored license against this machine before the UI is shown. A
  // key that no longer matches (e.g. after an OS reinstall) fails here and the
  // activation gate reappears.
  m_licensed = verifyStoredLicense();
  connect(&m_exportProcess, &QProcess::readyReadStandardError, this,
          &Backend::appendExportLog);
  connect(&m_exportProcess, &QProcess::readyReadStandardOutput, this,
          &Backend::updateExportProgress);
  connect(&m_exportProcess, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            // A crash also emits finished(), where stderr explains the cause.
            // Do not overwrite that explanation with "Process crashed".
            if (error == QProcess::FailedToStart)
              failExport(QStringLiteral("Could not start FFmpeg: %1")
                             .arg(m_exportProcess.errorString()));
          });
  connect(&m_exportProcess,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          &Backend::finishExportProcess);
  connect(
      &m_transcriptionProcess,
      qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
      [this](int code, QProcess::ExitStatus status) {
        const QString completedMediaId = m_transcriptionMediaId;
        const bool exitedCleanly = status == QProcess::NormalExit && code == 0;
        // Windowed runs publish their segments line by line while they work, so
        // by the time the process ends everything is already merged. Only the
        // single-pass path has a transcript sitting in the last stdout line.
        if (m_transcriptionStreamed) {
          consumeTranscriptionOutput();
          const int windowsDone = m_transcriptionWindowCount > 0
                                      ? m_transcriptionWindowIndex + 1
                                      : 0;
          const bool allWindowsDone =
              exitedCleanly && windowsDone >= m_transcriptionWindowCount;
          const qint64 covered =
              TranscriptionPlanner::coveredMs(m_transcriptionSegments);
          if (!completedMediaId.isEmpty() &&
              !m_transcriptionSegments.isEmpty()) {
            m_sourceTranscripts.insert(completedMediaId,
                                       m_transcriptionSegments);
            m_sourceTranscriptLanguages.insert(completedMediaId,
                                               m_transcriptLanguage);
            // Cleared on a complete run: there is nothing left to resume, and a
            // stale coverage mark would make the next Transcribe skip the file.
            if (allWindowsDone)
              m_transcriptCoverageMs.remove(completedMediaId);
            else
              m_transcriptCoverageMs.insert(completedMediaId, covered);
          }
          m_transcript = m_transcriptionSegments;
          if (allWindowsDone) {
            m_transcriptionStatus =
                m_transcriptLanguage.isEmpty()
                    ? QStringLiteral("Transcription complete")
                    : QStringLiteral("Transcription complete - %1")
                          .arg(m_transcriptLanguage);
            m_transcriptionProgress = 1.0;
          } else {
            const QString reached =
                QStringLiteral("%1 segments through %2")
                    .arg(m_transcriptionSegments.size())
                    .arg(TranscriptionPlanner::formatDuration(covered));
            // Only the last line of stderr: the rest is Whisper's progress bar.
            const QStringList errorLines =
                QString::fromUtf8(m_transcriptionStderr)
                    .split(QRegularExpression(QStringLiteral("[\\r\\n]")),
                           Qt::SkipEmptyParts);
            const QString workerError =
                errorLines.isEmpty() ? QString()
                                     : errorLines.last().trimmed().left(200);
            m_transcriptionStatus =
                m_transcriptionCancelRequested
                    ? QStringLiteral("Transcription cancelled - kept %1")
                          .arg(reached)
                    : QStringLiteral("Transcription stopped at %1%2")
                          .arg(reached,
                               workerError.isEmpty()
                                   ? QString()
                                   : QStringLiteral(" - %1").arg(workerError));
          }
          // No more window audio is coming, so the prefetcher's scratch files go
          // with the job directory.
          m_windowAudio.stop();
          clearTranscriptionJobDir();
          m_transcriptionMediaId.clear();
          m_transcriptionCancelRequested = false;
          m_transcriptionStderr.clear();
          m_transcriptionStdout.clear();
          m_transcriptionLastLine.clear();
          m_transcriptionSegments.clear();
          m_transcriptionStreamed = false;
          rebuildSequenceTranscript();
          // A partial transcript is still work worth saving.
          if (!m_transcript.isEmpty())
            markDirty();
          emit transcriptChanged();
          emit whisperModelsChanged();
          emit transcriptionFinished(allWindowsDone, completedMediaId);
          return;
        }
        const bool success = exitedCleanly;
        // stdout is drained as it arrives, so the reply is the last line the
        // reader saw rather than whatever is left in the pipe.
        consumeTranscriptionOutput();
        const QByteArray output = m_transcriptionLastLine;
        const auto document = QJsonDocument::fromJson(output);
        if (success && document.isObject()) {
          // The worker cuts its own lines - at the pauses in the word stream,
          // where the speaker actually stopped - so what arrives here is already
          // one segment per utterance and is stored as it came.
          const QVariantList sourceTranscript =
              document.object().value("segments").toArray().toVariantList();
          const QString sourceLanguage =
              document.object().value("language").toString();
          if (!m_transcriptionMediaId.isEmpty()) {
            m_sourceTranscripts.insert(m_transcriptionMediaId, sourceTranscript);
            m_sourceTranscriptLanguages.insert(m_transcriptionMediaId,
                                               sourceLanguage);
            m_transcriptCoverageMs.remove(m_transcriptionMediaId);
          }
          // Direct project-media transcription must be visible even when the
          // source has never been added to the timeline. If timeline clips do
          // exist, rebuildSequenceTranscript() below remaps these source times
          // into sequence time as before.
          m_transcript = sourceTranscript;
          m_transcriptLanguage = sourceLanguage;
          m_transcriptionStatus =
              m_transcriptLanguage.isEmpty()
                  ? QStringLiteral("Transcription complete")
                  : QStringLiteral("Transcription complete - %1")
                        .arg(m_transcriptLanguage);
          m_transcriptionProgress = 1.0;
        } else {
          const QString workerError =
              document.isObject() ? document.object().value("error").toString()
                                  : QString();
          const QString reason =
              workerError.isEmpty()
                  ? QString::fromUtf8(m_transcriptionStderr).trimmed()
                  : workerError;
          m_transcriptionStatus =
              m_transcriptionCancelRequested
                  ? QStringLiteral("Transcription cancelled")
                  : reason;
          // Surface the real reason in a dialog instead of only a muted status
          // line the user has to spot.
          if (!m_transcriptionCancelRequested)
            emit operationFailed(QStringLiteral("Transcription failed"),
                                 reason.isEmpty()
                                     ? QStringLiteral(
                                           "The transcription worker exited "
                                           "without reporting a reason.")
                                     : reason);
        }
        m_transcriptionMediaId.clear();
        m_transcriptionCancelRequested = false;
        m_transcriptionStderr.clear();
        m_transcriptionStdout.clear();
        m_transcriptionLastLine.clear();
        // A windowed job that died before its first line still left a scratch
        // directory behind.
        clearTranscriptionJobDir();
        rebuildSequenceTranscript();
        if (success)
          markDirty();
        emit transcriptChanged();
        emit whisperModelsChanged();
        emit transcriptionFinished(success, completedMediaId);
      });
  suppressConsoleWindow(m_transcriptionProcess);
  connect(&m_transcriptionBatch, &TranscriptionBatch::stateChanged, this, [this] {
    m_transcriptionStatus = m_transcriptionBatch.status();
    m_transcriptionProgress = m_transcriptionBatch.progress();
    emit transcriptionBatchJobsChanged();
    emit transcriptChanged();
  });
  connect(&m_transcriptionBatch, &TranscriptionBatch::sourceReady, this,
          [this](const QString &mediaId, const QVariantList &segments,
                 const QString &language, bool complete) {
    // Result ownership follows the source ID, never whichever clip happened
    // to finish last. Parallel completion must not mix transcripts or resume marks.
    m_sourceTranscripts.insert(mediaId, segments);
    m_sourceTranscriptLanguages.insert(mediaId, language);
    if (complete)
      m_transcriptCoverageMs.remove(mediaId);
    else
      m_transcriptCoverageMs.insert(mediaId, TranscriptionPlanner::coveredMs(segments));
    m_transcript = segments;
    m_transcriptLanguage = language;
    rebuildSequenceTranscript();
    markDirty();
    emit transcriptChanged();
  });
  connect(&m_transcriptionBatch, &TranscriptionBatch::jobFinished, this,
          [this](bool success, const QString &mediaId, const QString &error) {
    if (!error.isEmpty())
      emit operationFailed(QStringLiteral("Transcription failed"),
                           QStringLiteral("%1: %2").arg(mediaById(mediaId).value("name").toString(), error));
    emit transcriptionFinished(success, mediaId);
  });
  connect(&m_transcriptionProcess, &QProcess::stateChanged, this,
          [this](QProcess::ProcessState) { emit transcriptChanged(); });
  connect(&m_transcriptionProcess, &QProcess::readyReadStandardOutput, this,
          &Backend::consumeTranscriptionOutput);
  connect(&m_transcriptionProcess, &QProcess::readyReadStandardError, this,
          [this]() {
            m_transcriptionStderr += m_transcriptionProcess.readAllStandardError();
            const QString text = QString::fromLocal8Bit(m_transcriptionStderr);
            static const QRegularExpression percentPattern(QStringLiteral("(\\d{1,3})%"));
            auto matches = percentPattern.globalMatch(text);
            QRegularExpressionMatch match;
            while (matches.hasNext())
              match = matches.next();
            if (match.hasMatch()) {
              // Whisper's tqdm bar covers whatever audio it was handed, so on a
              // windowed run it restarts at zero for every window. It is the
              // position inside the current window, not the position in the file.
              const double fraction =
                  qBound(0.0, match.captured(1).toDouble() / 100.0, 1.0);
              if (m_transcriptionWindowCount > 0) {
                m_transcriptionWindowFraction = fraction;
                updateTranscriptionProgress();
              } else {
                m_transcriptionProgress = fraction;
              }
              emit transcriptChanged();
            }
            if (m_transcriptionStderr.size() > 8192)
              m_transcriptionStderr = m_transcriptionStderr.right(4096);
          });
  suppressConsoleWindow(m_demucsProcess);
  connect(&m_demucsProcess, &QProcess::readyReadStandardError, this, [this]() {
    m_demucsOutput += m_demucsProcess.readAllStandardError();
    const QString text = QString::fromLocal8Bit(m_demucsOutput);
    static const QRegularExpression percentPattern(QStringLiteral("(\\d{1,3})%"));
    auto matches = percentPattern.globalMatch(text);
    QRegularExpressionMatch match;
    while (matches.hasNext())
      match = matches.next();
    if (match.hasMatch()) {
      // Keep 100% reserved for QProcess::finished. Demucs can print its
      // final inference percentage before the WAV has been copied and the
      // clip metadata has been updated.
      const double parsed = match.captured(1).toDouble() / 100.0;
      // The default CPU profile uses one htdemucs model. Keep support for
      // the optional htdemucs_ft ensemble by aggregating its four passes.
      if (parsed + 0.20 < m_demucsLastRawProgress &&
          m_demucsProgressPass < m_demucsExpectedPasses - 1)
        ++m_demucsProgressPass;
      m_demucsLastRawProgress = parsed;
      const double overall =
          (m_demucsProgressPass + parsed) / m_demucsExpectedPasses;
      // Feed the smoothing timer a target instead of snapping the displayed
      // value; the timer eases m_demucsProgress toward it.
      m_demucsRawTarget = qBound(0.0, overall, 0.995);
    }
    m_demucsStatus = QStringLiteral("Separating vocals...");
    emit demucsChanged();
  });
  // Eases the displayed progress toward the parsed target, and creeps a
  // synthetic warm-up value up to ~15% while demucs is still loading its model
  // and has printed no percentage yet - so the bar counts smoothly from 1%.
  m_demucsSmoothTimer.setInterval(150);
  connect(&m_demucsSmoothTimer, &QTimer::timeout, this, [this]() {
    if (m_demucsProcess.state() == QProcess::NotRunning)
      return;
    double target = m_demucsRawTarget;
    if (m_demucsRawTarget < 0.02) {
      // No real progress yet: creep to 0.15 over ~45s of model load.
      const double elapsed = m_demucsWarmup.elapsed() / 1000.0;
      target = std::min(0.15, elapsed / 45.0 * 0.15);
    }
    if (target > m_demucsProgress) {
      // Ease toward the target, but always advance by a small minimum so the
      // number visibly ticks even when the target is close.
      const double eased = m_demucsProgress + (target - m_demucsProgress) * 0.18;
      m_demucsProgress =
          std::min({0.995, target, std::max(eased, m_demucsProgress + 0.004)});
      emit demucsChanged();
    }
  });
  connect(&m_demucsProcess, &QProcess::readyReadStandardOutput, this, [this]() {
    m_demucsOutput += m_demucsProcess.readAllStandardOutput();
    emit demucsChanged();
  });
  connect(&m_demucsProcess,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
    m_demucsOutput += m_demucsProcess.readAllStandardOutput();
    m_demucsOutput += m_demucsProcess.readAllStandardError();
    const QString clipId = m_demucsClipId;
    const QString outputPath = QDir(m_demucsOutputDir).filePath("no_vocals.wav");
    const bool success = status == QProcess::NormalExit && code == 0 &&
                         QFileInfo::exists(outputPath);
    if (success) {
      const int index = clipIndex(clipId);
      if (index >= 0) {
        // The replacement follows the sound: if this clip's audio has been
        // extracted onto its own lane, that clip is what actually plays it.
        QVector<int> touched = audioPeerIndexes(index);
        touched.append(index);
        for (const int i : touched) {
          QVariantMap clip = m_clips.at(i).toMap();
          QVariantMap effects = clip.value("effects").toMap();
          effects["vocalRemoval"] = true;
          effects["demucsPath"] = outputPath;
          clip["effects"] = effects;
          m_clips[i] = clip;
        }
        markDirty();
        emit clipsChanged();
        emit timelineChanged();
      }
      m_demucsProgress = 1.0;
      m_demucsStatus = QStringLiteral("Vocal separation complete");
    } else {
      m_demucsStatus = QStringLiteral("Vocal separation failed");
      const QString raw = QString::fromLocal8Bit(m_demucsOutput).trimmed();
      // The worker reports failures as a {"error": "..."} line and prints it to
      // both stdout and stderr, so the merged buffer holds the same JSON twice.
      // Pull the message out of the last such line: it reads as a plain
      // sentence instead of braces and quotes, and the duplicate collapses to
      // one. Output without that shape falls back to the raw tail.
      QString reason;
      const QStringList lines = raw.split(
          QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts);
      for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QLatin1Char('{')))
          continue;
        const QString msg = QJsonDocument::fromJson(trimmed.toUtf8())
                                .object()
                                .value(QStringLiteral("error"))
                                .toString();
        if (!msg.isEmpty())
          reason = msg;
      }
      if (reason.isEmpty())
        reason = raw.right(700);
      if (!reason.isEmpty())
        setError(reason);
    }
    m_demucsSmoothTimer.stop();
    m_demucsClipId.clear();
    m_demucsOutputDir.clear();
    m_demucsOutput.clear();
    m_demucsProgressPass = 0;
    m_demucsLastRawProgress = 0.0;
    m_demucsRawTarget = 0.0;
    m_demucsExpectedPasses = 1;
    emit demucsChanged();
    emit demucsFinished(success, clipId);
    QTimer::singleShot(0, this, &Backend::pumpDemucsSelectionQueue);
  });
  connect(&m_demucsProcess, &QProcess::stateChanged, this,
          [this](QProcess::ProcessState) { emit demucsChanged(); });
  connect(&m_translator, &TranscriptTranslator::stateChanged, this,
          &Backend::transcriptChanged);
  connect(&m_translator, &TranscriptTranslator::testFinished, this,
          [this](bool success, const QString &message) {
            if (!success && !message.isEmpty())
              setError(message);
            emit transcriptChanged();
          });
  connect(&m_recap, &RecapGenerator::stateChanged, this,
          &Backend::recapChanged);
  connect(&m_recap, &RecapGenerator::finished, this,
          [this](bool success, const QString &, const QString &error) {
            if (!success && !error.isEmpty())
              setError(error);
            emit recapChanged();
          });
  connect(&m_textToSpeechEngine, &TextToSpeechEngine::segmentsStreamed, this,
          &Backend::appendStreamedSpeech);
  connect(&m_textToSpeechEngine, &TextToSpeechEngine::finished, this,
          [this](bool success, const QVariantList &outputs,
                 const QString &error) {
            // A run that streamed its cues has already placed them, and the ones
            // still queued are placed by the close below. Nothing arrives here to
            // import a second time.
            if (m_ttsStreamOpen) {
              closeTimedSpeechStream(success, error);
              return;
            }
            if (!success) {
              if (!error.contains(QStringLiteral("cancel"),
                                  Qt::CaseInsensitive))
                setError(error.isEmpty() ? m_textToSpeechEngine.status()
                                         : error);
              return;
            }
            beginTimedSpeechImport(outputs);
          });
  connect(this, &Backend::clipsChanged, this,
          &Backend::scheduleSequenceTranscriptRebuild);
  connect(this, &Backend::tracksChanged, this,
          &Backend::scheduleSequenceTranscriptRebuild);
  connect(&m_effectPreviewGenerator, &EffectPreviewGenerator::previewReady,
          this, &Backend::effectPreviewReady);
  connect(&m_previewDecoder, &FfmpegPreviewDecoder::frameReady, this,
          [this](quint64) {
            // frameReady is queued from the decode thread, so a frame published
            // just before stop() landed can still arrive here afterwards. It was
            // never drawn, and letting it through would move the picture after
            // the pause handler had already anchored the playhead onto the frame
            // the user was looking at.
            if (!m_previewDecoder.running())
              return;
            // Recorded here, on the GUI thread, because this is the moment the
            // picture becomes the one QML draws. presentedSourceMs() on the
            // decoder is the publish instant, one queued call earlier.
            qint64 shown = -1;
            const QImage frame = m_previewDecoder.frame(&shown);
            // Several queued notifications can all refer to the newest buffer.
            // Accept it once; an old session has no timestamp after start().
            if (frame.isNull() || shown < 0 || shown == m_paintedSourceMs)
              return;
            {
              QMutexLocker locker(&m_previewFrameMutex);
              m_previewFrame = frame;
            }
            if (shown != m_paintedSourceMs) {
              m_previousPaintedSourceMs = m_paintedSourceMs;
              m_paintedSourceMs = shown;
              m_paintedWallMs = m_previewMonotonic.elapsed();
            }
            m_paintedFrameWidth = frame.width();
            m_acceptedPreviewClip = m_previewSessionClip;
            m_acceptedPreviewPath = m_previewSessionPath;
            m_acceptedPreviewSourceMs = shown;
            PreviewPlaybackClock::Sample sample;
            sample.clipStartMs = m_acceptedPreviewClip.value("startMs").toLongLong();
            sample.sourceInMs = m_acceptedPreviewClip.value("sourceInMs").toLongLong();
            sample.speed = m_acceptedPreviewClip.value("effects").toMap().value("speed", 100).toDouble() / 100;
            m_acceptedPreviewTimelineMs = PreviewPlaybackClock::timelinePosition(sample, shown);

            // Counted here rather than taken from the decoder: two producers
            // feed this URL now, and a revision that ever repeats would let QML
            // serve a cached image for a new frame.
            ++m_previewFrameRevision;
            emit previewFrameChanged();
          });

  connect(&m_previewDecoder, &FfmpegPreviewDecoder::stateChanged, this,
          &Backend::previewStateChanged);
  connect(&m_previewDecoder, &FfmpegPreviewDecoder::errorChanged, this,
          &Backend::previewErrorChanged);
  // Queued across the scrub worker thread by Qt itself; the revision is what the
  // monitor's image URL changes on, so a frame from the cache and a frame from a
  // decode look the same to QML.
  connect(&m_scrubFrames, &ScrubFrameService::frameReady, this,
          [this](quint64 revision) {
            if (!m_acceptScrubFrames || revision < m_minimumScrubRevision)
              return;
            qint64 sourceMs = -1;
            quint64 generation = 0;
            const QImage frame = m_scrubFrames.frame(&sourceMs, &generation);
            if (frame.isNull() || generation != m_scrubFrames.generation())
              return;
            {
              QMutexLocker locker(&m_previewFrameMutex);
              m_previewFrame = frame;
            }
            // A still landing while the monitor is paused replaces the picture
            // playback left there. Same frame or not, same resolution or not,
            // that repaint is what a "twitch on the freeze frame" is made of, so
            // the trace names it with both sizes and the gap between them.
            if (PlaybackTrace::enabled() && !m_playing)
              PlaybackTrace::instance().record(
                  "still.publish",
                  QStringLiteral("%1 px wide, playback picture was %2 px at %3 ms")
                      .arg(m_scrubFrames.frame().width())
                      .arg(m_paintedFrameWidth)
                      .arg(m_paintedSourceMs));
            m_acceptedPreviewClip = m_requestedPreviewClip;
            m_acceptedPreviewPath = m_requestedPreviewPath;
            m_acceptedPreviewTimelineMs = m_requestedPreviewTimelineMs;
            m_acceptedPreviewSourceMs = sourceMs;
            m_previousPaintedSourceMs = m_paintedSourceMs;
            m_paintedSourceMs = sourceMs;
            m_paintedWallMs = m_previewMonotonic.elapsed();
            m_paintedFrameWidth = frame.width();
            ++m_previewFrameRevision;
            emit previewFrameChanged();
          });

  connect(&m_scrubFrames, &ScrubFrameService::busyChanged, this,
          &Backend::previewStateChanged);
  connect(&m_scrubFrames, &ScrubFrameService::errorChanged, this,
          &Backend::previewErrorChanged);
  connect(&m_autoSaveTimer, &QTimer::timeout, this, &Backend::performAutoSave);
  m_projectDatabaseSaveTimer.setSingleShot(true);
  m_projectDatabaseSaveTimer.setInterval(250);
  connect(&m_projectDatabaseSaveTimer, &QTimer::timeout, this,
          [this]() { dispatchAsyncProjectSave(); });
  // A background save that fails (disk full, locked file) should not keep the
  // editor from running; surface it in the log, not as a modal error.
  connect(&m_projectSaveService, &ProjectSaveService::saveFinished, this,
          [](bool ok, const QString &error) {
            if (!ok)
              qWarning("Async project save failed: %s", qPrintable(error));
          });
  m_ttsImportTimer.setSingleShot(true);
  m_ttsImportTimer.setInterval(0);
  connect(&m_ttsImportTimer, &QTimer::timeout, this,
          &Backend::importNextTimedSpeechOutput);
  m_transcriptRebuildTimer.setSingleShot(true);
  // Long enough that a burst of edits pays for one rebuild, short enough that
  // the Text panel is up to date before the user can look at it.
  m_transcriptRebuildTimer.setInterval(120);
  connect(&m_transcriptRebuildTimer, &QTimer::timeout, this,
          &Backend::rebuildSequenceTranscript);
  // Trailing flush for the coalesced preview-backfill notification: when a burst
  // of thumbnail fills settles, emit the one rebuild that was held back.
  m_mediaPreviewPublishTimer.setSingleShot(true);
  connect(&m_mediaPreviewPublishTimer, &QTimer::timeout, this, [this]() {
    if (m_mediaPreviewPublishPending)
      publishMediaBinUpdate(false);
  });
  m_clipStackPublishTimer.setSingleShot(true);
  connect(&m_clipStackPublishTimer, &QTimer::timeout, this, [this]() {
    if (m_clipStackPublishPending)
      publishClipStackUpdate();
  });
  connect(this, &Backend::clipsChanged, this, &Backend::colorSettingsChanged);
  connect(this, &Backend::clipsChanged, this, [this]() {
    if (m_selectedClipId.isEmpty())
      return;
    if (clipIndex(m_selectedClipId) >= 0) {
      // The selected clip still exists. Its contents may well have moved - a
      // trim, a drag, an effect added - so the detail map is stale and the
      // panels need to re-read it. Its identity has not changed, so nothing
      // that keys off the id has anything to do.
      //
      // This used to emit selectionChanged() unconditionally here, which meant
      // every single clip mutation ran the full selection cascade: adding a
      // clip, moving one, trimming one, one emit per pointer move during a
      // drag. Worse, the emit landed while clipsChanged was still fanning out,
      // so the QML handler that revalidates selectedClipIds wrote back into
      // setSelectedClipId and started a second cascade nested inside the first.
      m_selectionDetailNotify.schedule();
      return;
    }
    m_selectedClipId.clear();
    emit selectionChanged();
    m_selectionDetailNotify.schedule();
    m_colorSettingsNotify.schedule();
  });
  connect(this, &Backend::mediaChanged, this, &Backend::colorSettingsChanged);
  connect(this, &Backend::tracksChanged, this, &Backend::colorSettingsChanged);
  connect(&m_translator, &TranscriptTranslator::finished, this,
          [this](bool success, const QVariantList &segments,
                 const QString &targetLanguage, const QString &error) {
            if (success && segments.size() == m_transcript.size()) {
              m_transcript = segments;
              m_transcriptLanguage = targetLanguage;
              // The translation has to land in the per-source transcripts, not
              // only in the sequence view. m_transcript is derived: the next
              // clipsChanged - which is exactly what "Add to Timeline" emits -
              // schedules rebuildSequenceTranscript(), and that regenerated the
              // whole list from the untranslated sources 120 ms later. The Khmer
              // came back as English while the status line still read
              // "Translated to Khmer".
              writeTranscriptTextToSources(segments);
              for (const QVariant &value : segments) {
                const QString mediaId =
                    value.toMap().value(QStringLiteral("mediaId")).toString();
                if (!mediaId.isEmpty())
                  m_sourceTranscriptLanguages.insert(mediaId, targetLanguage);
              }
              const bool subtitlesPatched = applyTranscriptTextToSubtitleClips();
              recordAction(QStringLiteral("subtitle-translated"),
                           QVariantMap{{"language", targetLanguage},
                                       {"segments", segments.size()}});
              markDirty();
              emit transcriptChanged();
              if (subtitlesPatched) {
                emit clipsChanged();
                emit timelineChanged();
              }
              return;
            }
            if (success) {
              // The transcript moved while the provider was working, so the reply
              // does not line up with it. Say so: this used to discard a finished
              // translation without a word about it.
              const QString mismatch =
                  QStringLiteral("The transcript changed while it was being "
                                 "translated (%1 segments came back for %2). "
                                 "Translate again.")
                      .arg(segments.size())
                      .arg(m_transcript.size());
              setError(mismatch);
              emit operationFailed(QStringLiteral("Translation failed"),
                                   mismatch);
            } else {
              const QString reason =
                  error.isEmpty()
                      ? QStringLiteral("The translation worker exited without "
                                       "reporting a reason.")
                      : error;
              setError(reason);
              emit operationFailed(QStringLiteral("Translation failed"), reason);
            }
            emit transcriptChanged();
          });
  configureAutoSave();
  // The model picker's cloud icon is a filesystem fact, so it follows the
  // filesystem: a checkpoint that finishes downloading while the panel is open
  // clears its icon without a restart.
  configureWhisperCacheWatch();
  // Whatever the interface was left on last time. Cheap - one SELECT over a table
  // of a few dozen rows - and it has to happen before QML binds to uiState.
  restoreUiStateFromDatabase();

  // Opening the Windows audio endpoint is a synchronous RPC to the audio service
  // that the tracer measured at 845 ms on the GUI thread - and the old code paid
  // it inside the Play handler, which is why the transport felt dead on the first
  // press. It cannot move off this thread (the device object has to live where it
  // is used), so it moves in time instead: a few seconds after launch, when the
  // window is up, nothing is playing, and a stall costs the user nothing. From
  // then on the sink is reused and Play does no device work at all.
  //
  // Single shot, not queued at interval 0: at 0 this would run before the first
  // frame is presented and simply move the stall into startup, which is already
  // the slowest part of the session.
  QTimer::singleShot(2500, this, [this]() {
    CUTPRO_GUI_SCOPE("Backend::warmAudioOutput");
    m_previewDecoder.warmAudioOutput();
  });
}

Backend::~Backend() {
  m_noiseRemoval.disconnect(this);
  m_noiseRemoval.cancel();
  teardownVoiceOverRecorder();
  if (m_exportProbeProcess) {
    m_exportProbeProcess->disconnect(this);
    m_exportProbeProcess->kill();
    m_exportProbeProcess->waitForFinished(1000);
    delete m_exportProbeProcess;
    m_exportProbeProcess = nullptr;
  }
  m_exportProcess.disconnect(this);
  if (m_exportProcess.state() != QProcess::NotRunning) {
    m_exportProcess.kill();
    m_exportProcess.waitForFinished(1000);
  }
  cleanupExportWork();
  m_silenceCancel->store(true);
  m_sceneQueue.clear();
  m_voiceFxQueue.clear();
  m_demucsSelectionQueue.clear();
  m_sceneProcess.disconnect(this);
  m_voiceFxProcess.disconnect(this);
  if (m_sceneProcess.state() != QProcess::NotRunning)
    m_sceneProcess.kill();
  if (m_voiceFxProcess.state() != QProcess::NotRunning)
    m_voiceFxProcess.kill();
  m_ttsImportTimer.stop();
  m_projectDatabaseSaveTimer.stop();
  // Drain any in-flight async save, then take one final authoritative snapshot
  // synchronously on the GUI connection so the latest edit is always on disk.
  m_projectSaveService.flush();
  persistProjectDatabase();
  // Before anything else: the import workers call back into probeMedia(), so
  // they must be stopped while this object is still whole.
  m_mediaImportQueue.shutdown();
  // The large-preview watchers are children of this and are torn down in
  // ~QObject, which severs their finished() connections; the pooled jobs they
  // wrap copy the media map by value and touch no Backend state, so any that
  // outlive us finish harmlessly on the thread pool. No wait needed.
  m_textToSpeechEngine.cancel();
  if (m_demucsProcess.state() != QProcess::NotRunning) {
    m_demucsProcess.kill();
    m_demucsProcess.waitForFinished(1000);
  }
}

bool Backend::generateTimedTextToSpeech(const QVariantList &segments,
                                        const QString &language,
                                        const QString &gender,
                                        double maxSpeed,
                                        double fixedSpeed) {
  if (m_ttsImportActive)
    return false;
  const QString worker = QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("tools/text_to_speech.py"));
  const QString outputDir =
      QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
          .filePath(QStringLiteral("generated-speech"));
  return m_textToSpeechEngine.generateSegments(
      segments, language, gender,
      m_appSettings.value(QStringLiteral("pythonExecutable"),
                          QStringLiteral("python"))
          .toString(),
      worker, outputDir, ffmpeg(), maxSpeed, fixedSpeed);
}

// Everything that has to be true before the first generated cue can be placed,
// whether the cues arrive one at a time from the running worker or all at once from
// its manifest.
void Backend::prepareTimedSpeechBatch() {
  rememberState();

  // Regeneration replaces the previous subtitle-voice batch. This keeps A1 free of
  // stale overlapping duplicates and also keeps the Project panel from accumulating
  // obsolete generated files.
  QSet<QString> oldMediaIds;
  for (const QVariant &value : m_media) {
    const QVariantMap media = value.toMap();
    if (media.value("generatedBy").toString() ==
        QStringLiteral("text_to_speech"))
      oldMediaIds.insert(media.value("id").toString());
  }
  for (int index = m_clips.size() - 1; index >= 0; --index) {
    const QVariantMap clip = m_clips[index].toMap();
    if (oldMediaIds.contains(clip.value("mediaId").toString()) ||
        clip.value("generatedBy").toString() ==
            QStringLiteral("text_to_speech"))
      m_clips.removeAt(index);
  }
  for (int index = m_media.size() - 1; index >= 0; --index) {
    if (oldMediaIds.contains(m_media[index].toMap().value("id").toString()))
      m_media.removeAt(index);
  }

  if (m_sequenceId.isEmpty()) {
    m_sequenceId = id("sequence");
    m_sequenceName = QStringLiteral("Sequence 01");
    emit sequenceChanged();
  }
  ensureTrackExists(QStringLiteral("A1"));

  // Seeded after the removal above, so the lanes the old batch used are free again.
  m_ttsLanes.seed(
      m_clips,
      [this](int number) {
        return trackLocked(QStringLiteral("A%1").arg(number));
      },
      m_audioTrackCount);
  m_ttsMediaByPath.clear();
  m_pendingTtsOutputs.clear();
  m_pendingTtsIndex = 0;
  m_pendingTtsAdded = 0;
  m_ttsTailBase = 0;
  m_ttsStreamError.clear();
  m_ttsImportActive = true;
  emit timelineImportActiveChanged();
  m_ttsPublishedMediaCount = m_media.size();
  m_ttsPublishedVideoTracks = m_videoTrackCount;
  m_ttsPublishedAudioTracks = m_audioTrackCount;
  m_ttsPublishedAdded = 0;
  m_ttsPublishClock.start();
}

// Cues from a worker that is still running.
void Backend::appendStreamedSpeech(const QVariantList &outputs) {
  if (outputs.isEmpty())
    return;
  if (!m_ttsStreamOpen) {
    // Opened by the first batch rather than when generation starts: a worker that
    // dies before it says anything must not have destroyed the previous voice track
    // on its way out.
    if (m_ttsImportActive)
      return;
    m_ttsStreamOpen = true;
    prepareTimedSpeechBatch();
  }
  m_pendingTtsOutputs.append(outputs);
  if (!m_ttsImportTimer.isActive())
    m_ttsImportTimer.start();
}

// The worker is gone. Whatever it managed to say is already on the timeline; only
// the queue that placement has not caught up with is left.
void Backend::closeTimedSpeechStream(bool success, const QString &error) {
  if (!m_ttsStreamOpen)
    return;
  m_ttsStreamOpen = false;
  if (!m_ttsImportActive)
    return;  // Cancelled: the drain already finished and cleared up.

  m_ttsTailBase = m_pendingTtsIndex;
  const int remaining = m_pendingTtsOutputs.size() - m_ttsTailBase;
  if (remaining > 0) {
    // Only now does the status stop being the worker's. During generation the
    // placement is the faster half and the queue is near empty; a resumed run, where
    // every file was already on disk, is the case where this is worth saying.
    m_textToSpeechEngine.beginTimelineImport(remaining);
  }
  if (!success && !error.isEmpty() &&
      !error.contains(QStringLiteral("cancel"), Qt::CaseInsensitive))
    m_ttsStreamError = error;
  importNextTimedSpeechOutput();
}

void Backend::beginTimedSpeechImport(const QVariantList &outputs) {
  if (outputs.isEmpty()) {
    setError(QStringLiteral("No generated subtitle voices could be imported."));
    return;
  }

  prepareTimedSpeechBatch();
  m_pendingTtsOutputs = outputs;
  // The whole import is known up front, so the two lists are grown once instead of
  // reallocating and copying a 19831-entry spine on the way through.
  m_media.reserve(m_media.size() + m_pendingTtsOutputs.size());
  m_clips.reserve(m_clips.size() + m_pendingTtsOutputs.size());
  m_textToSpeechEngine.beginTimelineImport(m_pendingTtsOutputs.size());
  m_ttsImportTimer.start();
}

void Backend::importNextTimedSpeechOutput() {
  if (!m_ttsImportActive)
    return;
  if (m_textToSpeechEngine.importCancellationRequested()) {
    m_pendingTtsOutputs.clear();
    m_ttsLanes.clear();
    m_ttsMediaByPath.clear();
    m_ttsImportActive = false;
    emit timelineImportActiveChanged();
    m_ttsStreamOpen = false;
    // Leave the voices placed so far in the finished chain, not the raw cue stack the
    // last few placements added after the final publish - a cancel should hand back a
    // clean, no-overlap layout, not a half-relaid one.
    relaySpeechChainForPreview();
    pruneEmptyTracks();
    markDirty();
    emit mediaChanged();
    emit clipsChanged();
    emit tracksChanged();
    emit timelineChanged();
    m_textToSpeechEngine.finishTimelineImport(
        false, m_pendingTtsAdded, QStringLiteral("Timeline import cancelled"));
    return;
  }

  // A time budget, not a count. Two per event-loop turn meant 9916 turns and 9916
  // rounds of signals for one generated voice track; a deadline places as many as
  // fit in one frame's worth of GUI time and still hands the loop back before the
  // window can miss a frame. At least one always goes through, so this terminates
  // no matter how slow a single placement is.
  constexpr qint64 kBudgetMs = 12;
  QElapsedTimer budget;
  budget.start();
  static const cutpro::speech::IdFactory makeId = [](const char *prefix) {
    return id(prefix);
  };
  // The silence a row keeps after a voice before the next one may reuse it. Read once
  // per drain turn, not per cue: it is the same value for the whole batch.
  const qint64 ttsGapMs =
      qBound(0, uiStateValue(QStringLiteral("tts/gapMs"), 0).toInt(), 5000);
  while (m_pendingTtsIndex < m_pendingTtsOutputs.size()) {
    const QVariantMap output =
        m_pendingTtsOutputs.at(m_pendingTtsIndex++).toMap();
    const QString knownPath = output.value(QStringLiteral("path")).toString();
    cutpro::speech::CuePlacement placement = cutpro::speech::describeCue(
        output, m_ttsMediaByPath.value(QFileInfo(knownPath).absoluteFilePath()),
        makeId);
    if (!placement.valid)
      continue;
    if (placement.needsMedia) {
      m_media.append(placement.media);
      m_ttsMediaByPath.insert(
          placement.absolutePath,
          placement.media.value(QStringLiteral("id")).toString());
    }

    // A transient preview only. While the worker is still sending cues out of order,
    // each voice is dropped near its own subtitle - reserve() hands it the lowest audio
    // row free across its span, so the intermediate never writes on top of anything and
    // stays close to where the line belongs. It may show two voices over each other for
    // a moment; that is not the final layout. Once the batch has landed,
    // chainGeneratedSpeech() below re-lays it in cue order into the real, sequential
    // no-overlap chain. The lane map holds the user's own audio and every locked track,
    // so a voice never lands on their music. See speech_chain.h.
    const qint64 lengthMs = qMax<qint64>(1, placement.endMs - placement.startMs);
    const QString destination =
        m_ttsLanes.reserve(placement.startMs, placement.startMs + lengthMs + ttsGapMs);
    ensureTrackExists(destination);
    placement.clip.insert(QStringLiteral("startMs"), placement.startMs);
    placement.clip.insert(QStringLiteral("track"), destination);
    m_clips.append(placement.clip);
    ++m_pendingTtsAdded;
    if (budget.elapsed() >= kBudgetMs)
      break;
  }

  const int total = m_pendingTtsOutputs.size();
  // The worker's own PROGRESS owns the meter while it is still running: it counts
  // cues it has finished, and those are exactly the ones this loop has placed.
  if (!m_ttsStreamOpen)
    m_textToSpeechEngine.updateTimelineImport(m_pendingTtsIndex - m_ttsTailBase,
                                              total - m_ttsTailBase);
  if (m_pendingTtsIndex < total) {
    // Publish what has accumulated, at most a few times a second, then yield to Qt.
    publishTimedSpeechProgress();
    m_ttsImportTimer.start();
    return;
  }

  if (m_ttsStreamOpen) {
    // Caught up with a worker that is still talking. Let go of what was placed:
    // holding every entry it ever sent is the memory this streaming exists to
    // avoid, and none of it is needed again.
    m_pendingTtsOutputs.clear();
    m_pendingTtsIndex = 0;
    m_ttsTailBase = 0;
    publishTimedSpeechProgress();
    return;
  }
  // The final turn falls through to the unconditional emits below, which publish
  // the finished timeline whether or not this last turn added anything.

  const int added = m_pendingTtsAdded;
  m_pendingTtsOutputs.clear();
  m_ttsLanes.clear();
  m_ttsMediaByPath.clear();
  m_ttsImportActive = false;
  // Before the emits below, deliberately: a panel that sat out the import reads
  // this flag, and the timeline it publishes next is the finished one.
  emit timelineImportActiveChanged();
  const QString streamError = m_ttsStreamError;
  m_ttsStreamError.clear();
  if (added == 0) {
    if (!m_undo.isEmpty()) {
      m_undo.removeLast();
      emit historyChanged();
    }
    const QString message =
        streamError.isEmpty()
            ? QStringLiteral("No generated subtitle voices could be imported.")
            : streamError;
    m_textToSpeechEngine.finishTimelineImport(false, 0, message);
    setError(message);
    return;
  }

  // The layout, made real. Live placement only dropped each cue near its subtitle for
  // preview while the worker sent them out of order. One pass in cue order now lays the
  // whole batch into the sequential chain: every voice waits for the one before it to
  // finish, so none is heard over another, each dealt onto the row its cue's overlap
  // earns. It is a sort and a walk over the voice clips, so it costs nothing next to the
  // import it follows, and runs before the emits below so the timeline is published once,
  // laid out. A long line can push the chain past the picture; the user pulls that in
  // with the speed limit - this pass never re-encodes.
  cutpro::speech::ChainOptions chainOptions;
  chainOptions.gapMs =
      qBound(0, uiStateValue(QStringLiteral("tts/gapMs"), 0).toInt(), 5000);
  chainOptions.audioTrackCount = m_audioTrackCount;
  chainOptions.lockedLane = [this](int number) {
    return trackLocked(QStringLiteral("A%1").arg(number));
  };
  const QVariantMap chained =
      cutpro::speech::chainGeneratedSpeech(m_clips, chainOptions);
  for (const QVariant &name : chained.value(QStringLiteral("tracks")).toList())
    ensureTrackExists(name.toString());

  pruneEmptyTracks();
  markDirty();
  emit mediaChanged();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  recordAction(QStringLiteral("text_to_speech"),
               QVariantMap{{"segmentCount", added},
                           {"placement", QStringLiteral("sequential-rows")}});
  if (!streamError.isEmpty()) {
    // Part of the track was spoken before the worker gave up. What was placed is
    // kept - it is one re-run away from being complete, and that re-run costs only
    // the cues that failed - and the failure is still reported.
    m_textToSpeechEngine.finishTimelineImport(false, added, streamError);
    setError(streamError);
    return;
  }
  m_textToSpeechEngine.finishTimelineImport(true, added);
}

// At most a few times a second, and only for what actually changed: with identical
// cue text sharing one bin entry, most stretches of an import add no media entry and
// no track, and a notification nothing follows is pure cost.
void Backend::relaySpeechChainForPreview() {
  // The same pass the finished import runs at the very end, run early: while the
  // worker is still sending cues, the drain drops each one near its subtitle for
  // speed, which piles voices on top of one another. Left alone that raw stack is
  // what the timeline shows for the whole of a long import - the overlap this mode
  // exists to remove - until the final pass snaps it into line. Running it here on
  // the publish cadence (a few times a second, not once per placement) keeps the
  // preview honest for the cost of a sort and a walk, small next to the re-projection
  // the emit below already pays. Idempotent, so once the tail has caught up it moves
  // nothing.
  cutpro::speech::ChainOptions options;
  options.gapMs =
      qBound(0, uiStateValue(QStringLiteral("tts/gapMs"), 0).toInt(), 5000);
  options.audioTrackCount = m_audioTrackCount;
  options.lockedLane = [this](int number) {
    return trackLocked(QStringLiteral("A%1").arg(number));
  };
  const QVariantMap chained =
      cutpro::speech::chainGeneratedSpeech(m_clips, options);
  for (const QVariant &name : chained.value(QStringLiteral("tracks")).toList())
    ensureTrackExists(name.toString());
}

void Backend::publishTimedSpeechProgress() {
  // The rate follows what a publish costs. Every one of them re-projects the
  // whole clip list - spans, the windowed collapse, the derived caches QML reads
  // - so the price of a notification grows with the clips already placed, while
  // what it buys stays the same: a glimpse of a track that is still arriving. A
  // fixed 150 ms spent a growing share of the GUI thread on that glimpse. This
  // stretches to 1000 ms as the sequence fills, so a twenty-thousand-clip import
  // pays for the whole-list relay and re-projection about once a second instead of
  // four times, and the GUI thread is left free for the rest of the app.
  constexpr qint64 kBasePublishIntervalMs = 150;
  constexpr qint64 kMaxPublishIntervalMs = 1000;
  constexpr int kFreeClips = 2000;
  const qint64 extra =
      qMax<qint64>(0, qint64(m_clips.size()) - kFreeClips) / 8;
  const qint64 interval =
      qMin(kMaxPublishIntervalMs, kBasePublishIntervalMs + extra);
  if (m_ttsPublishClock.elapsed() < interval)
    return;
  // Nothing was placed since the last publish: the relay would reorder nothing and
  // the re-projection would rebuild the very list already on screen. Skip both, and
  // leave the clock unrestarted so the next real placement shows the moment the
  // interval has passed rather than a further interval later.
  if (m_pendingTtsAdded == m_ttsPublishedAdded)
    return;
  m_ttsPublishClock.restart();
  m_ttsPublishedAdded = m_pendingTtsAdded;
  // Snap the voices placed since the last publish into the chain before showing them,
  // so the preview never flashes the raw cue overlap between here and the final pass.
  relaySpeechChainForPreview();
  if (m_media.size() != m_ttsPublishedMediaCount) {
    m_ttsPublishedMediaCount = m_media.size();
    emit mediaChanged();
  }
  emit clipsChanged();
  if (m_videoTrackCount != m_ttsPublishedVideoTracks ||
      m_audioTrackCount != m_ttsPublishedAudioTracks) {
    m_ttsPublishedVideoTracks = m_videoTrackCount;
    m_ttsPublishedAudioTracks = m_audioTrackCount;
    emit tracksChanged();
  }
  emit timelineChanged();
}

QString Backend::coreVersion() const {
  return QString::fromLatin1(core::kVersion);
}
qint64 Backend::durationMs() const {
  ensureClipCaches();
  return m_cachedDurationMs;
}

// One pass over m_clips answers every derived question QML asks about the
// timeline. Guarded by a pin that shares m_clips' buffer: while the pin holds a
// second reference the list cannot be mutated in place, so a differing data
// pointer is a reliable "something changed" signal, and an identical one is a
// reliable "nothing did".
void Backend::ensureClipCaches() const {
  if (m_clipCacheReady && m_clipCachePin.size() == m_clips.size() &&
      m_clipCachePin.constData() == m_clips.constData())
    return;
  CUTPRO_GUI_SCOPE("Backend::ensureClipCaches");

  static const QString kId = QStringLiteral("id");
  static const QString kKind = QStringLiteral("kind");
  static const QString kTrack = QStringLiteral("track");
  static const QString kStartMs = QStringLiteral("startMs");
  static const QString kDurationMs = QStringLiteral("durationMs");
  static const QString kSubtitle = QStringLiteral("subtitle");
  static const QString kEffect = QStringLiteral("effect");

  m_clipCachePin = m_clips;
  m_cachedClipIndex.clear();
  m_cachedClipIndex.reserve(m_clips.size());
  m_cachedTimelineEffects.clear();
  m_cachedMediaClips.clear();
  m_cachedVideoClips.clear();
  m_cachedDurationMs = 0;
  m_cachedSubtitleTrackCount = 1;
  m_cachedHasSubtitleClips = false;
  m_cachedHasRenderableClips = false;

  qint64 mediaEnd = 0;
  qint64 overlayEnd = 0;
  for (int i = 0; i < m_clips.size(); ++i) {
    const QVariantMap clip = m_clips.at(i).toMap();
    const auto idIt = clip.constFind(kId);
    if (idIt != clip.constEnd()) {
      // First occurrence wins, matching the linear scan clipIndex() used to do.
      const QString clipId = idIt->toString();
      if (!m_cachedClipIndex.contains(clipId))
        m_cachedClipIndex.insert(clipId, i);
    }

    const auto kindIt = clip.constFind(kKind);
    const QString kind =
        kindIt == clip.constEnd() ? QString() : kindIt->toString();
    const bool isSubtitle = kind == kSubtitle;
    const bool isEffect = kind == kEffect;

    const qint64 end = clip.value(kStartMs).toLongLong() +
                       clip.value(kDurationMs).toLongLong();
    // Subtitles and effect bars ride over the picture; on their own they are not
    // something to render, so they only set the length when nothing else does.
    if (isSubtitle || isEffect)
      overlayEnd = std::max(overlayEnd, end);
    else
      mediaEnd = std::max(mediaEnd, end);

    if (isSubtitle) {
      m_cachedHasSubtitleClips = true;
      const QString track =
          TimelinePlacement::normalizedTrack(clip.value(kTrack).toString());
      if (track.startsWith(QLatin1Char('S')))
        m_cachedSubtitleTrackCount =
            qMax(m_cachedSubtitleTrackCount, TimelinePlacement::trackNumber(track));
    } else
      m_cachedMediaClips.append(m_clips.at(i));
    if (isEffect)
      m_cachedTimelineEffects.append(m_clips.at(i));
    if (!isSubtitle && !isEffect)
      m_cachedHasRenderableClips = true;
    if (clip.value(kTrack).toString().startsWith(QLatin1Char('V')))
      m_cachedVideoClips.append(i);
  }
  m_cachedDurationMs = mediaEnd > 0 ? mediaEnd : overlayEnd;
  m_clipCacheReady = true;
}

bool Backend::hasSubtitleClips() const {
  ensureClipCaches();
  return m_cachedHasSubtitleClips;
}

void Backend::ensureMediaCaches() const {
  if (m_mediaCacheReady && m_mediaCachePin.size() == m_media.size() &&
      m_mediaCachePin.constData() == m_media.constData())
    return;
  CUTPRO_GUI_SCOPE("Backend::ensureMediaCaches");

  static const QString kId = QStringLiteral("id");
  static const QString kKind = QStringLiteral("kind");
  static const QString kPath = QStringLiteral("path");
  static const QString kExclude = QStringLiteral("excludeFromTranscript");
  static const QString kGeneratedBy = QStringLiteral("generatedBy");
  static const QString kVideo = QStringLiteral("video");
  static const QString kAudio = QStringLiteral("audio");
  static const QString kTextToSpeech = QStringLiteral("text_to_speech");
  static const QString kGeneratedSpeechDir = QStringLiteral("/generated-speech/");
  static const QString kHidden = QStringLiteral("hiddenInProjectPanel");

  m_mediaCachePin = m_media;
  m_cachedMediaIndex.clear();
  m_cachedMediaIndex.reserve(m_media.size());
  m_cachedTranscribableMedia.clear();
  m_cachedVisibleMedia.clear();
  m_cachedTranscribableIds.clear();

  for (int i = 0; i < m_media.size(); ++i) {
    const QVariantMap entry = m_media.at(i).toMap();
    const auto idIt = entry.constFind(kId);
    const QString mediaId = idIt == entry.constEnd() ? QString() : idIt->toString();
    if (!mediaId.isEmpty() && !m_cachedMediaIndex.contains(mediaId))
      // First occurrence wins, matching the linear scan mediaIndex() used to do.
      m_cachedMediaIndex.insert(mediaId, i);

    const QString generatedBy = entry.value(kGeneratedBy).toString();
    if (!entry.value(kHidden).toBool() && generatedBy != kTextToSpeech)
      m_cachedVisibleMedia.append(m_media.at(i));

    const QString kind = entry.value(kKind).toString();
    if (kind != kVideo && kind != kAudio)
      continue;
    if (entry.value(kExclude).toBool())
      continue;
    if (generatedBy == kTextToSpeech)
      continue;
    // Checked last: it is the only test that has to touch the path string.
    if (QDir::fromNativeSeparators(entry.value(kPath).toString())
            .contains(kGeneratedSpeechDir, Qt::CaseInsensitive))
      continue;
    m_cachedTranscribableMedia.append(m_media.at(i));
    if (!mediaId.isEmpty())
      m_cachedTranscribableIds.insert(mediaId);
  }
  m_mediaCacheReady = true;
}

QVariantList Backend::transcribableMedia() const {
  ensureMediaCaches();
  return m_cachedTranscribableMedia;
}

QVariantList Backend::visibleMedia() const {
  ensureMediaCaches();
  return m_cachedVisibleMedia;
}

bool Backend::isTranscribableMedia(const QString &mediaId) const {
  if (mediaId.isEmpty())
    return false;
  ensureMediaCaches();
  return m_cachedTranscribableIds.contains(mediaId);
}

QVariantList Backend::timelineEffects() const {
  ensureClipCaches();
  return m_cachedTimelineEffects;
}

QVariantList Backend::mediaClips() const {
  ensureClipCaches();
  return m_cachedMediaClips;
}

QVariantMap Backend::clipById(const QString &id) const {
  const int index = clipIndex(id);
  return index < 0 ? QVariantMap{} : m_clips.at(index).toMap();
}

bool Backend::canExport() const {
  if (!hasSequence())
    return false;
  ensureClipCaches();
  return m_cachedHasRenderableClips;
}
void Backend::setProjectName(const QString &v) {
  const auto n = v.trimmed();
  if (n.isEmpty() || n == m_projectName)
    return;
  rememberState();
  m_projectName = n;
  markDirty();
  emit projectChanged();
}
void Backend::setSequenceName(const QString &v) {
  const auto n = v.trimmed();
  if (n.isEmpty() || n == m_sequenceName)
    return;
  rememberState();
  m_sequenceName = n;
  markDirty();
  emit sequenceChanged();
}
void Backend::setActiveWorkspace(const QString &v) {
  if (v == m_activeWorkspace)
    return;
  // Returning to the home screen commits the open project to its real
  // .cutpro.json. The home cards read that file, not the auto-save shadow, so
  // without this a clip added in the editor never shows up as a thumbnail (nor
  // in media/clip counts) until the user saved by hand. Only a project that
  // has a file and unsaved edits is written; a fresh, never-saved sequence or a
  // clean one is left alone.
  if (v == QStringLiteral("Home") && m_dirty && !m_sequenceId.isEmpty() &&
      !m_projectFile.isEmpty())
    saveProject(m_projectFile);
  m_activeWorkspace = v;
  emit activeWorkspaceChanged();
}
void Backend::setLayoutPreset(const QString &v) {
  if (v == m_layoutPreset)
    return;
  m_layoutPreset = v;
  emit layoutPresetChanged();
}
void Backend::setPlayheadMs(qint64 v) {
  v = qBound<qint64>(0, v, durationMs());
  if (v == m_playheadMs)
    return;
  // A playhead that moves backwards, or jumps further than a couple of frames,
  // is either a seek or the symptom being hunted - the UI tick advances it by
  // one frame at a time, so anything larger had another cause and the trace
  // names it. The ordinary per-frame advance is not logged.
  if (PlaybackTrace::enabled() && m_playing) {
    const qint64 delta = v - m_playheadMs;
    if (delta < 0 || delta > 120)
      PlaybackTrace::instance().record(
          "playhead.jump", QStringLiteral("%1 -> %2 ms (%3%4)")
                               .arg(m_playheadMs)
                               .arg(v)
                               .arg(delta >= 0 ? QStringLiteral("+")
                                               : QString())
                               .arg(delta));
  }
  m_playheadMs = v;
  if (!m_advancingPreview && !m_pausingPreview)
    ++m_previewTransportSerial;
  const bool seekDuringPlayback = m_playing && !m_advancingPreview && !m_pausingPreview;
  if (seekDuringPlayback) {
    ++m_previewSeekSerial;
    m_previewPlaybackClock.start(m_previewMonotonic.elapsed(), v);
  }
  emit playheadChanged();
  if (seekDuringPlayback && m_playing) {
    const QVariantMap &clip = m_videoPreviewHelper.activeClipMap();
    const QVariantMap &media = m_videoPreviewHelper.activeMediaMap();
    const qint64 elapsed = qMax<qint64>(0, m_playheadMs - clip.value("startMs").toLongLong());
    const double speed = clip.value("effects").toMap().value("speed", 100).toDouble() / 100;
    if (!clip.isEmpty() && media.value("kind") != QStringLiteral("image"))
      startPreviewDecode(media.value("path").toString(), media.value("kind").toString(),
          clip.value("sourceInMs").toLongLong() + qRound64(elapsed * speed),
          qMax<qint64>(1, clip.value("durationMs").toLongLong() - elapsed),
          media.value("width").toInt(), media.value("height").toInt(),
          media.value("frameRate", 30).toDouble(), true,
          m_previewAudioVolume,
          clipReplacementAudioPath(m_videoPreviewHelper.activeAudioClip().toMap()));
  }
}

void Backend::setPlaying(bool playing) {
  if (m_playing == playing)
    return;
  ++m_previewTransportSerial;
  if (!playing) {
    m_previewPlayheadTimer.stop();
    m_pausingPreview = true;
    const bool hadVideoFrame = m_acceptedPreviewTimelineMs >= 0 &&
        clipIndex(m_acceptedPreviewClip.value("id").toString()) >= 0 &&
        m_acceptedPreviewClip.value("kind") == QStringLiteral("video");
    stopPreviewDecode();
    if (hadVideoFrame && m_playheadMs < durationMs() &&
        m_videoPreviewHelper.activeMediaMap().value("kind").toString() ==
            QStringLiteral("video")) {
      // Anchor while playing is still true: this position notification must
      // not be treated by the paused monitor as a user seek. The accepted
      // image is retained; pausing never requests another decode.
      setPlayheadMs(m_acceptedPreviewTimelineMs);
      m_paintedSourceMs = m_acceptedPreviewSourceMs;
    }
    m_pausingPreview = false;
  } else {
    m_previewPlaybackClock.start(m_previewMonotonic.elapsed(), m_playheadMs);
  }
  CUTPRO_PLAYBACK_TRACE("playing",
                        QStringLiteral("%1 -> %2 at playhead %3 ms")
                            .arg(m_playing ? QStringLiteral("true")
                                           : QStringLiteral("false"),
                                 playing ? QStringLiteral("true")
                                         : QStringLiteral("false"))
                            .arg(m_playheadMs));
  m_playing = playing;
  emit playingChanged();
  if (m_playing)
    m_previewPlayheadTimer.start();
}

bool Backend::newProject(const QString &name, const QString &location,
                         bool makeSequence, const QString &seqName) {
  m_projectDatabaseSaveTimer.stop();
  m_projectId = id("project");
  m_projectName =
      name.trimmed().isEmpty() ? QStringLiteral("Untitled") : name.trimmed();
  m_projectLocation = normalizePath(location);
  m_projectFile.clear();
  m_projectDatabaseFile.clear();
  m_projectDatabase.close();
  m_sequenceId = makeSequence ? id("sequence") : QString();
  m_sequenceName = seqName.trimmed().isEmpty() ? QStringLiteral("Sequence 01")
                                               : seqName.trimmed();
  m_media.clear();
  m_clips.clear();
  m_sourceTranscripts.clear();
  m_sourceTranscriptLanguages.clear();
  m_transcriptCoverageMs.clear();
  m_transcript.clear();
  m_videoTrackCount = m_appSettings.value("defaultVideoTracks").toInt();
  // A new sequence has no audio clips, so it opens with no audio lane. See the
  // constructor: A1 arrives with the first audio clip or the A+ button.
  m_audioTrackCount = 0;
  m_minVideoTracks = m_videoTrackCount;
  m_minAudioTracks = 0;
  m_mutedTracks.clear();
  m_trackStates.clear();
  m_markers.clear();
  m_snappingEnabled = m_appSettings.value("timelineSnapping").toBool();
  m_captionStyle = CaptionStyle{};
  m_colorSettings = ColorSettings::defaults();
  m_selectedClipId.clear();
  m_effectEditSelection.clear();
  if (!m_customBlurEditClipId.isEmpty() ||
      !m_customBlurEditInstanceId.isEmpty()) {
    m_customBlurEditClipId.clear();
    m_customBlurEditInstanceId.clear();
    emit customBlurEditChanged();
  }
  m_playheadMs = 0;
  setPlaying(false);
  m_undo.clear();
  m_redo.clear();
  markDirty(false);
  clearError();
  // Every project gets a database, including this one. An untitled project used to
  // get none, so its action log and its state snapshots went nowhere until the day
  // it was saved - and if it was never saved, nowhere at all. m_uiState is left
  // alone on purpose: a new project is not a reason to forget which panel the user
  // works in or which voice they use.
  if (openProjectDatabase())
    persistProjectDatabase();
  emitAllStateChanged();
  return true;
}

bool Backend::setClipEffectSetting(const QString &clipId, const QString &key,
                                   const QVariant &value) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  QVariantMap effects = ClipEffects::defaults();
  const QVariantMap existing = clip.value("effects").toMap();
  for (auto it = existing.cbegin(); it != existing.cend(); ++it)
    effects[it.key()] = it.value();
  if (!ClipEffects::setValue(&effects, key, value))
    return false;
  rememberStateForLiveEdit();
  clip["effects"] = effects;
  m_clips[index] = clip;
  markDirty();
  // Coalesced: a live gain/transform/effect slider streams this per mouse-move,
  // and a bare clipsChanged() re-projects the whole timeline each time.
  publishClipStackUpdate();
  if (key == QStringLiteral("vocalRemoval") && value.toBool() && !m_demucsBatchSetting)
    startDemucsForClip(clipId);
  return true;
}

bool Backend::startDemucsForClip(const QString &clipId) {
  if (demucsInProgress())
    return false;
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  const auto clip = m_clips.at(index).toMap();
  const auto item = mediaById(clip.value("mediaId").toString());
  const QString mediaPath = item.value("path").toString();
  if (mediaPath.isEmpty()) {
    setError(QStringLiteral("The selected clip has no source media path."));
    return false;
  }
  // The engine and (for Demucs) the quality come from Settings, so a weak
  // machine can pick the light/fast path. Both workers run under the bundled
  // interpreter their packages install into.
  const QString engine =
      m_appSettings.value("vocalEngine", "demucs").toString();
  const bool useMdxnet = engine == QStringLiteral("mdxnet");
  const QString quality =
      m_appSettings.value("vocalDemucsQuality", "fast").toString();
  const bool cleanDemucs = quality == QStringLiteral("clean");

  const QString python = PythonRuntime::executable(
      m_appSettings.value("pythonExecutable", "python").toString());
  const QString workerFile = useMdxnet ? QStringLiteral("tools/mdxnet_separate.py")
                                       : QStringLiteral("tools/demucs_separate.py");
  const QString worker =
      QDir(QCoreApplication::applicationDirPath()).filePath(workerFile);
  if (python.isEmpty() || !QFileInfo::exists(worker)) {
    setError(QStringLiteral("Python vocal-separation worker is not available."));
    return false;
  }
  m_demucsClipId = clipId;
  // Cache is keyed by engine + quality as well as media, so switching the
  // Settings choice re-separates rather than serving the previous stem.
  const QString cacheTag =
      useMdxnet ? QStringLiteral("mdxnet")
                : (cleanDemucs ? QStringLiteral("demucs-clean")
                               : QStringLiteral("demucs-fast"));
  m_demucsOutputDir = QDir(QStandardPaths::writableLocation(
      QStandardPaths::AppDataLocation)).filePath(
      QStringLiteral("vocal-sep-v6/%1/%2")
          .arg(cacheTag, clip.value("mediaId").toString()));

  // Demucs model: an explicit env var still wins for power users; otherwise the
  // quality toggle decides. htdemucs_ft is the 4-model ensemble (cleaner, ~4x
  // slower); plain htdemucs is the single fast model.
  QString demucsModel = qEnvironmentVariable(
      "CUTPRO_DEMUCS_MODEL",
      cleanDemucs ? QStringLiteral("htdemucs_ft") : QStringLiteral("htdemucs"));
  const int configuredShifts =
      qMax(1, qEnvironmentVariableIntValue("CUTPRO_DEMUCS_SHIFTS"));
  // Both workers get a fresh environment carrying the bundled ffmpeg. The
  // MDX-Net worker decodes the source to WAV with it first: audio-separator
  // reads audio through libsndfile, which cannot open a video container and
  // dies with "Format not recognised", whereas Demucs' torchaudio path decodes
  // it natively. Setting the environment on every run also clears any
  // CUTPRO_DEMUCS_MODEL left over from a previous demucs run on this reused
  // process.
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("CUTPRO_FFMPEG"), ffmpeg());
  if (useMdxnet) {
    // MDX-Net is a single ONNX pass.
    m_demucsExpectedPasses = 1;
  } else {
    m_demucsExpectedPasses =
        (demucsModel.contains(QStringLiteral("ft"), Qt::CaseInsensitive) ? 4 : 1)
        * configuredShifts;
    // Tell the demucs worker which model to load (it reads CUTPRO_DEMUCS_MODEL).
    env.insert(QStringLiteral("CUTPRO_DEMUCS_MODEL"), demucsModel);
  }
  m_demucsProcess.setProcessEnvironment(env);
  QDir().mkpath(m_demucsOutputDir);
  const QString cached = QDir(m_demucsOutputDir).filePath("no_vocals.wav");
  if (QFileInfo::exists(cached)) {
    QVector<int> touched = audioPeerIndexes(index);
    touched.append(index);
    for (const int i : touched) {
      QVariantMap updated = m_clips.at(i).toMap();
      QVariantMap effects = updated.value("effects").toMap();
      effects["vocalRemoval"] = true;
      effects["demucsPath"] = cached;
      updated["effects"] = effects;
      m_clips[i] = updated;
    }
    markDirty();
    emit clipsChanged();
    emit timelineChanged();
    m_demucsProgress = 1.0;
    m_demucsStatus = QStringLiteral("Vocal separation ready");
    emit demucsChanged();
    emit demucsFinished(true, clipId);
    return true;
  }
  m_demucsProgress = 0.0;
  m_demucsProgressPass = 0;
  m_demucsLastRawProgress = 0.0;
  m_demucsRawTarget = 0.0;
  m_demucsStatus = QStringLiteral("Preparing vocal separation...");
  m_demucsOutput.clear();
  emit demucsChanged();
  m_demucsProcess.start(python, {worker, mediaPath, m_demucsOutputDir});
  if (!m_demucsProcess.waitForStarted(1500)) {
    m_demucsStatus = QStringLiteral("Vocal separation failed");
    setError(QStringLiteral("Demucs could not be started."));
    emit demucsChanged();
    return false;
  }
  // Start the smooth warm-up creep now that the process is running.
  m_demucsWarmup.restart();
  m_demucsSmoothTimer.start();
  return true;
}

void Backend::pumpDemucsSelectionQueue() {
  if (demucsInProgress() || m_demucsSelectionPumping)
    return;
  m_demucsSelectionPumping = true;
  while (!demucsInProgress() && !m_demucsSelectionQueue.isEmpty()) {
    const QString id = m_demucsSelectionQueue.takeFirst();
    if (editableClipIds({id}, "audio").isEmpty())
      continue;
    startDemucsForClip(id);
    // Cache hits finish inline; continue iteratively rather than recurse.
  }
  m_demucsSelectionPumping = false;
}

void Backend::cancelDemucs() {
  m_demucsSelectionQueue.clear();
  if (demucsInProgress()) {
    m_demucsSmoothTimer.stop();
    m_demucsProcess.kill();
    m_demucsStatus = QStringLiteral("Vocal separation cancelled");
    emit demucsChanged();
  }
}

bool Backend::resetClipEffectSettings(const QString &clipId) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  if (clip.value("effects").toMap().isEmpty() &&
      clip.value("effectStack").toList().isEmpty())
    return false;
  rememberState();
  clip.remove("effects");
  clip.remove("effectStack");
  m_clips[index] = clip;
  markDirty();
  notifyClipEdit();
  return true;
}

QString Backend::addClipEffect(const QString &clipId,
                               const QString &effectId) {
  int index = clipIndex(clipId);
  const QVariantMap definition = EffectRegistry::definition(effectId);
  if (index < 0 || definition.isEmpty()) {
    setError(QStringLiteral("The selected effect is not available."));
    return {};
  }
  QVariantMap clip = m_clips.at(index).toMap();
  if (definition.value("mediaType") == QStringLiteral("audio") &&
      clip.value("separateAudio").toBool()) {
    // This clip no longer carries its own sound, so an audio effect dropped on
    // it belongs on the extracted clip that does.
    for (const int candidate : audioPeerIndexes(index)) {
      const QVariantMap peer = m_clips.at(candidate).toMap();
      if (peer.value("kind").toString() != QStringLiteral("audio"))
        continue;
      index = candidate;
      clip = peer;
      break;
    }
  }
  const QVariantMap media = mediaById(clip.value("mediaId").toString());
  if (!EffectRegistry::supportsClip(definition,
                                    clip.value("kind").toString(),
                                    media.value("channels").toInt() > 0)) {
    setError(definition.value("mediaType") == QStringLiteral("audio")
                 ? QStringLiteral("This clip has no audio stream.")
                 : QStringLiteral("This effect requires a video or image clip."));
    return {};
  }

  QVariantList stack = clip.value("effectStack").toList();
  const QString instanceId = id("effect");
  if (!EffectStack::append(&stack, definition, instanceId))
    return {};
  rememberState();
  clip["effectStack"] = stack;
  m_clips[index] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  setSelectedClipId(clip.value("id").toString());
  emit effectControlsRequested();
  // A Custom Blur does nothing until its region is placed, so hand the mask
  // straight to the program monitor rather than making the user find the
  // draw-mask button inside the Effect Controls tree first.
  if (effectId == QStringLiteral("custom_blur"))
    beginCustomBlurMaskEdit(clip.value("id").toString(), instanceId);
  return instanceId;
}

QString Backend::addTimelineEffect(const QString &effectId, qint64 startMs,
                                   qint64 durationMs) {
  const QVariantMap definition = EffectRegistry::definition(effectId);
  if (definition.isEmpty()) {
    setError(QStringLiteral("The selected effect is not available."));
    return {};
  }
  // The lane sits over the picture, so it can only carry effects that act on
  // the picture. An audio effect there would have nothing to process.
  if (definition.value("mediaType") == QStringLiteral("audio")) {
    setError(QStringLiteral(
        "Audio effects go on an audio clip, not on the effect track."));
    return {};
  }
  // A bar means "this effect, over this stretch". An effect ffmpeg cannot switch
  // on and off along the timeline cannot honour that, and applying it to the
  // whole clip instead would be the one thing the user did not ask for.
  if (!TimelineEffectWindow::supportsWindowing(effectId)) {
    setError(QStringLiteral("%1 cannot be limited to part of the timeline. "
                            "Add it to the clip itself instead.")
                 .arg(definition.value("name").toString()));
    return {};
  }
  if (m_sequenceId.isEmpty())
    createSequence();

  QVariantList stack;
  const QString instanceId = id("effect");
  if (!EffectStack::append(&stack, definition, instanceId))
    return {};

  const qint64 start = qMax(0LL, startMs < 0 ? m_playheadMs : startMs);
  // Five seconds is long enough to see and grab, short enough that trimming it
  // to the stretch that actually needs the effect is a small move either way.
  const qint64 span = durationMs > 0 ? durationMs : 5000;
  const QString clipId = id("clip");
  const QVariantMap clip{{"id", clipId},
                         {"kind", QStringLiteral("effect")},
                         {"track", QStringLiteral("F1")},
                         {"mediaId", QString()},
                         {"name", definition.value("name")},
                         {"definitionId", definition.value("id")},
                         {"startMs", start},
                         {"durationMs", span},
                         {"sourceInMs", 0},
                         {"sourceDurationMs", 0},
                         {"enabled", true},
                         {"effectStack", stack}};

  rememberState();
  m_clips.append(clip);
  markDirty();
  clearError();
  emit clipsChanged();
  emit timelineChanged();
  setSelectedClipId(clipId);
  emit effectControlsRequested();
  if (effectId == QStringLiteral("custom_blur"))
    beginCustomBlurMaskEdit(clipId, instanceId);
  return clipId;
}

bool Backend::removeClipEffect(const QString &clipId,
                               const QString &instanceId) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  QVariantList stack = clip.value("effectStack").toList();
  if (!EffectStack::remove(&stack, instanceId))
    return false;
  rememberState();
  clip["effectStack"] = stack;
  m_clips[index] = clip;
  markDirty();
  notifyClipEdit();
  if (clipId == m_customBlurEditClipId &&
      instanceId == m_customBlurEditInstanceId)
    endCustomBlurMaskEdit();
  return true;
}

bool Backend::moveClipEffect(const QString &clipId, const QString &instanceId,
                             int offset) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  QVariantList stack = clip.value("effectStack").toList();
  if (!EffectStack::move(&stack, instanceId, offset))
    return false;
  rememberState();
  clip["effectStack"] = stack;
  m_clips[index] = clip;
  markDirty();
  notifyClipEdit();
  return true;
}

bool Backend::setClipEffectEnabled(const QString &clipId,
                                   const QString &instanceId, bool enabled) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  QVariantList stack = clip.value("effectStack").toList();
  if (!EffectStack::setEnabled(&stack, instanceId, enabled))
    return false;
  rememberState();
  clip["effectStack"] = stack;
  m_clips[index] = clip;
  markDirty();
  notifyClipEdit();
  if (!enabled && clipId == m_customBlurEditClipId &&
      instanceId == m_customBlurEditInstanceId)
    endCustomBlurMaskEdit();
  return true;
}

bool Backend::setClipEffectParameter(const QString &clipId,
                                     const QString &instanceId,
                                     const QString &parameterId,
                                     const QVariant &value) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  QVariantList stack = clip.value("effectStack").toList();
  QVariantMap definition;
  for (const auto &entry : stack) {
    const QVariantMap instance = entry.toMap();
    if (instance.value("id").toString() == instanceId) {
      definition = EffectRegistry::definition(
          instance.value("definitionId").toString());
      break;
    }
  }
  if (!EffectStack::setParameter(&stack, instanceId, definition, parameterId,
                                 value))
    return false;
  rememberStateForLiveEdit();
  clip["effectStack"] = stack;
  m_clips[index] = clip;
  markDirty();
  // Coalesced: the blur-strength (and every Effect Controls) slider is live and
  // fires this per mouse-move; a bare clipsChanged() re-projects the whole
  // timeline each tick. The trailing tick always publishes the released value.
  publishClipStackUpdate();
  return true;
}

bool Backend::resetClipEffectInstance(const QString &clipId,
                                      const QString &instanceId) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  QVariantList stack = clip.value("effectStack").toList();
  QVariantMap definition;
  for (const auto &entry : stack) {
    const QVariantMap instance = entry.toMap();
    if (instance.value("id").toString() == instanceId) {
      definition = EffectRegistry::definition(
          instance.value("definitionId").toString());
      break;
    }
  }
  if (!EffectStack::reset(&stack, instanceId, definition))
    return false;
  rememberState();
  clip["effectStack"] = stack;
  m_clips[index] = clip;
  markDirty();
  notifyClipEdit();
  return true;
}

bool Backend::beginCustomBlurMaskEdit(const QString &clipId,
                                      const QString &instanceId) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  const QVariantMap clip = m_clips.at(index).toMap();
  bool found = false;
  for (const QVariant &value : clip.value("effectStack").toList()) {
    const QVariantMap instance = value.toMap();
    if (instance.value("id").toString() == instanceId &&
        instance.value("definitionId").toString() ==
            QStringLiteral("custom_blur") &&
        instance.value("enabled", true).toBool()) {
      found = true;
      break;
    }
  }
  if (!found)
    return false;

  setSelectedClipId(clipId);
  setPlaying(false);
  const qint64 start = clip.value("startMs").toLongLong();
  const qint64 end = start + clip.value("durationMs").toLongLong();
  if (m_playheadMs < start || m_playheadMs >= end)
    setPlayheadMs(start);
  if (m_customBlurEditClipId == clipId &&
      m_customBlurEditInstanceId == instanceId)
    return true;
  m_customBlurEditClipId = clipId;
  m_customBlurEditInstanceId = instanceId;
  emit customBlurEditChanged();
  return true;
}

void Backend::endCustomBlurMaskEdit() {
  if (m_customBlurEditClipId.isEmpty() && m_customBlurEditInstanceId.isEmpty())
    return;
  m_customBlurEditClipId.clear();
  m_customBlurEditInstanceId.clear();
  emit customBlurEditChanged();
}

bool Backend::setCustomBlurMask(const QString &clipId,
                                const QString &instanceId, double x, double y,
                                double width, double height) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  x = qBound(0.0, x, 0.98);
  y = qBound(0.0, y, 0.98);
  width = qMin(qBound(0.02, width, 1.0), 1.0 - x);
  height = qMin(qBound(0.02, height, 1.0), 1.0 - y);

  QVariantMap clip = m_clips.at(index).toMap();
  QVariantList stack = clip.value("effectStack").toList();
  const QVariantMap definition =
      EffectRegistry::definition(QStringLiteral("custom_blur"));
  const QVariantMap mask{{"x", x},
                         {"y", y},
                         {"width", width},
                         {"height", height}};
  if (!EffectStack::setParameter(&stack, instanceId, definition,
                                 QStringLiteral("mask"), mask))
    return false;
  rememberState();
  clip["effectStack"] = stack;
  m_clips[index] = clip;
  markDirty();
  notifyClipEdit();
  return true;
}

void Backend::requestEffectControls(const QString &clipId) {
  setSelectedClipId(clipId);
  if (!m_selectedClipId.isEmpty())
    emit effectControlsRequested();
}

void Backend::requestEffectsBrowser(const QString &clipId) {
  if (!clipId.isEmpty())
    setSelectedClipId(clipId);
  emit effectsBrowserRequested();
}

QString Backend::requestEffectPreview(const QString &clipId,
                                      const QString &effectId,
                                      bool animated) {
  const int index = clipIndex(clipId);
  const QVariantMap definition = EffectRegistry::definition(effectId);
  if (index < 0 || definition.value("mediaType") != QStringLiteral("video"))
    return {};

  const QVariantMap clip = m_clips.at(index).toMap();
  if (clip.value("kind") != QStringLiteral("video") &&
      clip.value("kind") != QStringLiteral("image"))
    return {};
  const QVariantMap media = mediaById(clip.value("mediaId").toString());
  if (media.isEmpty())
    return {};

  const qint64 clipStart = clip.value("startMs").toLongLong();
  const qint64 clipDuration = clip.value("durationMs").toLongLong();
  const qint64 sourceIn = clip.value("sourceInMs").toLongLong();
  const bool playheadInside =
      m_playheadMs >= clipStart && m_playheadMs < clipStart + clipDuration;
  const qint64 localPosition =
      playheadInside ? m_playheadMs - clipStart : clipDuration / 2;
  const qint64 sourcePosition = sourceIn + qMax<qint64>(0, localPosition);
  const qint64 sourceDuration = media.value("durationMs").toLongLong();
  const QString path = media.value("path").toString();
  const QString cached = m_effectPreviewGenerator.cachedPreview(
      path, sourcePosition, sourceDuration, effectId, animated);
  if (cached.isEmpty())
    m_effectPreviewGenerator.request(
        clipId, path, media.value("kind").toString(), sourcePosition,
        sourceDuration, effectId, animated);
  return cached;
}

QSet<QString> Backend::mediaDuplicateKeys() const {
  QSet<QString> keys;
  keys.reserve(m_media.size());
  for (const QVariant &value : m_media) {
    const QString key = MediaPath::duplicateKey(
        value.toMap().value(QStringLiteral("path")).toString());
    if (!key.isEmpty())
      keys.insert(key);
  }
  return keys;
}

int Backend::importMedia(const QStringList &paths, bool copy) {
  const auto files = expandImportPaths(paths);
  if (files.isEmpty()) {
    setError(QStringLiteral("No supported media files were selected."));
    return 0;
  }
  rememberState();
  // One pass over the bin instead of a linear rescan per file: importing 200
  // files into a bin of 200 previously meant 40 000 canonicalFilePath() calls,
  // each of which hits the filesystem.
  QSet<QString> keys = mediaDuplicateKeys();
  int count = 0;
  for (const auto &source : files) {
    QString path = source;
    if (copy) {
      if (m_projectLocation.isEmpty()) {
        setError(
            QStringLiteral("Choose a project location before copying media."));
        continue;
      }
      QDir d(QDir(m_projectLocation).filePath("Media"));
      if (!d.mkpath(".")) {
        setError(QStringLiteral("Could not create the project Media folder."));
        continue;
      }
      path = d.filePath(QFileInfo(source).fileName());
      if (QFileInfo::exists(path))
        path = d.filePath(QFileInfo(path).completeBaseName() + "_" +
                          id("copy").right(6) + "." + QFileInfo(path).suffix());
      if (!QFile::copy(source, path)) {
        setError(QStringLiteral("Could not copy %1.")
                     .arg(QFileInfo(source).fileName()));
        continue;
      }
    }
    const QString key = MediaPath::duplicateKey(path);
    if (key.isEmpty() || keys.contains(key))
      continue;
    // Rejected here rather than inside libav, which would only report an
    // unhelpful "No such file or directory" for a name it could not encode.
    QString reason;
    if (!MediaPath::isDecodable(path, &reason)) {
      setError(reason);
      continue;
    }
    auto item = probeMedia(path);
    if (item.value("kind") == "unknown")
      continue;
    keys.insert(key);
    item["id"] = id("media");
    item["importedAt"] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    m_media.append(item);
    ++count;
  }
  if (!count) {
    m_undo.removeLast();
    emit historyChanged();
    return 0;
  }
  markDirty();
  emit mediaChanged();
  emit timelineChanged();
  return count;
}

void Backend::configureMediaImportQueue() {
  // Runs on an import worker. Detailed filmstrips and waveforms are deferred: a
  // single cached thumbnail is enough for the project bin while a long file is
  // still importing, and generating twelve filmstrip cells per file is what used
  // to make a folder import take minutes.
  m_mediaImportQueue.setProber(
      [this](const QString &path) { return probeMedia(path, false); });
  connect(&m_mediaImportQueue, &MediaImportQueue::itemsReady, this,
          &Backend::appendImportedMedia);
  connect(&m_mediaImportQueue, &MediaImportQueue::progressChanged, this,
          &Backend::mediaImportChanged);
  connect(&m_mediaImportQueue, &MediaImportQueue::warning, this,
          [this](const QString &message) { setError(message); });
  connect(&m_mediaImportQueue, &MediaImportQueue::finished, this,
          [this](int accepted, int skipped, bool wasCancelled) {
            Q_UNUSED(skipped);
            if (!accepted && !wasCancelled && m_mediaImportQueue.total() == 0)
              setError(
                  QStringLiteral("No supported media files were selected."));
            m_mediaImportRemembered = false;
            emit mediaImportChanged();
          });
}

// True when the background job still has something to produce for this item. A
// video wants a poster and a filmstrip; anything with an audio track wants a
// waveform. Checking each artefact separately means a cached poster no longer
// suppresses the filmstrip, which is what previously left long clips as flat
// rectangles even after their thumbnail had appeared in the bin.
bool Backend::needsDeferredPreview(const QVariantMap &item) {
  const QString kind = item.value(QStringLiteral("kind")).toString();
  const bool isVideo = kind == QStringLiteral("video");
  const bool isAudio = kind == QStringLiteral("audio");
  if (!isVideo && !isAudio)
    return false;
  if (isVideo) {
    if (item.value(QStringLiteral("thumbnailUrl")).toString().isEmpty())
      return true;
    if (item.value(QStringLiteral("timelineThumbnailUrl")).toString().isEmpty())
      return true;
  }
  if ((isAudio || item.value(QStringLiteral("channels")).toInt() > 0) &&
      item.value(QStringLiteral("waveformUrl")).toString().isEmpty())
    return true;
  return false;
}

// Backfill for a source that reaches the timeline without its artefacts: media
// imported by an older build, restored from a project saved before the deferred
// job covered every entry, or dropped while its own import job was still
// queued. The clip draws its waveform from decoded windows in the meantime, so
// this is about the instant sheet underneath them, not about correctness.
void Backend::ensureMediaPreviews(const QString &mediaId) {
  if (mediaId.isEmpty())
    return;
  const int index = mediaIndex(mediaId);
  if (index < 0)
    return;
  const QVariantMap item = m_media.at(index).toMap();
  if (needsDeferredPreview(item))
    startDeferredMediaPreview(item);
}

void Backend::appendImportedMedia(const QVariantList &items) {
  if (items.isEmpty())
    return;
  // One undo entry for the whole import, taken before the first item lands.
  if (!m_mediaImportRemembered) {
    rememberState();
    m_mediaImportRemembered = true;
  }
  // Appended as a batch: mediaChanged() rebuilds the bin model, so emitting it
  // per file made import cost grow with the square of the bin size.
  QVariantList deferred;
  for (const QVariant &value : items) {
    QVariantMap item = value.toMap();
    if (item.value(QStringLiteral("kind")).toString() ==
        QStringLiteral("unknown"))
      continue;
    item[QStringLiteral("id")] = id("media");
    item[QStringLiteral("importedAt")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    m_media.append(item);
    // The import queue deliberately skips detailed previews so the bin appears
    // instantly, which leaves every entry - not just the multi-hour ones -
    // without the filmstrip and waveform the timeline draws from. Whatever is
    // still missing is produced in the background now that the item is visible.
    if (needsDeferredPreview(item))
      deferred.append(item);
  }
  markDirty();
  // Coalesced through the shared bin throttle: the 120 ms import queue batches
  // already arrive several times a second, and each mediaChanged() re-sorts the
  // whole visibleMedia list, so into a large bin a long import was still a
  // full-bin rebuild many times a second. withTimeline=false: this only appends
  // to the media bin, it touches no clip. Emitting timelineChanged() here
  // re-projected the whole timeline (VideoPreviewHelper::setTimeline ->
  // rebuildIndexes, an O(clips) rebuild) on every batch for nothing. The bin
  // view listens on mediaChanged().
  publishMediaBinUpdate(false);
  for (const QVariant &value : deferred)
    startDeferredMediaPreview(value.toMap());
}

void Backend::importMediaAsync(const QStringList &paths, bool copy) {
  CUTPRO_GUI_SCOPE("Backend::importMediaAsync");
  if (m_mediaImportQueue.active())
    return;
  if (paths.isEmpty()) {
    setError(QStringLiteral("No supported media files were selected."));
    return;
  }

  MediaImportQueue::Request request;
  request.paths = paths;
  if (copy) {
    if (m_projectLocation.isEmpty()) {
      setError(
          QStringLiteral("Choose a project location before copying media."));
      return;
    }
    request.copyDestination =
        QDir(m_projectLocation).filePath(QStringLiteral("Media"));
  }

  m_mediaImportRemembered = false;
  // A snapshot, not a live view: the queue rejects duplicates on worker threads
  // and must not read m_media while the GUI thread is appending to it.
  m_mediaImportQueue.setExistingKeys(mediaDuplicateKeys());
  m_mediaImportQueue.start(request);
  emit mediaImportChanged();
}

void Backend::cancelMediaImport() { m_mediaImportQueue.cancel(); }

bool Backend::removeMedia(const QString &mediaId) {
  int i = mediaIndex(mediaId);
  if (i < 0)
    return false;
  rememberState();
  m_media.removeAt(i);
  m_sourceTranscripts.remove(mediaId);
  m_sourceTranscriptLanguages.remove(mediaId);
  m_transcriptCoverageMs.remove(mediaId);
  for (int n = m_clips.size() - 1; n >= 0; --n)
    if (m_clips[n].toMap().value("mediaId") == mediaId)
      m_clips.removeAt(n);
  pruneEmptyTracks(true);
  markDirty();
  emit mediaChanged();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}
bool Backend::createSequence(const QString &name) {
  if (!m_sequenceId.isEmpty())
    return false;
  rememberState();
  m_sequenceId = id("sequence");
  m_sequenceName =
      name.trimmed().isEmpty() ? QStringLiteral("Sequence 01") : name.trimmed();
  markDirty();
  emit sequenceChanged();
  return true;
}
QString Backend::addClip(const QString &mediaId, qint64 start,
                         const QString &track) {
  auto item = mediaById(mediaId);
  if (item.isEmpty()) {
    setError(QStringLiteral("Media item not found."));
    return {};
  }
  if (m_sequenceId.isEmpty())
    createSequence();
  rememberState();
  auto k = item.value("kind").toString();
  qint64 dur = item.value("durationMs").toLongLong();
  if (dur <= 0)
    dur = k == "image"
              ? m_appSettings.value("defaultImageDurationMs").toLongLong()
              : 1000;
  if (start < 0)
    start = durationMs();
  const QString destination = track.isEmpty()
                                  ? TimelinePlacement::defaultTrackForKind(k)
                                  : TimelinePlacement::normalizedTrack(track);
  if (!TimelinePlacement::trackAcceptsKind(destination, k)) {
    setError(
        k == "audio"
            ? QStringLiteral("Audio clips can only be placed on A tracks.")
            : QStringLiteral(
                  "Video and image clips can only be placed on V tracks."));
    // In an edit batch rememberState() was skipped, so the snapshot on the
    // undo stack belongs to the batch (or an earlier edit) — never pop it.
    if (m_editBatchDepth == 0) {
      m_undo.removeLast();
      emit historyChanged();
    }
    return {};
  }
  QVariantMap c{{"id", id("clip")},
                {"mediaId", mediaId},
                {"name", item.value("name")},
                {"kind", k},
                {"track", destination},
                {"startMs", qMax<qint64>(0, start)},
                {"sourceInMs", 0},
                {"sourceDurationMs", dur},
                {"durationMs", dur},
                {"enabled", true}};
  // Same overlay default as the drop handler: an image on an upper video track
  // comes in at half size as a picture-in-picture, a V1 image stays full-frame.
  if (k == QStringLiteral("image")) {
    bool laneOk = false;
    const int lane = destination.startsWith(QLatin1Char('V'), Qt::CaseInsensitive)
                         ? destination.mid(1).toInt(&laneOk)
                         : 0;
    if (laneOk && lane >= 2) {
      QVariantMap fx;
      fx.insert(QStringLiteral("scale"), 50.0);
      fx.insert(QStringLiteral("positionX"), 0.0);
      fx.insert(QStringLiteral("positionY"), 0.0);
      fx.insert(QStringLiteral("opacity"), 100.0);
      c.insert(QStringLiteral("effects"), fx);
    }
  }
  ensureTrackExists(c.value("track").toString());
  m_clips.append(c);
  ensureMediaPreviews(mediaId);
  markDirty();
  if (m_editBatchDepth > 0) {
    m_editBatchClipsDirty = true;
  } else {
    emit clipsChanged();
    emit timelineChanged();
  }
  return c.value("id").toString();
}

QStringList Backend::addClips(const QStringList &mediaIds) {
  QStringList ids;
  if (mediaIds.isEmpty())
    return ids;
  // One snapshot / one clipsChanged for the whole batch instead of per item:
  // appending N selected media was N rememberState() deep-copies (expensive at
  // the 19831-cue scale) and N full projection passes.
  beginEditBatch();
  for (const QString &mediaId : mediaIds) {
    const QString clipId = addClip(mediaId);
    if (!clipId.isEmpty())
      ids.append(clipId);
  }
  endEditBatch();
  return ids;
}

QString Backend::addImageOverlay(const QString &pathOrMediaId, qint64 startMs) {
  if (m_sequenceId.isEmpty())
    createSequence();

  // Accept either an already-imported media id or a filesystem path / file URL.
  QString mediaId;
  QVariantMap item = mediaById(pathOrMediaId);
  if (!item.isEmpty()) {
    mediaId = pathOrMediaId;
  } else {
    const QString path = normalizePath(pathOrMediaId);
    const QString key = MediaPath::duplicateKey(path);
    // Reuse the bin entry for this file if it is already imported.
    for (int i = 0; i < m_media.size() && mediaId.isEmpty(); ++i) {
      const QVariantMap mm = m_media.at(i).toMap();
      if (!key.isEmpty() &&
          MediaPath::duplicateKey(mm.value("path").toString()) == key) {
        mediaId = mm.value("id").toString();
        item = mm;
      }
    }
    if (mediaId.isEmpty()) {
      importMedia({path}, false);
      for (int i = 0; i < m_media.size() && mediaId.isEmpty(); ++i) {
        const QVariantMap mm = m_media.at(i).toMap();
        if (!key.isEmpty() &&
            MediaPath::duplicateKey(mm.value("path").toString()) == key) {
          mediaId = mm.value("id").toString();
          item = mm;
        }
      }
    }
  }
  if (mediaId.isEmpty() || item.isEmpty()) {
    setError(QStringLiteral("Could not add the overlay image."));
    return {};
  }

  const QString kind = item.value("kind").toString();
  if (kind != QStringLiteral("image") && kind != QStringLiteral("video")) {
    setError(QStringLiteral("Only an image or video can be used as an overlay."));
    return {};
  }
  // OVERLAY_PLACEMENT_MARKER

  qint64 dur = item.value("durationMs").toLongLong();
  if (dur <= 0)
    dur = kind == QStringLiteral("image")
              ? qMax<qint64>(
                    1000,
                    m_appSettings.value("defaultImageDurationMs").toLongLong())
              : 1000;
  qint64 start = qMax<qint64>(0, startMs < 0 ? m_playheadMs : startMs);

  // Put the overlay on a fresh V track above everything, so it always
  // composites on top and never collides with a clip already on the timeline.
  int maxV = 0;
  for (int i = 0; i < m_clips.size(); ++i) {
    const QString t = m_clips.at(i).toMap().value("track").toString();
    if (t.startsWith(QLatin1Char('V'))) {
      bool ok = false;
      const int n = t.mid(1).toInt(&ok);
      if (ok)
        maxV = qMax(maxV, n);
    }
  }
  const QString destination = QStringLiteral("V%1").arg(maxV + 1);

  rememberState();
  const QVariantMap effects{{"scale", 35.0},
                            {"positionX", 0.0},
                            {"positionY", 0.0},
                            {"opacity", 100.0}};
  QVariantMap c{{"id", id("clip")},
                {"mediaId", mediaId},
                {"name", item.value("name")},
                {"kind", kind},
                {"track", destination},
                {"startMs", start},
                {"sourceInMs", 0},
                {"sourceDurationMs", dur},
                {"durationMs", dur},
                {"overlay", true},
                {"effects", effects},
                {"enabled", true}};
  ensureTrackExists(destination);
  m_clips.append(c);
  ensureMediaPreviews(mediaId);
  markDirty();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  setSelectedClipId(c.value("id").toString());
  return c.value("id").toString();
}

QStringList Backend::addMediaToTimeline(const QString &mediaId, qint64 start,
                                        const QString &track) {
  // The drop handler. Its own work is in-memory variant bookkeeping; measurement
  // showed the GUI-thread cost is not here and not in the clipsChanged /
  // timelineChanged emits either, but in the selection change at the end. The
  // nested scopes below are what proved that, so they stay.
  CUTPRO_GUI_SCOPE("Backend::addMediaToTimeline");
  const QVariantMap item = mediaById(mediaId);
  if (item.isEmpty()) {
    setError(QStringLiteral("Media item not found."));
    return {};
  }
  if (m_sequenceId.isEmpty())
    createSequence();

  const QString mediaKind = item.value("kind").toString();
  qint64 duration = item.value("durationMs").toLongLong();
  if (duration <= 0)
    duration = mediaKind == QStringLiteral("image")
                   ? m_appSettings.value("defaultImageDurationMs").toLongLong()
                   : 1000;
  if (start < 0)
    start = durationMs();
  start = qMax<qint64>(0, start);

  const QString primaryTrack =
      track.isEmpty() ? TimelinePlacement::defaultTrackForKind(mediaKind)
                      : TimelinePlacement::normalizedTrack(track);
  if (!TimelinePlacement::trackAcceptsKind(primaryTrack, mediaKind)) {
    setError(mediaKind == QStringLiteral("audio")
                 ? QStringLiteral("Audio clips can only be placed on A tracks.")
                 : QStringLiteral(
                       "Video and image clips can only be placed on V tracks."));
    return {};
  }

  {
    // Undo snapshot. It deep-copies the clip, track and media variant lists, so
    // its cost grows with the project rather than with this one placement.
    CUTPRO_GUI_SCOPE("Backend::addMediaToTimeline/rememberState");
    rememberState();
  }

  QStringList addedIds;
  // A video file is one timeline item. Its audio stream is carried by the
  // same V-track clip and rendered from the media item during playback/export.
  // Standalone audio files remain A-track clips.
  QVariantMap request = LongMediaTimelineHandler::placementRequest(
      item, start, primaryTrack);
  request["durationMs"] = duration;
  QVariantMap clip =
      LongMediaTimelineHandler::timelineClip(item, request, id("clip"));
  // An image dropped on an upper video track is an overlay (a picture-in-
  // picture over the track below), so it comes in at half size like CapCut
  // rather than filling the frame. Stored on the clip so the program monitor
  // and the export composite agree. A V1 image is a base/background and stays
  // full-frame (no seeded scale).
  if (mediaKind == QStringLiteral("image") && !clip.contains("effects")) {
    bool laneOk = false;
    const int lane = primaryTrack.startsWith(QLatin1Char('V'), Qt::CaseInsensitive)
                         ? primaryTrack.mid(1).toInt(&laneOk)
                         : 0;
    if (laneOk && lane >= 2) {
      QVariantMap fx;
      fx.insert(QStringLiteral("scale"), 50.0);
      fx.insert(QStringLiteral("positionX"), 0.0);
      fx.insert(QStringLiteral("positionY"), 0.0);
      fx.insert(QStringLiteral("opacity"), 100.0);
      clip.insert(QStringLiteral("effects"), fx);
    }
  }
  ensureTrackExists(primaryTrack);
  addedIds.append(clip.value("id").toString());
  m_clips.append(clip);
  ensureMediaPreviews(mediaId);

  markDirty();
  {
    // Not "just a signal". Every one of these is delivered synchronously to
    // every QML binding that reads clips/tracks, which rebuilds the timeline
    // delegates, their filmstrips and their waveforms before this line returns.
    CUTPRO_GUI_SCOPE("Backend::addMediaToTimeline/emit clips+tracks+timeline");
    emit clipsChanged();
    emit tracksChanged();
    emit timelineChanged();
  }
  if (!addedIds.isEmpty()) {
    CUTPRO_GUI_SCOPE("Backend::addMediaToTimeline/setSelectedClipId");
    setSelectedClipId(addedIds.first());
  }
  return addedIds;
}

bool Backend::beginTimelinePlacement(const QStringList &mediaIds, qint64 start,
                                     const QString &track) {
  if (m_timelinePlacementJob.inProgress() || mediaIds.isEmpty())
    return false;

  QVariantList items;
  qint64 cursor = qMax<qint64>(0, start);
  for (const QString &mediaId : mediaIds) {
    const QVariantMap media = mediaById(mediaId);
    if (media.isEmpty())
      continue;
    const QString kind = media.value("kind").toString();
    // Per item, because a selection can mix kinds: the audio files in a mixed
    // drop used to be dropped on the floor when the pointer was over a video
    // lane. They land on the nearest lane that accepts them instead.
    const QString requested = track.isEmpty()
                                  ? TimelinePlacement::defaultTrackForKind(kind)
                                  : TimelinePlacement::normalizedTrack(track);
    const QString destination = TimelinePlacement::trackAcceptsKind(requested, kind)
                                    ? requested
                                    : compatibleTrackFor(kind, requested);
    if (!TimelinePlacement::trackAcceptsKind(destination, kind))
      continue;
    qint64 duration = media.value("durationMs").toLongLong();
    if (duration <= 0)
      duration = kind == QStringLiteral("image")
                     ? m_appSettings.value("defaultImageDurationMs").toLongLong()
                     : 1000;
    items.append(LongMediaTimelineHandler::placementRequest(
        media, cursor, destination));
    cursor += qMax<qint64>(250, duration);
  }
  if (items.isEmpty()) {
    setError(QStringLiteral("No compatible media could be placed here."));
    return false;
  }

  m_timelinePlacementAddedIds.clear();
  m_timelinePlacementActive = true;
  m_timelinePlacementJob.start(items);
  return true;
}

void Backend::cancelTimelinePlacement() { m_timelinePlacementJob.cancel(); }

void Backend::handleTimelinePlacementStep(const QVariantMap &request) {
  const QVariantMap media = mediaById(request.value("mediaId").toString());
  if (media.isEmpty())
    return;
  if (m_timelinePlacementAddedIds.isEmpty()) {
    if (m_sequenceId.isEmpty()) {
      m_sequenceId = id("sequence");
      m_sequenceName = QStringLiteral("Sequence 01");
      emit sequenceChanged();
    }
    rememberState();
  }

  const QString kind = media.value("kind").toString();
  qint64 duration = media.value("durationMs").toLongLong();
  if (duration <= 0)
    duration = kind == QStringLiteral("image")
                   ? m_appSettings.value("defaultImageDurationMs").toLongLong()
                   : 1000;
  const QString destination =
      TimelinePlacement::normalizedTrack(request.value("track").toString());
  QVariantMap clip = LongMediaTimelineHandler::timelineClip(
      media, request, id("clip"));
  ensureTrackExists(destination);
  m_timelinePlacementAddedIds.append(clip.value("id").toString());
  m_clips.append(clip);
  ensureMediaPreviews(request.value("mediaId").toString());
}
bool Backend::moveClip(const QString &clipId, qint64 start,
                       const QString &track) {
  int i = clipIndex(clipId);
  if (i < 0)
    return false;
  auto c = m_clips[i].toMap();
  if (trackLocked(c.value("track").toString())) {
    setError(QStringLiteral("Unlock the track before moving clips."));
    return false;
  }
  const QString destination =
      track.isEmpty()
          ? TimelinePlacement::normalizedTrack(c.value("track").toString())
          : TimelinePlacement::normalizedTrack(track);
  if (!TimelinePlacement::trackAcceptsKind(destination,
                                           c.value("kind").toString())) {
    setError(
        c.value("kind") == "audio"
            ? QStringLiteral("Audio clips can only be placed on A tracks.")
            : QStringLiteral(
                  "Video and image clips can only be placed on V tracks."));
    return false;
  }
  rememberState();
  c["startMs"] = qMax<qint64>(0, start);
  c["track"] = destination;
  ensureTrackExists(c.value("track").toString());
  m_clips[i] = c;
  // A move vacates the track it came from, so the emptied lane goes with it.
  pruneEmptyTracks(true);
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  // A subtitle move can add or empty a text lane, and that count feeds the
  // track list, so refresh the lanes too.
  if (c.value(QStringLiteral("kind")).toString() == QStringLiteral("subtitle"))
    emit tracksChanged();
  return true;
}

bool Backend::moveClips(const QStringList &clipIds, qint64 deltaMs,
                        int trackDelta) {
  QVector<int> indexes;
  for (const auto &clipId : clipIds) {
    const int index = clipIndex(clipId);
    if (index >= 0 && !indexes.contains(index))
      indexes.append(index);
  }
  if (indexes.isEmpty())
    return false;

  rememberState();
  int requiredVideoTracks = m_videoTrackCount;
  int requiredAudioTracks = m_audioTrackCount;
  QVector<QVariantMap> updates;
  updates.reserve(indexes.size());

  for (const int index : indexes) {
    auto clip = m_clips[index].toMap();
    const QString currentTrack =
        TimelinePlacement::normalizedTrack(clip.value("track").toString());
    if (trackLocked(currentTrack)) {
      setError(QStringLiteral("Unlock selected tracks before moving clips."));
      m_undo.removeLast();
      emit historyChanged();
      return false;
    }
    if (!TimelinePlacement::trackAcceptsKind(
            currentTrack, clip.value("kind").toString())) {
      setError(QStringLiteral("The selected clip is on an invalid track."));
      m_undo.removeLast();
      emit historyChanged();
      return false;
    }

    const QString destination =
        TimelinePlacement::shiftedTrack(currentTrack, trackDelta);
    if (destination.isEmpty()) {
      setError(QStringLiteral("The selected clips cannot move to that track."));
      m_undo.removeLast();
      emit historyChanged();
      return false;
    }

    if (!TimelinePlacement::trackAcceptsKind(
            destination, clip.value("kind").toString())) {
      setError(
          QStringLiteral("Clips can only move between compatible tracks."));
      m_undo.removeLast();
      emit historyChanged();
      return false;
    }

    clip["startMs"] =
        qMax<qint64>(0, clip.value("startMs").toLongLong() + deltaMs);
    clip["track"] = destination;
    updates.append(clip);
    const QChar prefix = destination.at(0);
    const int nextNumber = TimelinePlacement::trackNumber(destination);
    if (prefix == 'V')
      requiredVideoTracks = qMax(requiredVideoTracks, nextNumber);
    else if (prefix == 'A')
      requiredAudioTracks = qMax(requiredAudioTracks, nextNumber);
  }

  m_videoTrackCount = requiredVideoTracks;
  m_audioTrackCount = requiredAudioTracks;
  for (int i = 0; i < indexes.size(); ++i)
    m_clips[indexes[i]] = updates[i];
  // Same as moveClip: whatever lane these clips left is now empty, and an empty
  // lane above the stack is not something to keep standing around.
  pruneEmptyTracks(true);
  markDirty();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  return true;
}
bool Backend::splitClip(const QString &clipId, qint64 pos) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  const QVariantMap target = m_clips.at(index).toMap();
  if (trackLocked(target.value("track").toString()))
    return false;
  const qint64 relative = pos - target.value("startMs").toLongLong();
  if (relative <= 0 || relative >= target.value("durationMs").toLongLong())
    return false;

  // A linked pair is cut in one action. Cutting only the clip that was clicked
  // would hand its link group to both halves, and from then on deleting either
  // half would take the partner - and the partner's other half - with it.
  // Extracted audio is not such a pair: it is an independent clip, so a cut on
  // the video leaves it alone.
  const QString group = isDetachedAudioGroup(
                            target.value("linkGroupId").toString())
                            ? QString()
                            : target.value("linkGroupId").toString();
  QVector<int> indexes{index};
  if (!group.isEmpty()) {
    for (int i = 0; i < m_clips.size(); ++i) {
      if (i == index)
        continue;
      const QVariantMap clip = m_clips.at(i).toMap();
      if (clip.value("linkGroupId").toString() != group)
        continue;
      const qint64 offset = pos - clip.value("startMs").toLongLong();
      if (offset <= 0 || offset >= clip.value("durationMs").toLongLong())
        continue;
      if (trackLocked(clip.value("track").toString()))
        continue;
      indexes.append(i);
    }
  }

  rememberState();
  // One new group for everything on the right of the cut, so each side is a pair
  // of its own.
  const QString tailGroup = group.isEmpty() ? QString() : id("link");
  // Descending: an insert must not move an index that has still to be cut.
  std::sort(indexes.begin(), indexes.end(), std::greater<int>());
  for (const int i : indexes) {
    QVariantMap head = m_clips.at(i).toMap();
    const qint64 offset = pos - head.value("startMs").toLongLong();
    const qint64 duration = head.value("durationMs").toLongLong();
    QVariantMap tail = head;
    tail["id"] = id("clip");
    tail["startMs"] = pos;
    tail["sourceInMs"] = head.value("sourceInMs").toLongLong() + offset;
    tail["durationMs"] = duration - offset;
    head["durationMs"] = offset;
    if (!tailGroup.isEmpty())
      tail["linkGroupId"] = tailGroup;
    // The tail is a new clip id, and keyframe channels are keyed by clip id, so
    // without this the right-hand piece of every cut loses all its animation -
    // invisibly, until export. The curves are copied whole rather than clipped
    // to each side: keyframe times are absolute, each piece renders only over
    // its own span, and keeping the out-of-span keys is what lets a later trim
    // that re-extends the piece bring its animation back, the way Premiere does.
    m_keyframeEngine.cloneClip(head.value("id").toString(),
                               tail.value("id").toString());
    m_clips[i] = head;
    m_clips.insert(i + 1, tail);
  }
  markDirty();
  if (m_editBatchDepth > 0) {
    m_editBatchClipsDirty = true;
  } else {
    emit clipsChanged();
    emit timelineChanged();
  }
  return true;
}
bool Backend::trimClipStart(const QString &clipId, qint64 requestedStart) {
  const int i = clipIndex(clipId);
  if (i < 0)
    return false;
  auto clip = m_clips[i].toMap();
  if (trackLocked(clip.value("track").toString()))
    return false;
  const qint64 oldStart = clip.value("startMs").toLongLong();
  const qint64 oldEnd = oldStart + clip.value("durationMs").toLongLong();
  const qint64 sourceIn = clip.value("sourceInMs").toLongLong();
  // An effect bar, an image still and a subtitle all have no source footage
  // behind them, so their head is not pinned by an in-point: the item may be
  // dragged out to the head of the sequence and back. An image is one frame
  // held for a duration and a caption is one string held for a duration, so
  // both stretch freely the way CapCut and Premiere allow.
  const QString trimKind = clip.value("kind").toString();
  const bool freeHead = trimKind == QStringLiteral("effect") ||
                        trimKind == QStringLiteral("image") ||
                        trimKind == QStringLiteral("subtitle");
  const qint64 minimumStart =
      freeHead ? 0 : qMax<qint64>(0, oldStart - sourceIn);
  const qint64 newStart = qBound(minimumStart, requestedStart, oldEnd - 1);
  if (newStart == oldStart)
    return false;
  rememberState();
  clip["startMs"] = newStart;
  if (!freeHead)
    clip["sourceInMs"] = sourceIn + newStart - oldStart;
  clip["durationMs"] = oldEnd - newStart;
  m_clips[i] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}
bool Backend::trimClipEnd(const QString &clipId, qint64 requestedEnd) {
  const int i = clipIndex(clipId);
  if (i < 0)
    return false;
  auto clip = m_clips[i].toMap();
  if (trackLocked(clip.value("track").toString()))
    return false;
  const qint64 start = clip.value("startMs").toLongLong();
  const qint64 sourceIn = clip.value("sourceInMs").toLongLong();
  // Nothing runs out at the tail of an effect bar, an image still (looped on
  // export) or a subtitle, so the tail is free for all three. Twenty-four
  // hours is not a real limit, only a stop against a runaway drag.
  const QString trimEndKind = clip.value("kind").toString();
  if (trimEndKind == QStringLiteral("effect") ||
      trimEndKind == QStringLiteral("image") ||
      trimEndKind == QStringLiteral("subtitle")) {
    const qint64 newEffectEnd = qBound<qint64>(start + 1, requestedEnd,
                                               qint64(86400000));
    if (newEffectEnd == start + clip.value("durationMs").toLongLong())
      return false;
    rememberState();
    clip["durationMs"] = newEffectEnd - start;
    // Keep the artificial source length in step so nothing downstream clamps
    // the freshly stretched still back to its old default duration.
    if (trimEndKind == QStringLiteral("image"))
      clip["sourceDurationMs"] = newEffectEnd - start;
    m_clips[i] = clip;
    markDirty();
    emit clipsChanged();
    emit timelineChanged();
    return true;
  }
  qint64 sourceDuration = clip.value("sourceDurationMs").toLongLong();
  if (sourceDuration <= 0) {
    const QVariantMap media = mediaById(clip.value("mediaId").toString());
    sourceDuration = media.value("durationMs").toLongLong();
  }
  sourceDuration =
      qMax(sourceDuration, sourceIn + clip.value("durationMs").toLongLong());
  const qint64 maximumEnd = start + qMax<qint64>(1, sourceDuration - sourceIn);
  const qint64 newEnd = qBound(start + 1, requestedEnd, maximumEnd);
  if (newEnd == start + clip.value("durationMs").toLongLong())
    return false;
  rememberState();
  clip["sourceDurationMs"] = sourceDuration;
  clip["durationMs"] = newEnd - start;
  m_clips[i] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}
bool Backend::deleteClipLeft(const QString &clipId, qint64 pos) {
  const int i = clipIndex(clipId);
  if (i < 0)
    return false;
  auto clip = m_clips[i].toMap();
  if (trackLocked(clip.value("track").toString()))
    return false;
  const qint64 start = clip.value("startMs").toLongLong();
  const qint64 end = start + clip.value("durationMs").toLongLong();
  if (pos <= start || pos >= end)
    return false;
  rememberState();
  const qint64 removed = pos - start;
  clip["startMs"] = pos;
  clip["sourceInMs"] = clip.value("sourceInMs").toLongLong() + removed;
  clip["durationMs"] = end - pos;
  m_clips[i] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}
bool Backend::deleteClipRight(const QString &clipId, qint64 pos) {
  const int i = clipIndex(clipId);
  if (i < 0)
    return false;
  auto clip = m_clips[i].toMap();
  if (trackLocked(clip.value("track").toString()))
    return false;
  const qint64 start = clip.value("startMs").toLongLong();
  const qint64 end = start + clip.value("durationMs").toLongLong();
  if (pos <= start || pos >= end)
    return false;
  rememberState();
  clip["durationMs"] = pos - start;
  m_clips[i] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}
bool Backend::removeClip(const QString &clipId) {
  return removeClips({clipId});
}

QStringList
Backend::expandedLinkedClipIds(const QStringList &clipIds) const {
  const QSet<QString> requested(clipIds.begin(), clipIds.end());
  QSet<QString> linkGroups;
  for (const auto &value : m_clips) {
    const QVariantMap clip = value.toMap();
    if (!requested.contains(clip.value("id").toString()))
      continue;
    const QString group = clip.value("linkGroupId").toString();
    // Extracted audio is deliberately left out: deleting it must not delete the
    // video it came from, and deleting the video must not take the sound the
    // user moved onto its own lane.
    if (!group.isEmpty() && !isDetachedAudioGroup(group))
      linkGroups.insert(group);
  }

  QStringList expanded;
  for (const auto &value : m_clips) {
    const QVariantMap clip = value.toMap();
    const QString clipId = clip.value("id").toString();
    const QString group = clip.value("linkGroupId").toString();
    if (requested.contains(clipId) ||
        (!group.isEmpty() && linkGroups.contains(group)))
      expanded.append(clipId);
  }
  return expanded;
}

bool Backend::removeClips(const QStringList &clipIds) {
  const QStringList removalIds = expandedLinkedClipIds(clipIds);
  // One pass to index the timeline, then a hash lookup per id. This used to call
  // clipIndex() - a linear scan of m_clips - once per id, and check membership
  // with QVector::contains, so deleting a subtitle track was two nested walks of
  // 2581 elements before any work started.
  QHash<QString, int> indexById;
  indexById.reserve(m_clips.size());
  for (int i = 0; i < m_clips.size(); ++i)
    indexById.insert(m_clips.at(i).toMap().value(QStringLiteral("id")).toString(),
                     i);
  QVector<int> indexes;
  indexes.reserve(removalIds.size());
  QSet<int> seen;
  seen.reserve(removalIds.size());
  for (const auto &clipId : removalIds) {
    const int index = indexById.value(clipId, -1);
    if (index >= 0 && !seen.contains(index)) {
      seen.insert(index);
      indexes.append(index);
    }
  }
  if (indexes.isEmpty())
    return false;
  for (const int index : indexes) {
    if (trackLocked(m_clips[index].toMap().value("track").toString())) {
      setError(QStringLiteral("Unlock selected tracks before deleting clips."));
      return false;
    }
  }
  rememberState();
  const bool selectionRemoved = removalIds.contains(m_selectedClipId);
  std::sort(indexes.begin(), indexes.end(), std::greater<int>());
  for (const int index : indexes)
    m_clips.removeAt(index);
  // Their animation channels go too, otherwise the project file keeps growing a
  // curve for every clip that ever existed.
  for (const QString &clipId : removalIds)
    m_keyframeEngine.forgetClip(clipId);
  // The reason this parameter exists: delete the last clip on a track and the
  // track goes with it.
  pruneEmptyTracks(true);
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  if (selectionRemoved) {
    m_selectedClipId.clear();
    emit selectionChanged();
    m_selectionDetailNotify.schedule();
    m_colorSettingsNotify.schedule();
  }
  return true;
}

namespace {
// The intrinsic settings that act on sound rather than on the picture. When a
// clip's audio moves to its own lane these move with it: a level or a filter set
// before the extraction would otherwise stay on a clip that no longer carries
// any audio, where nothing reads it and the change is silently lost.
const QStringList &audioEffectSettingKeys() {
  static const QStringList keys{QStringLiteral("volumeDb"),
                                QStringLiteral("volumeBypass"),
                                QStringLiteral("channelVolumeLeft"),
                                QStringLiteral("channelVolumeRight"),
                                QStringLiteral("balance"),
                                QStringLiteral("pan"),
                                QStringLiteral("noiseReduction"),
                                QStringLiteral("noiseRemoval"),
                                QStringLiteral("noiseRemovalPath"),
                                QStringLiteral("noiseRemovalInputPath"),
                                QStringLiteral("noiseRemovalStrength"),
                                QStringLiteral("highPassHz"),
                                QStringLiteral("lowPassHz"),
                                QStringLiteral("compressor")};
  return keys;
}

// Splits an effect stack by what each instance acts on. The registry owns that
// answer, so a new audio effect added there follows the sound without this
// needing to know its name.
void partitionAudioStack(const QVariantList &stack, QVariantList *audio,
                         QVariantList *rest) {
  for (const auto &value : stack) {
    const QVariantMap instance = value.toMap();
    const QVariantMap definition = EffectRegistry::definition(
        instance.value(QStringLiteral("definitionId")).toString());
    if (definition.value(QStringLiteral("mediaType")).toString() ==
        QStringLiteral("audio"))
      audio->append(instance);
    else
      rest->append(instance);
  }
}
} // namespace

QString Backend::freeAudioTrack(qint64 startMs, qint64 endMs) const {
  const int lanes = qBound(0, m_audioTrackCount, 64);
  for (int number = 1; number <= qMin(64, lanes + 1); ++number) {
    const QString track = QStringLiteral("A%1").arg(number);
    bool occupied = false;
    for (const auto &value : m_clips) {
      const QVariantMap clip = value.toMap();
      if (clip.value(QStringLiteral("track")).toString().compare(
              track, Qt::CaseInsensitive) != 0)
        continue;
      const qint64 clipStart = clip.value(QStringLiteral("startMs")).toLongLong();
      const qint64 clipEnd =
          clipStart + clip.value(QStringLiteral("durationMs")).toLongLong();
      if (clipStart < endMs && clipEnd > startMs) {
        occupied = true;
        break;
      }
    }
    if (!occupied)
      return track;
  }
  return QStringLiteral("A%1").arg(qMin(64, lanes + 1));
}

bool Backend::isDetachedAudioGroup(const QString &group) const {
  if (group.isEmpty())
    return false;
  for (const auto &value : m_clips) {
    const QVariantMap clip = value.toMap();
    if (clip.value(QStringLiteral("linkGroupId")).toString() != group)
      continue;
    if (clip.value(QStringLiteral("separateAudio")).toBool() ||
        clip.value(QStringLiteral("linkedRole")).toString() ==
            QStringLiteral("audio"))
      return true;
  }
  return false;
}

QVector<int> Backend::audioPeerIndexes(int index) const {
  QVector<int> peers;
  if (index < 0 || index >= m_clips.size())
    return peers;
  const QVariantMap clip = m_clips.at(index).toMap();
  const QString clipId = clip.value(QStringLiteral("id")).toString();
  const QString extractedFrom =
      clip.value(QStringLiteral("extractedFromClipId")).toString();
  const QString group = clip.value(QStringLiteral("linkGroupId")).toString();
  const bool detached = clip.value(QStringLiteral("separateAudio")).toBool();
  for (int i = 0; i < m_clips.size(); ++i) {
    if (i == index)
      continue;
    const QVariantMap other = m_clips.at(i).toMap();
    // The audio this video clip's sound was moved into, or the video clip this
    // audio came out of. Either direction is the same pair.
    if (detached && other.value(QStringLiteral("extractedFromClipId"))
                            .toString() == clipId) {
      peers.append(i);
      continue;
    }
    if (!extractedFrom.isEmpty() &&
        other.value(QStringLiteral("id")).toString() == extractedFrom) {
      peers.append(i);
      continue;
    }
    // Pairs saved before extracted audio became independent.
    if (!group.isEmpty() &&
        other.value(QStringLiteral("linkGroupId")).toString() == group)
      peers.append(i);
  }
  return peers;
}

QStringList Backend::extractClipAudio(const QStringList &clipIds) {
  // Vet everything first: this command either extracts or reports why not, and
  // half of a batch applied before an unusable clip is found would leave the
  // timeline in a state the undo entry does not describe.
  QVector<int> targets;
  for (const QString &clipId : clipIds) {
    const int index = clipIndex(clipId);
    if (index < 0)
      continue;
    const QVariantMap clip = m_clips.at(index).toMap();
    if (clip.value(QStringLiteral("kind")).toString() !=
            QStringLiteral("video") ||
        clip.value(QStringLiteral("separateAudio")).toBool())
      continue;
    if (mediaById(clip.value(QStringLiteral("mediaId")).toString())
            .value(QStringLiteral("channels"))
            .toInt() <= 0)
      continue;
    if (trackLocked(clip.value(QStringLiteral("track")).toString())) {
      setError(QStringLiteral("Unlock the track before extracting its audio."));
      return {};
    }
    targets.append(index);
  }
  if (targets.isEmpty()) {
    setError(QStringLiteral(
        "Select a video clip with an audio stream to extract."));
    return {};
  }

  rememberState();
  QStringList addedIds;
  int highestLane = 0;
  for (const int index : targets) {
    QVariantMap video = m_clips.at(index).toMap();
    const qint64 startMs = video.value(QStringLiteral("startMs")).toLongLong();
    const qint64 durationMs =
        qMax<qint64>(1, video.value(QStringLiteral("durationMs")).toLongLong());
    // Appended clips are visible to this, so extracting two clips that overlap
    // in time puts the second one on the next lane instead of on top of the
    // first.
    const QString track = freeAudioTrack(startMs, startMs + durationMs);
    const QString sourceClipId = video.value(QStringLiteral("id")).toString();

    QVariantMap videoEffects = video.value(QStringLiteral("effects")).toMap();
    QVariantMap audioEffects;
    for (const QString &key : audioEffectSettingKeys()) {
      if (!videoEffects.contains(key))
        continue;
      audioEffects[key] = videoEffects.take(key);
    }
    // Vocal separation is mirrored onto the extracted clip by the Demucs
    // handlers, so both keep it rather than the video losing the state its own
    // Effect Controls shows.
    for (const QString &key :
         {QStringLiteral("vocalRemoval"), QStringLiteral("demucsPath")}) {
      if (videoEffects.contains(key))
        audioEffects[key] = videoEffects.value(key);
    }
    QVariantList audioStack;
    QVariantList videoStack;
    partitionAudioStack(video.value(QStringLiteral("effectStack")).toList(),
                        &audioStack, &videoStack);

    QVariantMap audio{
        {QStringLiteral("id"), id("clip")},
        {QStringLiteral("mediaId"), video.value(QStringLiteral("mediaId"))},
        {QStringLiteral("name"), video.value(QStringLiteral("name"))},
        {QStringLiteral("kind"), QStringLiteral("audio")},
        {QStringLiteral("track"), track},
        {QStringLiteral("startMs"), startMs},
        {QStringLiteral("sourceInMs"), video.value(QStringLiteral("sourceInMs"))},
        {QStringLiteral("sourceDurationMs"),
         video.value(QStringLiteral("sourceDurationMs"))},
        {QStringLiteral("durationMs"), durationMs},
        {QStringLiteral("enabled"), video.value(QStringLiteral("enabled"), true)},
        // Which clip it came out of, and nothing more. It is deliberately not a
        // link group: extracted audio is an independent clip the way CapCut's
        // detached audio is, so deleting or moving it leaves the video alone.
        // This id only says where a "Restore Clip Audio" puts it back, and which
        // A-track clip a sound setting on the video belongs to.
        {QStringLiteral("extractedFromClipId"), sourceClipId}};
    if (!audioEffects.isEmpty())
      audio[QStringLiteral("effects")] = audioEffects;
    if (!audioStack.isEmpty())
      audio[QStringLiteral("effectStack")] = audioStack;

    if (videoEffects.isEmpty())
      video.remove(QStringLiteral("effects"));
    else
      video[QStringLiteral("effects")] = videoEffects;
    if (videoStack.isEmpty())
      video.remove(QStringLiteral("effectStack"));
    else
      video[QStringLiteral("effectStack")] = videoStack;
    video[QStringLiteral("separateAudio")] = true;
    video.remove(QStringLiteral("embeddedAudio"));

    m_clips[index] = video;
    m_clips.append(audio);
    highestLane = qMax(highestLane, TimelinePlacement::trackNumber(track));
    addedIds.append(audio.value(QStringLiteral("id")).toString());
  }

  // Once, after the loop: ensureTrackExists emits tracksChanged, and a batch
  // that opened two lanes would otherwise publish a track count that does not
  // cover the clips already appended.
  if (highestLane > 0)
    ensureTrackExists(QStringLiteral("A%1").arg(highestLane));
  markDirty();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  if (!addedIds.isEmpty())
    setSelectedClipId(addedIds.first());
  return addedIds;
}

bool Backend::restoreClipAudio(const QStringList &clipIds) {
  // Either half identifies the pair, because the command is reached from the
  // context menu of whichever clip was right-clicked: a video clip whose audio
  // was extracted, or the extracted audio clip itself.
  QSet<QString> videoIds;
  for (const QString &clipId : clipIds) {
    const int index = clipIndex(clipId);
    if (index < 0)
      continue;
    const QVariantMap clip = m_clips.at(index).toMap();
    const QString extractedFrom =
        clip.value(QStringLiteral("extractedFromClipId")).toString();
    if (!extractedFrom.isEmpty())
      videoIds.insert(extractedFrom);
    else if (clip.value(QStringLiteral("separateAudio")).toBool())
      videoIds.insert(clip.value(QStringLiteral("id")).toString());
    // Legacy pairs, from before extracted audio became an independent clip.
    const QString group = clip.value(QStringLiteral("linkGroupId")).toString();
    if (!group.isEmpty()) {
      for (const auto &value : m_clips) {
        const QVariantMap other = value.toMap();
        if (other.value(QStringLiteral("linkGroupId")).toString() == group &&
            other.value(QStringLiteral("separateAudio")).toBool())
          videoIds.insert(other.value(QStringLiteral("id")).toString());
      }
    }
  }
  if (videoIds.isEmpty())
    return false;

  // The settings that moved out with the sound come back, so a level set on the
  // extracted lane is not lost by putting it back.
  QHash<QString, QVariantMap> audioEffectsByVideo;
  QHash<QString, QVariantList> audioStackByVideo;
  QVector<int> removeIndexes;
  for (int i = 0; i < m_clips.size(); ++i) {
    const QVariantMap clip = m_clips.at(i).toMap();
    if (clip.value(QStringLiteral("kind")).toString() !=
        QStringLiteral("audio"))
      continue;
    QString owner = clip.value(QStringLiteral("extractedFromClipId")).toString();
    if (owner.isEmpty()) {
      const QString group = clip.value(QStringLiteral("linkGroupId")).toString();
      if (group.isEmpty())
        continue;
      for (const auto &value : m_clips) {
        const QVariantMap other = value.toMap();
        if (other.value(QStringLiteral("linkGroupId")).toString() == group &&
            other.value(QStringLiteral("separateAudio")).toBool())
          owner = other.value(QStringLiteral("id")).toString();
      }
    }
    if (owner.isEmpty() || !videoIds.contains(owner))
      continue;
    audioEffectsByVideo.insert(owner, clip.value(QStringLiteral("effects")).toMap());
    audioStackByVideo.insert(owner,
                             clip.value(QStringLiteral("effectStack")).toList());
    removeIndexes.append(i);
  }

  rememberState();
  for (int i = 0; i < m_clips.size(); ++i) {
    QVariantMap clip = m_clips.at(i).toMap();
    const QString clipId = clip.value(QStringLiteral("id")).toString();
    if (!videoIds.contains(clipId))
      continue;
    QVariantMap effects = clip.value(QStringLiteral("effects")).toMap();
    const QVariantMap recovered = audioEffectsByVideo.value(clipId);
    for (auto it = recovered.cbegin(); it != recovered.cend(); ++it)
      effects[it.key()] = it.value();
    QVariantList stack = clip.value(QStringLiteral("effectStack")).toList();
    stack.append(audioStackByVideo.value(clipId));
    if (!effects.isEmpty())
      clip[QStringLiteral("effects")] = effects;
    if (!stack.isEmpty())
      clip[QStringLiteral("effectStack")] = stack;
    clip.remove(QStringLiteral("separateAudio"));
    clip.remove(QStringLiteral("linkGroupId"));
    clip.remove(QStringLiteral("linkedRole"));
    m_clips[i] = clip;
  }

  std::sort(removeIndexes.begin(), removeIndexes.end(), std::greater<int>());
  bool selectionRemoved = false;
  for (const int index : removeIndexes) {
    const QString removedId =
        m_clips.at(index).toMap().value(QStringLiteral("id")).toString();
    selectionRemoved = selectionRemoved || removedId == m_selectedClipId;
    m_keyframeEngine.forgetClip(removedId);
    m_clips.removeAt(index);
  }
  pruneEmptyTracks(true);
  markDirty();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  if (selectionRemoved) {
    m_selectedClipId.clear();
    emit selectionChanged();
    m_selectionDetailNotify.schedule();
    m_colorSettingsNotify.schedule();
  }
  return true;
}

QString Backend::addTrack(const QString &kind, bool sticky) {
  const bool video = kind.compare("video", Qt::CaseInsensitive) == 0;
  const bool audio = kind.compare("audio", Qt::CaseInsensitive) == 0;
  if (!video && !audio)
    return {};

  int &count = video ? m_videoTrackCount : m_audioTrackCount;
  if (count >= 64) {
    setError(
        QStringLiteral("A sequence supports up to 64 tracks of each type."));
    return {};
  }

  rememberState();
  ++count;
  // Only a track the user asked for by name gets a floor. The lane a clip drag
  // opens up at the top of the stack is not one: it exists to catch that clip,
  // so occupancy owns it and an aborted drag leaves nothing behind.
  if (sticky)
    (video ? m_minVideoTracks : m_minAudioTracks) = count;
  markDirty();
  emit tracksChanged();
  emit timelineChanged();
  return QString(video ? "V%1" : "A%1").arg(count);
}

bool Backend::removeLastTrack(const QString &kind) {
  const bool video = kind.compare("video", Qt::CaseInsensitive) == 0;
  const bool audio = kind.compare("audio", Qt::CaseInsensitive) == 0;
  if (!video && !audio)
    return false;

  int &count = video ? m_videoTrackCount : m_audioTrackCount;
  // V1 always exists; audio can go back to having no lane at all.
  if (count <= (video ? 1 : 0))
    return false;

  const QString track = QString(video ? "V%1" : "A%1").arg(count);
  for (const auto &value : m_clips) {
    if (value.toMap().value("track").toString() == track) {
      setError(QStringLiteral("Move or delete clips on %1 before removing it.")
                   .arg(track));
      return false;
    }
  }

  rememberState();
  m_mutedTracks.removeAll(track);
  m_trackStates.remove(track);
  --count;
  (video ? m_minVideoTracks : m_minAudioTracks) = count;
  markDirty();
  emit tracksChanged();
  emit timelineChanged();
  return true;
}

QString Backend::trackForRow(int row) const {
  return TimelinePlacement::trackForRow(row, m_videoTrackCount,
                                        m_audioTrackCount);
}

QString Backend::compatibleTrackFor(const QString &kind,
                                    const QString &requestedTrack) const {
  const QString resolved = TimelinePlacement::nearestCompatibleTrack(
      kind, requestedTrack, m_videoTrackCount, m_audioTrackCount);
  return resolved.isEmpty() ? TimelinePlacement::defaultTrackForKind(kind)
                            : resolved;
}

bool Backend::setTrackMuted(const QString &value, bool muted) {
  const QString track = value.trimmed().toUpper();
  static const QRegularExpression pattern(QStringLiteral("^([VA])(\\d+)$"));
  const auto match = pattern.match(track);
  if (!match.hasMatch())
    return false;

  const int number = match.captured(2).toInt();
  const int count = match.captured(1) == QStringLiteral("V")
                        ? m_videoTrackCount
                        : m_audioTrackCount;
  if (number < 1 || number > count)
    return false;

  const bool currentlyMuted = m_mutedTracks.contains(track);
  if (currentlyMuted == muted)
    return true;

  rememberState();
  if (muted)
    m_mutedTracks.append(track);
  else
    m_mutedTracks.removeAll(track);
  markDirty();
  emit tracksChanged();
  return true;
}

int Backend::subtitleTrackCount() const {
  // QML sequence wrappers can call trackStates() again for each field read.
  // Recounting every clip here made those reads dominate a busy timeline drag.
  ensureClipCaches();
  return m_cachedSubtitleTrackCount;
}

QVariantList Backend::trackStates() const {
  QVariantList result;
  // Text lanes sit at the very top, highest-numbered first, so a clip dragged
  // "up" to a new row lands above the ones already there - like CapCut.
  for (int number = subtitleTrackCount(); number >= 1; --number)
    result.append(trackState(QStringLiteral("S%1").arg(number)));
  result.append(trackState(QStringLiteral("F1")));
  for (int number = m_videoTrackCount; number >= 1; --number)
    result.append(trackState(QStringLiteral("V%1").arg(number)));
  for (int number = 1; number <= m_audioTrackCount; ++number)
    result.append(trackState(QStringLiteral("A%1").arg(number)));
  return result;
}

QVariantMap Backend::trackState(const QString &value) const {
  const QString track = value.trimmed().toUpper();
  QVariantMap state = defaultTrackState(track);
  const QVariantMap saved = m_trackStates.value(track).toMap();
  for (auto it = saved.constBegin(); it != saved.constEnd(); ++it)
    state[it.key()] = it.value();
  state["muted"] = m_mutedTracks.contains(track);
  return state;
}

bool Backend::trackLocked(const QString &track) const {
  return trackState(track).value("locked").toBool();
}
bool Backend::trackVisible(const QString &track) const {
  return trackState(track).value("visible", true).toBool();
}
bool Backend::trackSolo(const QString &track) const {
  return trackState(track).value("solo").toBool();
}
bool Backend::trackSyncLocked(const QString &track) const {
  return trackState(track).value("syncLocked", true).toBool();
}
bool Backend::trackTargeted(const QString &track) const {
  return trackState(track).value("targeted", true).toBool();
}

bool Backend::setTrackState(const QString &value, const QString &stateName,
                            bool enabled) {
  const QString track = value.trimmed().toUpper();
  static const QRegularExpression pattern(QStringLiteral("^([VASF])(\\d+)$"));
  const auto match = pattern.match(track);
  if (!match.hasMatch() ||
      ((match.captured(1) == QStringLiteral("S") ||
        match.captured(1) == QStringLiteral("F")) &&
       match.captured(2) != QStringLiteral("1")))
    return false;
  const int number = match.captured(2).toInt();
  if ((track.startsWith('V') && number > m_videoTrackCount) ||
      (track.startsWith('A') && number > m_audioTrackCount))
    return false;
  static const QSet<QString> allowed{"visible", "locked", "syncLocked",
                                     "targeted", "solo"};
  if (!allowed.contains(stateName))
    return false;
  QVariantMap state = trackState(track);
  if (state.value(stateName).toBool() == enabled)
    return true;
  rememberState();
  state.remove("muted");
  state[stateName] = enabled;
  m_trackStates[track] = state;
  markDirty();
  emit tracksChanged();
  emit timelineChanged();
  return true;
}

qint64 Backend::snapTime(qint64 requestedMs, const QStringList &excludedClipIds,
                         qint64 thresholdMs) const {
  if (!m_snappingEnabled)
    return qMax<qint64>(0, requestedMs);
  return TimelineEditor::snapTime(requestedMs, m_clips, m_markers,
                                  excludedClipIds, m_playheadMs,
                                  qMax<qint64>(0, thresholdMs), durationMs());
}

QVariantMap Backend::snapClipDrag(qint64 startMs, qint64 clipDurationMs,
                                  const QStringList &excludedClipIds,
                                  qint64 thresholdMs) const {
  const qint64 requested = qMax<qint64>(0, startMs);
  QVariantMap result{{QStringLiteral("snapped"), false},
                     {QStringLiteral("startMs"), requested},
                     {QStringLiteral("guideMs"), requested},
                     {QStringLiteral("edge"), QString()},
                     {QStringLiteral("target"), QString()}};
  if (!m_snappingEnabled)
    return result;
  const TimelineEditor::SnapResult snap = TimelineEditor::snapClip(
      requested, qMax<qint64>(0, clipDurationMs), m_clips, m_markers,
      excludedClipIds, m_playheadMs, qMax<qint64>(0, thresholdMs),
      durationMs());
  if (!snap.snapped)
    return result;
  result[QStringLiteral("snapped")] = true;
  result[QStringLiteral("startMs")] = qMax<qint64>(0, requested + snap.deltaMs);
  result[QStringLiteral("guideMs")] = snap.guideMs;
  result[QStringLiteral("edge")] = snap.edge;
  result[QStringLiteral("target")] = snap.target;
  return result;
}

bool Backend::rippleDeleteClips(const QStringList &clipIds) {
  const QStringList removalIds = expandedLinkedClipIds(clipIds);
  if (removalIds.isEmpty())
    return false;
  for (const auto &clipId : removalIds) {
    const int index = clipIndex(clipId);
    if (index >= 0 &&
        trackLocked(m_clips[index].toMap().value("track").toString()))
      return false;
  }
  QStringList syncLockedTracks;
  for (const auto &stateValue : trackStates()) {
    const auto state = stateValue.toMap();
    if (state.value("syncLocked", true).toBool())
      syncLockedTracks.append(state.value("id").toString());
  }
  QVariantList updated = m_clips;
  if (!TimelineEditor::rippleDelete(updated, removalIds, syncLockedTracks))
    return false;
  rememberState();
  const bool selectionRemoved = removalIds.contains(m_selectedClipId);
  m_clips = updated;
  pruneEmptyTracks(true);
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  if (selectionRemoved) {
    m_selectedClipId.clear();
    emit selectionChanged();
    m_selectionDetailNotify.schedule();
    m_colorSettingsNotify.schedule();
  }
  return true;
}

bool Backend::rippleTrimClipEnd(const QString &clipId, qint64 endMs) {
  const int index = clipIndex(clipId);
  if (index < 0 ||
      trackLocked(m_clips[index].toMap().value("track").toString()))
    return false;
  QStringList syncLockedTracks;
  for (const auto &stateValue : trackStates()) {
    const auto state = stateValue.toMap();
    if (state.value("syncLocked", true).toBool())
      syncLockedTracks.append(state.value("id").toString());
  }
  QVariantList updated = m_clips;
  if (!TimelineEditor::rippleTrimEnd(updated, clipId, endMs, syncLockedTracks))
    return false;
  rememberState();
  m_clips = updated;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}

bool Backend::closeGap(const QString &track, qint64 startMs, qint64 endMs) {
  if (trackLocked(track))
    return false;
  QVariantList updated = m_clips;
  if (!TimelineEditor::closeGap(updated, track.toUpper(), startMs, endMs))
    return false;
  rememberState();
  m_clips = updated;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}

QVariantMap Backend::gapAt(const QString &track, qint64 positionMs) const {
  const TimelineEditor::GapResult gap =
      TimelineEditor::gapAt(m_clips, track.toUpper(), positionMs);
  // A locked track reports no gap at all rather than one that cannot be closed:
  // the menu is built from this answer, and offering an edit the lock will refuse
  // is worse than not offering it.
  if (!gap.found || trackLocked(track))
    return {};
  return {{"found", true},
          {"startMs", gap.startMs},
          {"endMs", gap.endMs},
          {"durationMs", gap.endMs - gap.startMs}};
}

int Backend::closeAllGaps(const QString &track) {
  if (trackLocked(track))
    return 0;
  QVariantList updated = m_clips;
  const int closed = TimelineEditor::closeAllGaps(updated, track.toUpper());
  if (closed <= 0)
    return 0;
  // One history entry for the whole sweep: undoing "close all gaps" should put
  // the track back the way it was, not one gap at a time.
  rememberState();
  m_clips = updated;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return closed;
}

namespace {

// A clip's speed as a timeline-ms -> source-ms multiplier. The stored effects
// map may be partial, so an absent key falls back to the registered default
// rather than to zero.
double clipSpeedFactor(const QVariantMap &clip) {
  const QVariant stored =
      clip.value("effects").toMap().value(QStringLiteral("speed"));
  const double percent =
      stored.isValid()
          ? stored.toDouble()
          : ClipEffects::defaults().value(QStringLiteral("speed")).toDouble();
  return qMax(0.01, percent / 100.0);
}

// One clip's worth of scan parameters, copied by value so the scan can run on a
// worker thread without touching the clip model.
struct SilenceScanInput {
  QString path;
  qint64 sourceInMs = 0;
  qint64 clipStartMs = 0;
  qint64 clipDurationMs = 0;
  double factor = 1.0; // clip speed: timeline ms -> source ms
  double thresholdDb = -32.0;
  qint64 minSilenceMs = 500;
  qint64 paddingMs = 100;
  bool useAverage = false;
};

struct SilenceScanOutput {
  QVector<QPair<qint64, qint64>> intervals; // timeline coords
  bool ok = false;
  bool cancelled = false;
  QString error;
};

// Peak envelope over the span of source the clip actually uses, re-binned onto a
// fixed grid, then reduced to the runs that sit below the threshold for long
// enough to be worth cutting.
//
// The grid is in *source* time. The clip's speed is what converts it back to the
// timeline, and getting that wrong is not a rounding error: on a retimed clip it
// analyses one span and cuts another.
SilenceScanOutput
scanSilence(const SilenceScanInput &in, const std::atomic_bool *cancel,
            const std::function<void(double)> &onProgress) {
  SilenceScanOutput out;
  constexpr qint64 kColumnMs = 25;
  const qint64 sourceSpan =
      qMax<qint64>(1, qRound64(in.clipDurationMs * in.factor));
  const qint64 tileSpan = AudioPeakWindowService::kFullDecodeLimitMs;
  const double threshold =
      std::pow(10.0, qBound(-90.0, in.thresholdDb, -1.0) / 20.0);
  auto &peaks = AudioPeakWindowService::instance();

  QVector<float> envelope;
  envelope.reserve(int(sourceSpan / kColumnMs) + 2);
  for (qint64 offset = 0; offset < sourceSpan; offset += tileSpan) {
    if (cancel && cancel->load()) {
      out.cancelled = true;
      return out;
    }
    const qint64 span = qMin(tileSpan, sourceSpan - offset);
    const int columns =
        int(qBound<qint64>(qint64(32), span / kColumnMs, qint64(2048)));
    const AudioPeakWindowService::Window window =
        peaks.window(in.path, in.sourceInMs + offset, span, columns, cancel);
    if (window.cancelled) {
      out.cancelled = true;
      return out;
    }
    // A tile that would not decode is not silence. Filling it with zeros - which
    // is what this did - classified the whole tile as silent and deleted up to
    // six seconds of dialogue over a single flaky seek, with nothing to show the
    // user it had happened. Refuse the scan instead.
    if (!window.valid()) {
      out.error =
          QCoreApplication::translate(
              "Backend",
              "The audio could not be read %1:%2 into this clip, so the scan "
              "was stopped rather than risk cutting good audio. The source may "
              "be damaged or still downloading.")
              .arg((in.sourceInMs + offset) / 60000)
              .arg(((in.sourceInMs + offset) / 1000) % 60, 2, 10, QChar('0'));
      return out;
    }
    const int grid = int(qMax<qint64>(1, span / kColumnMs));
    const int pn = window.peaks.size();
    for (int g = 0; g < grid; ++g) {
      const int lo = int(qint64(g) * pn / grid);
      const int hi = qMin(pn, qMax(lo + 1, int(qint64(g + 1) * pn / grid)));
      float level = 0.0f;
      if (in.useAverage) {
        double sum = 0.0;
        for (int c = lo; c < hi; ++c)
          sum += double(window.peaks.at(c));
        level = float(sum / double(qMax(1, hi - lo)));
      } else {
        for (int c = lo; c < hi; ++c)
          level = qMax(level, window.peaks.at(c));
      }
      envelope.append(level);
    }
    if (onProgress)
      onProgress(double(offset + span) / double(sourceSpan));
  }

  // Grid index (source) -> absolute timeline position inside the clip.
  const auto timelineAt = [&](qint64 gridIndex) {
    return in.clipStartMs +
           qRound64((double(gridIndex) * double(kColumnMs)) / in.factor);
  };

  const int n = envelope.size();
  int runStart = -1;
  const auto flush = [&](int runEndExclusive) {
    if (runStart < 0)
      return;
    qint64 a = timelineAt(runStart);
    qint64 b = timelineAt(runEndExclusive);
    if (b - a >= in.minSilenceMs) {
      a += in.paddingMs;
      b -= in.paddingMs;
      if (b - a > 0)
        out.intervals.append({a, b});
    }
    runStart = -1;
  };
  for (int i = 0; i < n; ++i) {
    const bool silent = envelope.at(i) <= threshold;
    if (silent && runStart < 0)
      runStart = i;
    else if (!silent)
      flush(i);
  }
  flush(n);
  out.ok = true;
  return out;
}

} // namespace

int Backend::removeSilence(const QString &clipId, double thresholdDb,
                           qint64 minSilenceMs, qint64 paddingMs) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return 0;
  const QVariantMap clip = m_clips.at(index).toMap();
  if (trackLocked(clip.value("track").toString()))
    return 0;

  const QString path =
      mediaById(clip.value("mediaId").toString()).value("path").toString();
  const qint64 clipDuration = clip.value("durationMs").toLongLong();
  if (path.isEmpty() || clipDuration <= 0 ||
      !AudioPeakWindowService::available()) {
    setError(tr("This clip has no audio to scan for silence."));
    return -1;
  }

  SilenceScanInput in;
  in.path = path;
  in.sourceInMs = clip.value("sourceInMs").toLongLong();
  in.clipStartMs = clip.value("startMs").toLongLong();
  in.clipDurationMs = clipDuration;
  in.factor = clipSpeedFactor(clip);
  in.thresholdDb = thresholdDb;
  in.minSilenceMs = minSilenceMs;
  in.paddingMs = paddingMs;

  const SilenceScanOutput out = scanSilence(in, nullptr, {});
  if (!out.ok) {
    setError(out.error.isEmpty()
                 ? tr("This clip has no audio to scan for silence.")
                 : out.error);
    return -1;
  }
  if (out.intervals.isEmpty()) {
    setError(tr("No silences found in this clip."));
    return 0;
  }
  m_silenceClipId = clipId;
  m_silenceCandidates = silenceIntervalsToVariant(out.intervals);
  emit silenceCandidatesChanged();
  return applySilenceRemoval(clipId);
}

QVariantList Backend::silenceIntervalsToVariant(
    const QVector<QPair<qint64, qint64>> &intervals) {
  QVariantList list;
  list.reserve(intervals.size());
  for (const auto &interval : intervals) {
    list.append(QVariantMap{{"startMs", interval.first},
                            {"endMs", interval.second},
                            {"durationMs", interval.second - interval.first}});
  }
  return list;
}

bool Backend::analyzeSilence(const QString &clipId, double thresholdDb,
                             qint64 minSilenceMs, qint64 paddingMs,
                             bool useAverage) {
  if (m_silenceScanning)
    return false;
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  const QVariantMap clip = m_clips.at(index).toMap();
  const QString path =
      mediaById(clip.value("mediaId").toString()).value("path").toString();
  const qint64 clipDuration = clip.value("durationMs").toLongLong();
  if (path.isEmpty() || clipDuration <= 0 ||
      !AudioPeakWindowService::available()) {
    m_silenceStatus = tr("This clip has no audio to scan for silence.");
    emit silenceScanChanged();
    emit silenceScanFinished(false, 0, m_silenceStatus);
    return false;
  }

  SilenceScanInput in;
  in.path = path;
  in.sourceInMs = clip.value("sourceInMs").toLongLong();
  in.clipStartMs = clip.value("startMs").toLongLong();
  in.clipDurationMs = clipDuration;
  in.factor = clipSpeedFactor(clip);
  in.thresholdDb = thresholdDb;
  in.minSilenceMs = minSilenceMs;
  in.paddingMs = paddingMs;
  in.useAverage = useAverage;

  m_silenceCancel->store(false);
  m_silenceScanning = true;
  m_silenceProgress = 0.0;
  m_silenceClipId = clipId;
  m_silenceStatus = tr("Scanning audio...");
  m_silenceCandidates.clear();
  const int generation = ++m_silenceScanGeneration;
  emit silenceScanChanged();
  emit silenceCandidatesChanged();

  // The scan decodes the clip's whole used span tile by tile. On the GUI thread
  // that froze the app for minutes, which is why it used to refuse any clip over
  // twelve minutes outright; on a worker there is no reason to refuse at all.
  const auto cancel = m_silenceCancel;
  auto *watcher = new QFutureWatcher<SilenceScanOutput>(this);
  connect(watcher, &QFutureWatcher<SilenceScanOutput>::progressValueChanged, this,
          [this, generation](int value) {
    if (generation == m_silenceScanGeneration) {
      m_silenceProgress = qBound(0.0, value / 1000.0, 1.0);
      emit silenceScanChanged();
    }
  });
  connect(watcher, &QFutureWatcher<SilenceScanOutput>::finished, this,
          [this, watcher, generation]() {
    const SilenceScanOutput out = watcher->result();
    watcher->deleteLater();
    finishSilenceScan(generation, out.ok, out.cancelled, out.error, out.intervals);
  });
  // The worker owns immutable input and its cancellation token. Destroying the
  // editor disconnects the watcher without leaving a worker holding Backend.
  watcher->setFuture(QtConcurrent::run([in, cancel](QPromise<SilenceScanOutput> &promise) {
    promise.setProgressRange(0, 1000);
    promise.addResult(scanSilence(in, cancel.get(), [&promise](double fraction) {
      promise.setProgressValue(qRound(fraction * 1000));
    }));
  }));
  return true;
}

void Backend::finishSilenceScan(int generation, bool ok, bool cancelled,
                                const QString &error,
                                const QVector<QPair<qint64, qint64>> &found) {
  // A scan the user restarted or cancelled must not land on top of the current
  // one's results.
  if (generation != m_silenceScanGeneration)
    return;
  m_silenceScanning = false;
  if (m_silenceBatchActive) {
    if (cancelled || m_silenceCancel->load()) {
      m_silenceQueue.clear();
      m_silenceBatchCandidates.clear();
      m_silenceBatchError = tr("Scan cancelled");
    } else if (ok) {
      const QVariantMap snapshot = m_silenceSnapshots.value(m_silenceClipId);
      for (const auto &interval : found) {
        m_silenceBatchCandidates.append(QVariantMap{
            {"clipId", m_silenceClipId}, {"startMs", interval.first},
            {"endMs", interval.second}, {"durationMs", interval.second - interval.first},
            {"reviewStartMs", snapshot.value("reviewOffsetMs").toLongLong() +
                              interval.first - snapshot.value("startMs").toLongLong()}});
      }
    } else {
      m_silenceBatchError = error.isEmpty() ? tr("A selected clip could not be scanned.") : error;
    }
    QTimer::singleShot(0, this, &Backend::pumpSilenceQueue);
    return;
  }

  m_silenceProgress = 0.0;
  if (cancelled) {
    m_silenceStatus = tr("Scan cancelled");
    emit silenceScanChanged();
    emit silenceScanFinished(false, 0, tr("Cancelled"));
    return;
  }
  if (!ok) {
    m_silenceStatus = error.isEmpty()
                          ? tr("The audio could not be scanned.")
                          : error;
    emit silenceScanChanged();
    emit silenceScanFinished(false, 0, m_silenceStatus);
    return;
  }
  m_silenceCandidates = silenceIntervalsToVariant(found);
  if (found.isEmpty())
    m_silenceStatus = tr("No silences found at this threshold.");
  else
    m_silenceStatus = tr("%n silent stretch(es) found", "", found.size());
  emit silenceScanChanged();
  emit silenceCandidatesChanged();
  emit silenceScanFinished(true, found.size(), QString());
}

void Backend::cancelSilenceScan() {
  if (!silenceScanInProgress())
    return;
  m_silenceCancel->store(true);
  m_silenceQueue.clear();
}

void Backend::clearSilenceCandidates() {
  if (m_silenceCandidates.isEmpty() && m_silenceClipId.isEmpty())
    return;
  m_silenceCandidates.clear();
  m_silenceClipId.clear();
  if (!m_silenceBatchActive) {
    m_silenceSnapshots.clear();
    m_silenceBatchCandidates.clear();
  }
  m_silenceStatus.clear();
  emit silenceCandidatesChanged();
  emit silenceScanChanged();
}

qint64 Backend::silenceCandidateTotalMs() const {
  qint64 total = 0;
  for (const QVariant &value : m_silenceCandidates)
    total += value.toMap().value("durationMs").toLongLong();
  return total;
}

int Backend::applySilenceRemoval(const QString &clipId) {
  if (m_silenceCandidates.isEmpty() || m_silenceClipId != clipId)
    return 0;
  const int index = clipIndex(clipId);
  if (index < 0)
    return 0;
  const QVariantMap clip = m_clips.at(index).toMap();
  const QString track = clip.value("track").toString();
  if (trackLocked(track))
    return 0;

  QVector<QPair<qint64, qint64>> silences;
  silences.reserve(m_silenceCandidates.size());
  for (const QVariant &value : m_silenceCandidates) {
    const QVariantMap entry = value.toMap();
    const qint64 a = entry.value("startMs").toLongLong();
    const qint64 b = entry.value("endMs").toLongLong();
    if (b > a)
      silences.append({a, b});
  }
  if (silences.isEmpty())
    return 0;
  std::sort(silences.begin(), silences.end());

  const qint64 clipStart = clip.value("startMs").toLongLong();
  const qint64 clipDuration = clip.value("durationMs").toLongLong();
  const qint64 clipEnd = clipStart + clipDuration;

  // Which clips to reshape: the analysed clip, plus any real linked partner (a
  // linked A/V pair, not detached extracted audio) covering the same span, so
  // picture and sound are cut at the same times and stay together.
  const QString group =
      isDetachedAudioGroup(clip.value("linkGroupId").toString())
          ? QString()
          : clip.value("linkGroupId").toString();
  QVector<int> targetIndexes{index};
  if (!group.isEmpty()) {
    for (int i = 0; i < m_clips.size(); ++i) {
      if (i == index)
        continue;
      const QVariantMap other = m_clips.at(i).toMap();
      if (other.value("linkGroupId").toString() != group)
        continue;
      if (trackLocked(other.value("track").toString()))
        continue;
      if (other.value("startMs").toLongLong() == clipStart &&
          other.value("durationMs").toLongLong() == clipDuration)
        targetIndexes.append(i);
    }
  }

  QStringList syncLockedTracks;
  for (const auto &stateValue : trackStates()) {
    const auto state = stateValue.toMap();
    if (state.value("syncLocked", true).toBool() &&
        !state.value("locked").toBool())
      syncLockedTracks.append(state.value("id").toString());
  }

  rememberState();

  // A shared new link id per surviving segment, so the pieces of a formerly
  // linked pair stay linked to each other after the cut.
  QVector<QString> segmentGroups;
  // Descending: an in-place splice must not move an index still to be processed.
  std::sort(targetIndexes.begin(), targetIndexes.end(), std::greater<int>());
  for (const int ti : targetIndexes) {
    const QVariantMap original = m_clips.at(ti).toMap();
    const QString originalId = original.value("id").toString();
    const qint64 oStart = original.value("startMs").toLongLong();
    const qint64 oSourceIn = original.value("sourceInMs").toLongLong();
    const double factor = clipSpeedFactor(original);
    // Surviving spans = the clip minus the silent intervals.
    QVector<QPair<qint64, qint64>> survivors;
    qint64 cursor = oStart;
    for (const auto &s : silences) {
      const qint64 a = qMax(oStart, s.first);
      const qint64 b = qMin(clipEnd, s.second);
      if (b <= a)
        continue;
      if (a > cursor)
        survivors.append({cursor, a});
      cursor = qMax(cursor, b);
    }
    if (cursor < clipEnd)
      survivors.append({cursor, clipEnd});

    QVariantList pieces;
    for (int k = 0; k < survivors.size(); ++k) {
      QVariantMap piece = original;
      const QString pieceId = id("clip");
      piece["id"] = pieceId;
      piece["startMs"] = survivors.at(k).first; // ripple pulls it back below
      // Timeline offset into the clip scaled back up by the clip's speed. A
      // retimed clip's source and timeline milliseconds are not the same unit,
      // and treating them as one pointed every piece at the wrong frame.
      piece["sourceInMs"] =
          oSourceIn +
          qRound64(double(survivors.at(k).first - oStart) * factor);
      piece["durationMs"] = survivors.at(k).second - survivors.at(k).first;
      if (!group.isEmpty()) {
        if (k >= segmentGroups.size())
          segmentGroups.append(id("link"));
        piece["linkGroupId"] = segmentGroups.at(k);
      }
      // Each piece is a new clip id, so it needs its own copy of the curves or
      // the animation vanishes with the old id.
      m_keyframeEngine.cloneClip(originalId, pieceId);
      pieces.append(piece);
    }
    // The original id is gone from the timeline; without this its channels
    // linger in the project file forever, driving nothing.
    m_keyframeEngine.forgetClip(originalId);
    m_clips.removeAt(ti);
    for (int k = pieces.size() - 1; k >= 0; --k)
      m_clips.insert(ti, pieces.at(k));
  }

  // Pull everything (the survivors included) left to close the removed spans.
  if (!m_silenceBatchApplying)
    TimelineEditor::rippleRemoveIntervals(m_clips, track, silences,
                                          syncLockedTracks);

  if (!m_silenceBatchApplying)
    pruneEmptyTracks(true);
  markDirty();
  setError(QString());
  notifyClipEdit();
  if (m_selectedClipId == clipId) {
    m_selectedClipId.clear();
    emit selectionChanged();
    m_selectionDetailNotify.schedule();
    m_colorSettingsNotify.schedule();
  }
  const int removed = int(silences.size());
  m_silenceCandidates.clear();
  m_silenceClipId.clear();
  if (!m_silenceBatchApplying)
    emit silenceCandidatesChanged();
  return removed;
}


bool Backend::analyzeClipsSilence(const QStringList &requested, double thresholdDb,
                                  qint64 minSilenceMs, qint64 paddingMs,
                                  bool useAverage) {
  if (silenceScanInProgress())
    return false;
  const QStringList ids = editableClipIds(requested, "audio");
  if (ids.isEmpty())
    return false;
  clearSilenceCandidates();
  m_silenceSnapshots.clear();
  qint64 offset = 0;
  for (const QString &id : ids) {
    QVariantMap snapshot = clipById(id);
    snapshot["reviewOffsetMs"] = offset;
    m_silenceSnapshots.insert(id, snapshot);
    offset += snapshot.value("durationMs").toLongLong();
  }
  m_silenceQueue = ids;
  m_silenceBatchCandidates.clear();
  m_silenceBatchError.clear();
  m_silenceBatchThreshold = thresholdDb;
  m_silenceBatchMinimum = minSilenceMs;
  m_silenceBatchPadding = paddingMs;
  m_silenceBatchAverage = useAverage;
  m_silenceBatchActive = true;
  m_silenceCancel->store(false);
  pumpSilenceQueue();
  return true;
}

void Backend::pumpSilenceQueue() {
  if (m_silenceCancel->load()) {
    m_silenceQueue.clear();
    m_silenceBatchCandidates.clear();
    m_silenceBatchError = tr("Scan cancelled");
  }
  while (!m_silenceQueue.isEmpty()) {
    const QString id = m_silenceQueue.takeFirst();
    if (editableClipIds({id}, "audio").isEmpty()) {
      m_silenceBatchError = tr("A selected clip is no longer available.");
      continue;
    }
    if (analyzeSilence(id, m_silenceBatchThreshold, m_silenceBatchMinimum,
                       m_silenceBatchPadding, m_silenceBatchAverage))
      return;
    m_silenceBatchError = tr("A selected clip could not be scanned.");
  }
  m_silenceBatchActive = false;
  m_silenceClipId.clear();
  m_silenceCandidates = m_silenceBatchCandidates;
  m_silenceBatchCandidates.clear();
  m_silenceProgress = 0.0;
  m_silenceStatus = m_silenceBatchError.isEmpty()
      ? (m_silenceCandidates.isEmpty() ? tr("No silences found at this threshold.")
          : tr("%1 silent stretches found in selected clips").arg(m_silenceCandidates.size()))
      : m_silenceBatchError;
  emit silenceScanChanged();
  emit silenceCandidatesChanged();
  emit silenceScanFinished(m_silenceBatchError.isEmpty(), m_silenceCandidates.size(),
                           m_silenceBatchError);
}

int Backend::applyClipsSilenceRemoval() {
  if (silenceScanInProgress() || m_silenceCandidates.isEmpty())
    return 0;
  // Review results refer to a precise source slice. Refuse stale results rather
  // than cutting a moved, trimmed, retimed, or replaced clip at old positions.
  QHash<QString, QVariantList> byClip;
  for (const QVariant &v : m_silenceCandidates) {
    const QVariantMap candidate = v.toMap();
    byClip[candidate.value("clipId").toString()].append(v);
  }
  for (auto it = byClip.cbegin(); it != byClip.cend(); ++it) {
    const QVariantMap current = clipById(it.key());
    const QVariantMap scanned = m_silenceSnapshots.value(it.key());
    if (current.isEmpty() || scanned.isEmpty() ||
        editableClipIds({it.key()}, "audio").isEmpty())
      return 0;
    for (const QString &key : {QString("mediaId"), QString("sourceInMs"),
                               QString("startMs"), QString("durationMs")}) {
      if (current.value(key) != scanned.value(key)) {
        setError(tr("The clips changed after scanning. Scan again before removing silence."));
        return 0;
      }
    }
    if (clipSpeedFactor(current) != clipSpeedFactor(scanned)) {
      setError(tr("The clip speed changed after scanning. Scan again."));
      return 0;
    }
    for (const QString &peer : expandedLinkedClipIds({it.key()})) {
      if (trackLocked(clipById(peer).value("track").toString()))
        return 0;
    }
  }
  // Reshape every clip in original coordinates first. Then close the union of
  // the intervals once per track; overlapping selections must not ripple twice.
  QHash<QString, QVector<QPair<qint64, qint64>>> cuts;
  QStringList ids = byClip.keys();
  std::sort(ids.begin(), ids.end(), [this](const QString &a, const QString &b) {
    return clipById(a).value("startMs").toLongLong() > clipById(b).value("startMs").toLongLong();
  });
  beginEditBatch();
  m_silenceBatchApplying = true;
  int removed = 0;
  for (const QString &id : ids) {
    const QVariantMap clip = clipById(id);
    if (clip.isEmpty()) // a linked partner was already reshaped
      continue;
    const QString track = clip.value("track").toString();
    const QVariantList candidates = byClip.value(id);
    m_silenceClipId = id;
    m_silenceCandidates = candidates;
    const int count = applySilenceRemoval(id);
    removed += count;
    if (count > 0) {
      for (const QVariant &v : candidates) {
        const QVariantMap candidate = v.toMap();
        cuts[track].append({candidate.value("startMs").toLongLong(),
                            candidate.value("endMs").toLongLong()});
      }
    }
  }
  for (const QVariant &v : trackStates()) {
    const QVariantMap state = v.toMap();
    const QString track = state.value("id").toString();
    if (state.value("locked").toBool())
      continue;
    QVector<QPair<qint64, qint64>> intervals = cuts.value(track);
    if (state.value("syncLocked", true).toBool()) {
      intervals.clear();
      for (auto it = cuts.cbegin(); it != cuts.cend(); ++it)
        intervals += it.value();
    }
    std::sort(intervals.begin(), intervals.end());
    QVector<QPair<qint64, qint64>> merged;
    for (const auto &interval : intervals) {
      if (!merged.isEmpty() && interval.first <= merged.last().second)
        merged.last().second = qMax(merged.last().second, interval.second);
      else
        merged.append(interval);
    }
    TimelineEditor::rippleRemoveIntervals(m_clips, track, merged, {});
  }
  m_silenceBatchApplying = false;
  pruneEmptyTracks(true);
  endEditBatch();
  clearSilenceCandidates();
  emit silenceCandidatesChanged();
  return removed;
}

QString Backend::addMarker(qint64 positionMs, const QString &name,
                           const QString &color) {
  rememberState();
  const QString markerId = id("marker");
  const QString markerColor =
      color.trimmed().isEmpty() ? QStringLiteral("#59a7ff") : color.trimmed();
  m_markers.append(
      QVariantMap{{"id", markerId},
                  {"positionMs", qBound<qint64>(0, positionMs, durationMs())},
                  {"name", name.trimmed().isEmpty() ? QStringLiteral("Marker")
                                                    : name.trimmed()},
                  {"color", markerColor}});
  markDirty();
  if (m_editBatchDepth > 0) {
    m_editBatchMarkersDirty = true;
  } else {
    emit markersChanged();
    emit timelineChanged();
  }
  return markerId;
}

bool Backend::updateMarker(const QString &markerId, qint64 positionMs,
                           const QString &name, const QString &color) {
  for (int i = 0; i < m_markers.size(); ++i) {
    auto marker = m_markers.at(i).toMap();
    if (marker.value("id").toString() != markerId)
      continue;
    rememberState();
    marker["positionMs"] = qBound<qint64>(0, positionMs, durationMs());
    marker["name"] =
        name.trimmed().isEmpty() ? QStringLiteral("Marker") : name.trimmed();
    marker["color"] =
        color.trimmed().isEmpty() ? QStringLiteral("#59a7ff") : color.trimmed();
    m_markers[i] = marker;
    markDirty();
    emit markersChanged();
    emit timelineChanged();
    return true;
  }
  return false;
}

bool Backend::removeMarker(const QString &markerId) {
  for (int i = 0; i < m_markers.size(); ++i) {
    if (m_markers.at(i).toMap().value("id").toString() != markerId)
      continue;
    rememberState();
    m_markers.removeAt(i);
    markDirty();
    emit markersChanged();
    emit timelineChanged();
    return true;
  }
  return false;
}

void Backend::setSnappingEnabled(bool enabled) {
  if (m_snappingEnabled == enabled)
    return;
  rememberState();
  m_snappingEnabled = enabled;
  markDirty();
  emit snappingChanged();
  emit timelineChanged();
}

void Backend::setEffectDragActive(bool active) {
  if (m_effectDragActive == active)
    return;
  m_effectDragActive = active;
  emit effectDragActiveChanged();
}

void Backend::setVideoFullScreen(bool active) {
  if (m_videoFullScreen == active)
    return;
  m_videoFullScreen = active;
  emit videoFullScreenChanged();
}

bool Backend::saveProject(const QString &path) {
  m_projectDatabaseSaveTimer.stop();
  // Drain any debounced save already handed to the worker (against the old,
  // pre-Save path) so it cannot land after the synchronous write below.
  m_projectSaveService.flush();
  QString target = normalizePath(path);
  if (target.isEmpty())
    target = m_projectFile;
  if (target.isEmpty() && !m_projectLocation.isEmpty())
    target = QDir(m_projectLocation).filePath(m_projectName + ".cutpro.json");
  if (target.isEmpty()) {
    setError(QStringLiteral("Choose a project file location before saving."));
    return false;
  }
  if (QFileInfo(target).suffix().isEmpty())
    target += ".cutpro.json";
  QDir().mkpath(QFileInfo(target).absolutePath());
  // The one place the indented form is worth its cost: this is the file a user
  // can open, diff and read.
  const QByteArray snapshot = serializeState(true);
  QSaveFile f(target);
  if (!f.open(QIODevice::WriteOnly) || f.write(snapshot) < 0 ||
      !f.commit()) {
    setError(QStringLiteral("Could not save project: ") + f.errorString());
    return false;
  }
  m_projectFile = target;
  m_projectLocation = QFileInfo(target).absolutePath();
  m_projectDatabaseFile = target + QStringLiteral(".sqlite");
  m_projectDatabase.close();
  m_projectDatabase.open(m_projectDatabaseFile);
  if (!m_projectDatabase.isOpen()) {
    setError(QStringLiteral("Could not open project SQLite database: ") +
             m_projectDatabase.lastError());
    return false;
  }
  if (!persistProjectDatabase())
    return false;
  m_projectDatabase.appendAction(
      m_projectId, m_sequenceId, QStringLiteral("project-save"),
      QVariantMap{{"path", target}}, snapshot);
  markDirty(false);
  emit projectChanged();
  return true;
}
bool Backend::loadProject(const QString &path) {
  m_projectDatabaseSaveTimer.stop();
  QFile f(normalizePath(path));
  if (!f.open(QIODevice::ReadOnly)) {
    setError(QStringLiteral("Could not open project: ") + f.errorString());
    return false;
  }
  // Parse the JSON file to a variant once. The SQLite snapshot (when present
  // and non-empty) is the newer, authoritative copy - autosave and the debounce
  // writer keep it ahead of the .cutpro.json file - so it wins. This used to
  // restoreState() the JSON and then, if the sqlite existed, restoreState() the
  // sqlite as well: the whole project applied twice, the first apply pure waste
  // and a second full clip/media rebuild for nothing.
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
  if (!doc.isObject()) {
    setError(QStringLiteral("Invalid project file: ") + parseError.errorString());
    return false;
  }
  QVariantMap state = doc.object().toVariantMap();

  m_projectFile = QFileInfo(f.fileName()).absoluteFilePath();
  m_projectLocation = QFileInfo(f.fileName()).absolutePath();
  m_projectDatabaseFile = m_projectFile + QStringLiteral(".sqlite");
  if (QFileInfo::exists(m_projectDatabaseFile)) {
    m_projectDatabase.close();
    if (m_projectDatabase.open(m_projectDatabaseFile)) {
      const QString projectId = state.value(QStringLiteral("projectId")).toString();
      const QVariantMap sqliteState = m_projectDatabase.loadProject(
          projectId.isEmpty() ? m_projectId : projectId);
      if (!sqliteState.isEmpty())
        state = sqliteState;
    }
  }
  if (!restoreStateVariant(state))
    return false;
  pruneEmptyTracks();
  m_undo.clear();
  m_redo.clear();
  markDirty(false);
  emitAllStateChanged();
  return true;
}

// ---- Workspace / home screen ---------------------------------------------

QString Backend::workspaceLocation() const {
  const QString stored =
      m_appSettings.value(QStringLiteral("workspaceLocation")).toString();
  return stored.isEmpty() ? AppSettings::defaultWorkspaceLocation() : stored;
}

void Backend::setWorkspaceLocation(const QString &path) {
  const QString clean = normalizePath(path);
  if (clean.isEmpty() || clean == workspaceLocation())
    return;
  QVariantMap next = m_appSettings;
  next[QStringLiteral("workspaceLocation")] = clean;
  // applyAppSettings normalizes, persists to the registry and swaps
  // m_appSettings; it only emits appSettingsChanged, so the dedicated property
  // signal is raised here for bindings that watch workspaceLocation alone.
  if (applyAppSettings(next))
    emit workspaceLocationChanged();
}

void Backend::chooseWorkspaceLocation(const QString &path) {
  setWorkspaceLocation(path);
}

QString Backend::ensureWorkspaceDir() {
  const QStringList candidates{
      workspaceLocation(),
      QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
          .filePath(QStringLiteral("Cut Pro")),
      QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
          .filePath(QStringLiteral("Cut Pro")),
      QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
          .filePath(QStringLiteral("Projects"))};
  for (const QString &candidate : candidates) {
    if (candidate.isEmpty() || !writableDirectory(candidate))
      continue;
    // Only rewrite the setting when we actually had to move: setWorkspaceLocation
    // no-ops on an unchanged path, so a healthy configured folder never triggers
    // a registry write here.
    if (QDir(candidate).absolutePath() !=
        QDir(workspaceLocation()).absolutePath())
      setWorkspaceLocation(candidate);
    return candidate;
  }
  return QString();
}

QVariantList Backend::listWorkspaceProjects() {
  // Resolve first so a broken default (e.g. an unsynced OneDrive Documents
  // path) is replaced before the home screen shows its "Saving to" line, rather
  // than the user only discovering it when Create silently does nothing.
  const QString root = ensureWorkspaceDir();
  if (root.isEmpty())
    return {};
  QDir dir(root);
  if (!dir.exists())
    return {};

  // Only the project files a user made, not the auto-save shadows: an
  // ".autosave.cutpro.json" ends in ".cutpro.json" too, so it would otherwise
  // show up as a second, phantom project beside the real one.
  const QStringList entries = dir.entryList(
      QStringList{QStringLiteral("*.cutpro.json")}, QDir::Files, QDir::Time);

  QVariantList projects;
  for (const QString &entry : entries) {
    if (entry.endsWith(QStringLiteral(".autosave.cutpro.json")))
      continue;
    const QFileInfo info(dir.filePath(entry));
    const QDateTime modified = info.lastModified();
    QString name = entry;
    name.chop(QStringLiteral(".cutpro.json").size());

    // A card thumbnail is the first media the project can show a still for: an
    // image is its own poster, a video gets a stored (or freshly seeked) frame,
    // and audio has no picture.
    //
    // Source of truth is the .sqlite mirror, not the .cutpro.json: the mirror is
    // rewritten on every edit (the 250 ms database-save timer), while the JSON is
    // only rewritten on an explicit save. A clip added in the editor therefore
    // lives in the mirror immediately but not in the JSON, so reading the JSON
    // here showed an empty card for a project that clearly had media. We load the
    // mirror's media list when it exists and fall back to the JSON only for
    // projects that never got a database (older files, or a hand-placed .json).
    QString thumbnailUrl;
    QString mediaKind;
    QVariantList media;

    const QString dbPath = info.absoluteFilePath() + QStringLiteral(".sqlite");
    if (QFileInfo::exists(dbPath)) {
      ProjectDatabase db;
      if (db.open(dbPath)) {
        const QVariantList stored = db.listProjects();
        if (!stored.isEmpty()) {
          const QString projectId =
              stored.first().toMap().value(QStringLiteral("id")).toString();
          media = db.loadProject(projectId).value(QStringLiteral("media")).toList();
        }
        db.close();
      }
    }
    if (media.isEmpty()) {
      QFile projectFile(info.absoluteFilePath());
      if (projectFile.open(QIODevice::ReadOnly)) {
        media = QJsonDocument::fromJson(projectFile.readAll())
                    .object()
                    .value(QStringLiteral("media"))
                    .toArray()
                    .toVariantList();
        projectFile.close();
      }
    }

    for (const QVariant &value : media) {
      const QVariantMap entryObj = value.toMap();
      const QString kind = entryObj.value(QStringLiteral("kind")).toString();
      if (kind != QStringLiteral("video") && kind != QStringLiteral("image"))
        continue;
      QString stored = entryObj.value(QStringLiteral("thumbnailUrl")).toString();
      if (stored.isEmpty()) {
        const QString mediaPath =
            entryObj.value(QStringLiteral("path")).toString();
        const qint64 durationMs =
            entryObj.value(QStringLiteral("durationMs")).toLongLong();
        if (kind == QStringLiteral("image")) {
          if (QFileInfo::exists(mediaPath))
            stored = QUrl::fromLocalFile(mediaPath).toString();
        } else if (QFileInfo::exists(mediaPath)) {
          stored = thumbnailForMedia(mediaPath, kind, durationMs);
        }
      }
      if (!stored.isEmpty()) {
        thumbnailUrl = stored;
        mediaKind = kind;
        break;
      }
    }
    // No still-able media but the project has audio? Tell QML so the card can
    // show an audio glyph instead of the generic film one.
    if (mediaKind.isEmpty()) {
      for (const QVariant &value : media) {
        if (value.toMap().value(QStringLiteral("kind")).toString() ==
            QStringLiteral("audio")) {
          mediaKind = QStringLiteral("audio");
          break;
        }
      }
    }

    projects.append(QVariantMap{
        {QStringLiteral("name"), name},
        {QStringLiteral("path"), info.absoluteFilePath()},
        {QStringLiteral("modified"), modified.toString(Qt::ISODate)},
        {QStringLiteral("modifiedLabel"), relativeTimeLabel(modified)},
        {QStringLiteral("sizeBytes"), info.size()},
        {QStringLiteral("thumbnailUrl"), thumbnailUrl},
        {QStringLiteral("mediaKind"), mediaKind},
    });
  }
  return projects;
}

QString Backend::createWorkspaceProject(const QString &name) {
  const QString root = ensureWorkspaceDir();
  if (root.isEmpty()) {
    setError(QStringLiteral(
        "Could not create a workspace folder anywhere writable. Choose a "
        "folder from the home screen and try again."));
    return QString();
  }

  QString base = safeFileName(name);
  if (base == QStringLiteral("Sequence 01") && name.trimmed().isEmpty())
    base = QStringLiteral("Untitled Project");

  // Never silently overwrite an existing project: a repeat "New project" gives
  // "Name", "Name 2", "Name 3" rather than clobbering the first one's file.
  QDir dir(root);
  QString candidate = base;
  int suffix = 2;
  while (QFileInfo::exists(dir.filePath(candidate + QStringLiteral(".cutpro.json"))))
    candidate = base + QStringLiteral(" ") + QString::number(suffix++);

  const QString target = dir.filePath(candidate + QStringLiteral(".cutpro.json"));
  newProject(candidate, root, true, QStringLiteral("Sequence 01"));
  if (!saveProject(target))
    return QString();
  setActiveWorkspace(QStringLiteral("Edit"));
  return target;
}

bool Backend::openWorkspaceProject(const QString &path) {
  if (!loadProject(path))
    return false;
  setActiveWorkspace(QStringLiteral("Edit"));
  return true;
}

bool Backend::deleteWorkspaceProject(const QString &path) {
  const QString target = normalizePath(path);
  if (target.isEmpty() || !target.endsWith(QStringLiteral(".cutpro.json"))) {
    setError(QStringLiteral("Not a project file: ") + path);
    return false;
  }
  // If the deleted project is the one open right now, drop it back to a clean
  // Untitled session so the editor is not left pointing at a file that is gone.
  if (QFileInfo(target).absoluteFilePath() ==
      QFileInfo(m_projectFile).absoluteFilePath())
    newProject();

  bool ok = QFile::remove(target);
  // The mirror and the auto-save shadow go with it; missing siblings are fine.
  QFile::remove(target + QStringLiteral(".sqlite"));
  QString autosave = target;
  autosave.chop(QStringLiteral(".cutpro.json").size());
  QFile::remove(autosave + QStringLiteral(".autosave.cutpro.json"));
  if (!ok)
    setError(QStringLiteral("Could not delete project: ") + target);
  return ok;
}

QString Backend::renameWorkspaceProject(const QString &path,
                                        const QString &newName) {
  const QString source = normalizePath(path);
  if (source.isEmpty() || !source.endsWith(QStringLiteral(".cutpro.json"))) {
    setError(QStringLiteral("Not a project file: ") + path);
    return QString();
  }
  const QFileInfo info(source);
  QDir dir = info.dir();
  const QString base = safeFileName(newName);
  const QString dest = dir.filePath(base + QStringLiteral(".cutpro.json"));
  if (QFileInfo(dest).absoluteFilePath() == info.absoluteFilePath())
    return source; // no change
  if (QFileInfo::exists(dest)) {
    setError(QStringLiteral("A project named \"%1\" already exists.").arg(base));
    return QString();
  }
  if (!QFile::rename(source, dest)) {
    setError(QStringLiteral("Could not rename project."));
    return QString();
  }
  // Carry the SQLite mirror across so the rename keeps the project's history;
  // the auto-save shadow is disposable, so it is simply dropped.
  QFile::rename(source + QStringLiteral(".sqlite"),
                dest + QStringLiteral(".sqlite"));
  QString oldAutosave = source;
  oldAutosave.chop(QStringLiteral(".cutpro.json").size());
  QFile::remove(oldAutosave + QStringLiteral(".autosave.cutpro.json"));

  // If the renamed project is the open one, follow it to the new path and
  // rename the project in place so the editor's title and future saves match.
  if (info.absoluteFilePath() == QFileInfo(m_projectFile).absoluteFilePath()) {
    m_projectFile = QFileInfo(dest).absoluteFilePath();
    m_projectDatabaseFile = m_projectFile + QStringLiteral(".sqlite");
    setProjectName(base);
    emit projectChanged();
  }
  return dest;
}

QVariantMap Backend::probeMedia(const QString &path,
                                bool generateDetailedPreviews) {
  QFileInfo f(normalizePath(path));
  QVariantMap r{{"path", f.absoluteFilePath()},
                {"name", f.fileName()},
                {"kind", kindFor(f)},
                {"sizeBytes", f.size()},
                {"durationMs", 0},
                {"width", 0},
                {"height", 0},
                {"frameRate", 0.0},
                {"sampleRate", 0},
                {"channels", 0},
                {"thumbnailUrl", QString()},
                {"timelineThumbnailUrl", QString()},
                // Filmstrip cell layout, read by the timeline so clip
                // thumbnails can be drawn one cell at a time at their true
                // aspect. A still is a single cell of unknown pixel size.
                {"filmstripFrames", 0},
                {"filmstripFrameWidth", 0},
                {"filmstripFrameHeight", 0},
                {"waveformUrl", QString()}};
  if (!f.exists() || !f.isFile()) {
    r["kind"] = "unknown";
    return r;
  }
  if (r.value("kind") == "image") {
    const QString imageUrl =
        QUrl::fromLocalFile(f.absoluteFilePath()).toString();
    r["thumbnailUrl"] = imageUrl;
    r["timelineThumbnailUrl"] = imageUrl;
    r["filmstripFrames"] = 1;
    LargeMediaPolicy::applyPresentationFlags(&r);
    return r;
  }
  // In-process probe first: no process to launch, no 5 second timeout to lose on
  // a multi-gigabyte source, and it carries rotation/codec detail the JSON path
  // never did. This is what makes a long clip report its real resolution and
  // frame rate instead of the zeros a killed ffprobe left behind. ffprobe stays
  // as the fallback for builds without direct FFmpeg linkage.
  bool probed = false;
  if (MediaMetadata::available()) {
    const MediaMetadata::Info info = MediaMetadata::probe(f.absoluteFilePath());
    if (info.valid) {
      info.applyTo(&r);
      probed = true;
    }
  }
  if (!probed) {
    QProcess probe;
    probe.start(ffprobe(),
                {QStringLiteral("-v"), QStringLiteral("error"),
                 QStringLiteral("-show_entries"),
                 QStringLiteral("format=duration,size:stream=codec_type,width,height,avg_frame_rate,sample_rate,channels"),
                 QStringLiteral("-of"), QStringLiteral("json"),
                 f.absoluteFilePath()});
    if (!probe.waitForFinished(5000)) {
      probe.kill();
      probe.waitForFinished(1000);
    }
    const QJsonDocument metadata =
        QJsonDocument::fromJson(probe.readAllStandardOutput());
    const QJsonObject root = metadata.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    r["durationMs"] = qRound64(
        format.value(QStringLiteral("duration")).toString().toDouble() * 1000.0);
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &streamValue : streams) {
      const QJsonObject stream = streamValue.toObject();
      const QString type = stream.value(QStringLiteral("codec_type")).toString();
      if (type == QStringLiteral("video") && r.value("width").toInt() == 0) {
        r["width"] = stream.value(QStringLiteral("width")).toInt();
        r["height"] = stream.value(QStringLiteral("height")).toInt();
        const QStringList fraction =
            stream.value(QStringLiteral("avg_frame_rate")).toString().split('/');
        if (fraction.size() == 2 && fraction.at(1).toDouble() > 0)
          r["frameRate"] = fraction.at(0).toDouble() / fraction.at(1).toDouble();
      } else if (type == QStringLiteral("audio") &&
                 r.value("channels").toInt() == 0) {
        r["sampleRate"] =
            stream.value(QStringLiteral("sample_rate")).toString().toInt();
        r["channels"] = stream.value(QStringLiteral("channels")).toInt();
      }
    }
  }
  const QString kind = r.value("kind").toString();
  const qint64 durationMs = r.value("durationMs").toLongLong();
  // Apply the policy before touching FFmpeg preview generation. Shotcut's
  // timeline can display a producer immediately without decoding a complete
  // filmstrip/waveform; doing that here is what previously made multi-hour
  // imports freeze or exhaust memory.
  LargeMediaPolicy::applyPresentationFlags(&r);
  if (r.value("largeMedia").toBool()) {
    // Import stays instant for a large source: nothing is generated here. What is
    // already on disk from an earlier session is free, though, so a re-import
    // shows its filmstrip and waveform immediately instead of waiting for the
    // deferred job to rediscover them.
    r["thumbnailUrl"] = FilmstripBuilder::cachedPoster(f.absoluteFilePath());
    r["timelineThumbnailUrl"] = QString();
    r["waveformUrl"] = QString();
    if (kind == "video") {
      const FilmstripBuilder::Result strip =
          FilmstripBuilder::cached(f.absoluteFilePath(), durationMs);
      if (strip.valid()) {
        r["timelineThumbnailUrl"] = strip.url;
        r["filmstripFrames"] = strip.cells;
        r["filmstripFrameWidth"] = strip.cellWidth;
        r["filmstripFrameHeight"] = strip.cellHeight;
      }
    }
    if (kind == "audio" || r.value("channels").toInt() > 0) {
      const AudioPeakBuilder::Result waveform =
          AudioPeakBuilder::cached(f.absoluteFilePath(), durationMs);
      if (waveform.valid())
        r["waveformUrl"] = waveform.url;
    }
    return r;
  }

  r["thumbnailUrl"] = thumbnailForMedia(f.absoluteFilePath(), kind, durationMs);
  if (!generateDetailedPreviews && kind == "video") {
    // Avoid decoding an entire long video during import. The timeline can use
    // this single cached frame until detailed preview generation is requested.
    r["timelineThumbnailUrl"] = r.value("thumbnailUrl");
    r["filmstripFrames"] = 1;
  } else if (kind == "video") {
    // Seek-sampled, like the large-media path: a short clip's strip is built
    // from one keyframe per cell instead of by walking the whole file through an
    // fps filter, so import stays quick and the two paths cannot drift apart in
    // appearance.
    const FilmstripBuilder::Result strip =
        FilmstripBuilder::build(f.absoluteFilePath(), durationMs);
    if (strip.valid()) {
      r["timelineThumbnailUrl"] = strip.url;
      r["filmstripFrames"] = strip.cells;
      r["filmstripFrameWidth"] = strip.cellWidth;
      r["filmstripFrameHeight"] = strip.cellHeight;
    } else {
      r["timelineThumbnailUrl"] =
          MediaPreviewGenerator::filmstrip(f.absoluteFilePath(), durationMs);
      if (!r.value("timelineThumbnailUrl").toString().isEmpty()) {
        r["filmstripFrames"] = MediaPreviewGenerator::kFilmstripFrames;
        r["filmstripFrameWidth"] = MediaPreviewGenerator::kFilmstripFrameWidth;
        r["filmstripFrameHeight"] = MediaPreviewGenerator::kFilmstripFrameHeight;
      }
    }
  } else {
    r["timelineThumbnailUrl"] = QString();
  }
  if (!r.value("timelineThumbnailUrl").toString().isEmpty() &&
      kind == "video" && !r.contains("filmstripFrameWidth")) {
    r["filmstripFrames"] = 1;
    r["filmstripFrameWidth"] = 320;
    r["filmstripFrameHeight"] = 180;
  }
  r["waveformUrl"] = QString();
  if (generateDetailedPreviews && (kind == "video" || kind == "audio")) {
    const AudioPeakBuilder::Result waveform =
        AudioPeakBuilder::build(f.absoluteFilePath(), durationMs);
    r["waveformUrl"] = waveform.valid()
                           ? waveform.url
                           : MediaPreviewGenerator::waveform(
                                 f.absoluteFilePath());
  }
  return r;
}

int Backend::maxConcurrentLargePreviews() {
  return qBound(1, QThread::idealThreadCount() / 4, 4);
}

void Backend::startDeferredMediaPreview(const QVariantMap &media) {
  if (media.isEmpty())
    return;
  const QString mediaId = media.value(QStringLiteral("id")).toString();
  // One job per source. The same file can be dropped on the timeline several
  // times, and a second job would only rebuild what the first is already
  // producing into the same cache entry.
  if (!mediaId.isEmpty() && (m_largeMediaPreviewInFlight.contains(mediaId) ||
                             m_pendingLargeMediaPreviewSet.contains(mediaId)))
    return;
  if (m_largeMediaPreviewInFlight.size() >= maxConcurrentLargePreviews()) {
    // Pool is full: queue the id (cheap) and run it as a slot frees up. Bounded
    // only as a safety valve - dropping the oldest id just defers that source's
    // filmstrip to whenever it is next placed (ensureMediaPreviews).
    if (!mediaId.isEmpty()) {
      if (m_pendingLargeMediaPreviews.size() >= kMaxPendingLargePreviews) {
        const QString dropped = m_pendingLargeMediaPreviews.takeFirst();
        m_pendingLargeMediaPreviewSet.remove(dropped);
      }
      m_pendingLargeMediaPreviews.append(mediaId);
      m_pendingLargeMediaPreviewSet.insert(mediaId);
    }
    return;
  }
  launchLargeMediaPreview(media);
}

void Backend::launchLargeMediaPreview(const QVariantMap &media) {
  const QString mediaId = media.value(QStringLiteral("id")).toString();
  m_largeMediaPreviewInFlight.insert(mediaId);
  // Self-owned watcher: parented to this so shutdown cleans it up, deleteLater'd
  // the moment it reports. The task copies the media map by value and touches no
  // Backend state, so a watcher torn down mid-flight at shutdown leaves nothing
  // dangling.
  auto *watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
          [this, watcher]() {
            const QVariantMap result = watcher->result();
            watcher->deleteLater();
            finishDeferredMediaPreview(result);
          });
  watcher->setFuture(QtConcurrent::run(
      [](const QVariantMap &value) {
        return LargeMediaPreviewJob::generate(value);
      },
      media));
}

void Backend::finishDeferredMediaPreview(const QVariantMap &result) {
  const QString id = result.value(QStringLiteral("id")).toString();
  m_largeMediaPreviewInFlight.remove(id);
  // Every key the job filled in is merged, not just the poster: the filmstrip and
  // the waveform are the reason a long clip now looks like a short one on the
  // timeline instead of a flat rectangle.
  static const QStringList previewKeys{
      QStringLiteral("thumbnailUrl"),        QStringLiteral("timelineThumbnailUrl"),
      QStringLiteral("filmstripFrames"),     QStringLiteral("filmstripFrameWidth"),
      QStringLiteral("filmstripFrameHeight"), QStringLiteral("waveformUrl")};
  if (!id.isEmpty()) {
    const int index = mediaIndex(id);
    if (index >= 0) {
      auto media = m_media.at(index).toMap();
      bool changed = false;
      for (const QString &key : previewKeys) {
        if (!result.contains(key))
          continue;
        const QVariant value = result.value(key);
        // An empty URL means the producer failed; keeping whatever is already
        // there beats replacing a working thumbnail with nothing.
        if (value.metaType().id() == QMetaType::QString &&
            value.toString().isEmpty())
          continue;
        if (media.value(key) == value)
          continue;
        media[key] = value;
        changed = true;
      }
      if (changed) {
        m_media[index] = media;
        markDirty();
        // Coalesced, not emitted per completion: in a thousand-clip bin these
        // land one after another and each mediaChanged() rebuilds the whole bin.
        // The timeline draws clip thumbnails from the media entry, so the clips
        // already placed for this source repaint on the same coalesced tick.
        publishMediaBinUpdate(true);
      }
    }
  }
  // Refill the freed slot(s) from the backlog, skipping ids whose source has
  // gone or has since been filled by another path.
  while (!m_pendingLargeMediaPreviews.isEmpty() &&
         m_largeMediaPreviewInFlight.size() < maxConcurrentLargePreviews()) {
    const QString nextId = m_pendingLargeMediaPreviews.takeFirst();
    m_pendingLargeMediaPreviewSet.remove(nextId);
    const int index = mediaIndex(nextId);
    if (index < 0)
      continue;
    const QVariantMap next = m_media.at(index).toMap();
    if (!needsDeferredPreview(next))
      continue;
    launchLargeMediaPreview(next);
  }
}

void Backend::publishMediaBinUpdate(bool withTimeline) {
  // Sticky across the coalesced window: once a backfill completion in this burst
  // has asked for the clip repaint, the eventual flush must carry timelineChanged
  // even if the call that trips the flush is an import append that does not.
  if (withTimeline)
    m_mediaPreviewTimelinePending = true;
  // The cost of the notification grows with the bin: every mediaChanged()
  // re-materialises the whole visibleMedia list into JS and re-sorts it, so a
  // 1000-clip bin pays a full rebuild per thumbnail that streams in. Collapse
  // the burst to one rebuild per interval, scaled by bin size the way the TTS
  // publish throttle scales by clip count, so a huge import relays about once a
  // second instead of a thousand times and the GUI thread stays free to play.
  constexpr qint64 kBaseIntervalMs = 150;
  constexpr qint64 kMaxIntervalMs = 1000;
  constexpr int kFreeMedia = 200;
  const qint64 extra = qMax<qint64>(0, qint64(m_media.size()) - kFreeMedia) / 4;
  const qint64 interval = qMin(kMaxIntervalMs, kBaseIntervalMs + extra);

  if (m_mediaPreviewPublishClock.isValid() &&
      m_mediaPreviewPublishClock.elapsed() < interval) {
    // Inside the window: remember that a fill is waiting and make sure a trailing
    // tick will flush it, so the last thumbnail in a burst is never dropped.
    m_mediaPreviewPublishPending = true;
    if (!m_mediaPreviewPublishTimer.isActive())
      m_mediaPreviewPublishTimer.start(
          int(interval - m_mediaPreviewPublishClock.elapsed()));
    return;
  }

  m_mediaPreviewPublishClock.restart();
  m_mediaPreviewPublishPending = false;
  const bool emitTimeline = m_mediaPreviewTimelinePending;
  m_mediaPreviewTimelinePending = false;
  emit mediaChanged();
  if (emitTimeline)
    emit timelineChanged();
}

void Backend::publishClipStackUpdate() {
  if (m_editBatchDepth > 0) {
    m_editBatchClipsDirty = true;
    return;
  }
  // A live effect slider streams one edit per mouse-move. clipsChanged() queues
  // a full projection - TimelineClipModel::setClips (O(clips) span rebuild) and
  // VideoPreviewHelper::setTimeline -> rebuildIndexes (O(clips)+O(media)) - and
  // an effect value changes none of the structure either walks. On a thousand
  // clips that is a whole-timeline rebuild per drag tick. Collapse the burst to
  // one rebuild per interval, scaled by clip count like the TTS publish
  // throttle, with a leading emit so the monitor reacts as the drag starts and
  // a trailing tick so the value the user releases on is always published.
  constexpr qint64 kBaseIntervalMs = 150;
  constexpr qint64 kMaxIntervalMs = 1000;
  constexpr int kFreeClips = 200;
  const qint64 extra = qMax<qint64>(0, qint64(m_clips.size()) - kFreeClips) / 4;
  const qint64 interval = qMin(kMaxIntervalMs, kBaseIntervalMs + extra);

  if (m_clipStackPublishClock.isValid() &&
      m_clipStackPublishClock.elapsed() < interval) {
    m_clipStackPublishPending = true;
    if (!m_clipStackPublishTimer.isActive())
      m_clipStackPublishTimer.start(
          int(interval - m_clipStackPublishClock.elapsed()));
    return;
  }

  m_clipStackPublishClock.restart();
  m_clipStackPublishPending = false;
  // Payload-only: an effect/transform/gain value changes a clip's effects map,
  // never its position/duration/track/kind, so the projection takes the
  // windowed path (see emitClipsPayloadChanged / flushProjection).
  emitClipsPayloadChanged();
}

void Backend::notifyClipEdit(bool payloadOnly) {
  if (m_editBatchDepth > 0) {
    m_editBatchClipsDirty = true;
    if (!payloadOnly)
      m_editBatchPayloadOnly = false;
    return;
  }
  if (payloadOnly)
    emitClipsPayloadChanged();
  else {
    emit clipsChanged();
    emit timelineChanged();
  }
}

void Backend::emitClipsPayloadChanged() {
  if (m_editBatchDepth > 0) {
    m_editBatchClipsDirty = true;
    return;
  }
  m_clipsPayloadOnlyEdit = true;
  emit clipsChanged();
  emit timelineChanged();
}

QString Backend::thumbnailForMedia(const QString &path, const QString &kind,
                                   qint64 durationMs) const {
  if (kind != "video")
    return {};
  // Seek-based first: one keyframe out of an open container, with the FFmpeg
  // executable kept only as the fallback for builds without native linkage.
  const QString poster = FilmstripBuilder::poster(path, durationMs);
  if (!poster.isEmpty())
    return poster;
  return MediaPreviewGenerator::thumbnail(path, durationMs);
}

bool Backend::startExport(const QString &output, const QString &preset) {
  return startExportWithSettings(output, {{"quality", preset}});
}

QString Backend::suggestedExportPath() const {
  QStringList folders;
  if (!m_projectLocation.isEmpty()) {
    folders << QDir(m_projectLocation).filePath(QStringLiteral("Exports"));
  } else {
    const QStringList roots{
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)};
    for (const QString &root : roots)
      if (!root.isEmpty())
        folders << QDir(root).filePath(QStringLiteral("CutPro Exports"));
  }
  QString folder;
  for (const QString &candidate : folders) {
    if (writableDirectory(candidate)) {
      folder = candidate;
      break;
    }
  }
  if (folder.isEmpty() && !folders.isEmpty())
    folder = folders.first();
  if (folder.isEmpty())
    folder = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                 .filePath(QStringLiteral("CutPro Exports"));
  const QString base = safeFileName(m_sequenceName);
  QString target = QDir(folder).filePath(base + QStringLiteral(".mp4"));
  for (int suffix = 2; QFileInfo::exists(target); ++suffix)
    target = QDir(folder).filePath(
        QStringLiteral("%1 %2.mp4").arg(base).arg(suffix));
  return QDir::cleanPath(target);
}

Q_LOGGING_CATEGORY(lcExport, "cutpro.export")

bool Backend::startExportWithSettings(const QString &output,
                                      const QVariantMap &settings) try {
  qCInfo(lcExport) << "requested" << output << "quality"
                   << settings.value("quality").toString() << "size"
                   << settings.value("width").toInt() << "x"
                   << settings.value("height").toInt() << "fps"
                   << settings.value("frameRate").toDouble() << "format"
                   << settings.value("videoFormat").toString();
  QVariantMap exportSettings = settings;
  exportSettings.insert(QStringLiteral("captionFontFamily"),
                        m_captionStyle.fontFamily);
  exportSettings.insert(QStringLiteral("captionFontSize"), m_captionStyle.fontSize);
  exportSettings.insert(QStringLiteral("captionBoxWidth"), m_captionStyle.boxWidth);
  exportSettings.insert(QStringLiteral("captionTextColor"), m_captionStyle.textColor);
  exportSettings.insert(QStringLiteral("captionBold"), m_captionStyle.bold);
  exportSettings.insert(QStringLiteral("captionItalic"), m_captionStyle.italic);
  exportSettings.insert(QStringLiteral("captionBackgroundVisible"),
                        m_captionStyle.backgroundVisible);
  exportSettings.insert(QStringLiteral("captionBackgroundColor"), m_captionStyle.backgroundColor);
  exportSettings.insert(QStringLiteral("captionPosition"), m_captionStyle.position);
  exportSettings.insert(QStringLiteral("captionAlignment"), m_captionStyle.alignment);
  exportSettings.insert(QStringLiteral("captionPositionX"), m_captionStyle.positionX);
  exportSettings.insert(QStringLiteral("captionPositionY"), m_captionStyle.positionY);
  exportSettings.insert(QStringLiteral("captionTextEffect"),
                        m_captionStyle.textEffect);
  exportSettings.insert(QStringLiteral("captionAnimation"),
                        m_captionStyle.animation);
  exportSettings.insert(QStringLiteral("captionAnimationSpeed"),
                        m_captionStyle.animationSpeed);
  // Second, independent license check before a high-value action. Aborts on a
  // patched/inconsistent gate.
  enforceLicenseGuard();
  if (exportInProgress() || !canExport()) {
    qCWarning(lcExport) << "refused: inProgress" << exportInProgress()
                        << "canExport" << canExport() << "hasSequence"
                        << hasSequence();
    setError(
        exportInProgress()
            ? QStringLiteral("An export is already running.")
            : QStringLiteral("Add media to the sequence before exporting."));
    return false;
  }
  QString target = normalizePath(output);
  if (target.isEmpty())
    target = suggestedExportPath();
  QFileInfo targetInfo(target);
  if (targetInfo.exists() && targetInfo.isDir()) {
    target = QDir(target).filePath(safeFileName(m_sequenceName) + ".mp4");
    targetInfo.setFile(target);
  } else if (targetInfo.suffix().isEmpty()) {
    target += ".mp4";
    targetInfo.setFile(target);
  }
  const QString outputFolder = targetInfo.absolutePath();
  if (!writableDirectory(outputFolder)) {
    m_exportStatus = QStringLiteral("Export failed");
    qCWarning(lcExport) << "folder not writable" << outputFolder;
    setError(QStringLiteral("The export folder is not writable: %1")
                 .arg(QDir::toNativeSeparators(outputFolder)));
    emit exportStateChanged();
    return false;
  }
  m_exportOutputPath = target;
  recordAction(QStringLiteral("export-start"),
               QVariantMap{{"output", target}, {"settings", settings}});
  m_exportProgress = 0;
  m_exportStatus = QStringLiteral("Exporting");
  m_exportStandardError.clear();
  m_exportMetrics.reset();
  m_exportCancelled = false;
  m_exportFinalizing = false;
  m_exportChunkIndex = 0;
  m_exportRemainingSeconds = -1;
  m_exportDurationMs = durationMs();
  m_exportElapsed.start();
  m_exportElapsedMs = 0;
  m_exportEncoder.clear();
  m_exportEncoderFallback.clear();
  m_exportEstimate.start(0, m_exportDurationMs, m_exportJoinSpeed);
  clearError();
  // Hand each clip its animation channels so the rendered file follows the same
  // curves the monitor shows. Attached to the clip rather than passed as a
  // parallel argument because everything downstream already works clip by clip.
  QVariantList exportClips = m_clips;
  // Cues that carry no word timing of their own, measured off the audio so a
  // burned-in karaoke highlight follows the speaker like the monitor does. Only
  // for a karaoke animation, and bounded: each one is a decode, and the raster
  // pass only animates a transcript it can fit in its image budget anyway - far
  // fewer cues than this. Past the cap a cue keeps the weighted estimate.
  constexpr int kMaxMeasuredCues = 128;
  const bool measureCues =
      m_captionStyle.animation.startsWith(QLatin1String("karaoke"));
  int measured = 0;
  for (QVariant &value : exportClips) {
    QVariantMap clip = value.toMap();
    const QVariantMap channels =
        m_keyframeEngine.channelsForClip(clip.value("id").toString());
    if (!channels.isEmpty()) {
      clip.insert(QStringLiteral("keyframes"), channels);
      value = clip;
    }
    if (!measureCues || measured >= kMaxMeasuredCues ||
        clip.value(QStringLiteral("kind")) != QStringLiteral("subtitle") ||
        !clip.value(QStringLiteral("wordTimes")).toString().isEmpty())
      continue;
    const qint64 cueStart = clip.value(QStringLiteral("startMs")).toLongLong();
    qint64 sourceStart = 0;
    const QString path = captionAudioSourceForExport(cueStart, &sourceStart);
    if (path.isEmpty())
      continue;
    ++measured;
    const QString packed = m_captionSpeech.timingNow(
        path, sourceStart, clip.value(QStringLiteral("durationMs")).toLongLong(),
        clip.value(QStringLiteral("text"))
            .toString()
            .simplified()
            .split(QLatin1Char(' '), Qt::SkipEmptyParts));
    if (packed.isEmpty())
      continue;
    clip.insert(QStringLiteral("wordTimes"), packed);
    value = clip;
  }
  m_exportChunks = SequentialExportPlan::build(
      exportClips, m_media, trackStates(), exportSettings.value("frameRate").toDouble());
  if (!m_exportChunks.isEmpty()) {
    const auto last = m_exportChunks.last().toMap();
    const auto first = m_exportChunks.first().toMap();
    // Hidden tracks may extend the sequence beyond the enabled videos. Keep
    // the general exporter for that layout so its black/silent tail survives.
    const qint64 plannedEnd = last.value("startMs").toLongLong() + last.value("durationMs").toLongLong();
    if (plannedEnd < m_exportDurationMs ||
        first.value("width").toInt() <= 0 || first.value("height").toInt() <= 0) {
      m_exportChunks.clear();
    } else {
      // Match Source must stay fixed across clips with different dimensions.
      exportSettings["frameRate"] = first.value("frameRate");
      if (exportSettings.value("width").toInt() <= 0 ||
          exportSettings.value("height").toInt() <= 0) {
        exportSettings["width"] = first.value("width");
        exportSettings["height"] = first.value("height");
      }
    }
  }
  if (!m_exportChunks.isEmpty()) {
    QTemporaryDir work(QDir(outputFolder).filePath(QStringLiteral(".cutpro-export-XXXXXX")));
    if (!work.isValid()) {
      failExport(QStringLiteral("Could not create the export working folder."));
      return false;
    }
    work.setAutoRemove(false);
    m_exportWorkDir = work.path();
  }
  // Snapshot both export paths before probing; edits during capability work
  // must not change either the encoder configuration or the rendered sequence.
  if (exportSettings.value("width").toInt() <= 0 || exportSettings.value("height").toInt() <= 0) {
    const auto source = sequenceSourceInfo();
    exportSettings["width"] = source.value("width");
    exportSettings["height"] = source.value("height");
  }
  if (exportSettings.value("frameRate").toDouble() <= 0)
    exportSettings["frameRate"] = sequenceSourceInfo().value("frameRate");
  m_exportMedia = m_media;
  m_exportClips = exportClips;
  m_exportTrackStates = trackStates();
  m_exportMutedTracks = m_mutedTracks;
  m_exportColorSettings = m_colorSettings;
  m_exportSettings = exportSettings;
  m_exportActive = true;
  prepareExportEncoder();
  return m_exportActive;
} catch (const std::bad_alloc &) {
  failExport(QStringLiteral("Not enough memory to prepare the export."));
  return false;
} catch (const std::exception &error) {
  failExport(QStringLiteral("Could not prepare the export: %1")
                 .arg(QString::fromUtf8(error.what())));
  return false;
} catch (...) {
  failExport(QStringLiteral("Could not prepare the export unexpectedly."));
  return false;
}
void Backend::cancelExport() {
  if (!exportInProgress())
    return;
  m_exportCancelled = true;
  if (m_exportProbeProcess) {
    if (m_exportProbeProcess->state() != QProcess::NotRunning)
      m_exportProbeProcess->kill();
    return;
  }
  if (m_exportProcess.state() != QProcess::NotRunning)
    m_exportProcess.kill();
  else
    finishExportProcess(-1, QProcess::CrashExit);
}
bool Backend::transcribeMediaBatch(const QStringList &mediaIds, const QString &model,
                                   const QString &language) {
  if (transcriptionInProgress() || mediaIds.isEmpty())
    return false;
  QString selectedModel = model.trimmed().isEmpty()
      ? m_appSettings.value("transcriptionModel", "small").toString() : model.trimmed();
  if (!TranscriptionPlanner::isGeminiModel(selectedModel))
    return false; // Local engines retain their existing single-model memory budget.
  if (selectedModel.compare(QStringLiteral("gemini"), Qt::CaseInsensitive) == 0)
    selectedModel = m_appSettings.value("geminiModel",
        m_appSettings.value("transcriptionGeminiModel", "gemini-2.5-flash")).toString().trimmed();
  if (selectedModel.isEmpty())
    selectedModel = QStringLiteral("gemini-2.5-flash");
  const QString selectedLanguage = language.trimmed().isEmpty()
      ? m_appSettings.value("transcriptionLanguage", "auto").toString() : language;
  const QString python = PythonRuntime::executable(
      m_appSettings.value("pythonExecutable", "python").toString());
  const QString worker = QDir(QCoreApplication::applicationDirPath()).filePath("tools/gemini_transcribe.py");
  if (python.isEmpty() || !QFileInfo::exists(worker)) {
    setError(QStringLiteral("Python Gemini transcription worker is not available."));
    return false;
  }
  const bool usePool = m_appSettings.value("geminiUsePool", true).toBool();
  const QString endpoint = usePool ? cutpro::pool::endpointIfReady() : QString();
  const QString keys = m_appSettings.value("translationGeminiApiKeys").toString().trimmed();
  if (endpoint.isEmpty() && keys.isEmpty()) {
    setError(QStringLiteral("Start the Account Pool with a ready Google account, "
                            "or add a fallback Gemini API key in Settings > Translation."));
    return false;
  }
  TranscriptionBatchRequest request;
  request.program = python;
  request.worker = worker;
  request.model = selectedModel;
  request.language = selectedLanguage;
  request.ffmpeg = ffmpeg();
  request.maxSegmentMs = maxSegmentMs();
  request.environment = QProcessEnvironment::systemEnvironment();
  request.environment.insert(QStringLiteral("CUTPRO_SEGMENT_MAX_MS"), QString::number(maxSegmentMs()));
  request.environment.insert(QStringLiteral("CUTPRO_TRANSCRIPTION_MODEL"), selectedModel);
  request.environment.insert(QStringLiteral("CUTPRO_TRANSCRIPTION_API_KEYS"), keys);
  request.environment.insert(QStringLiteral("CUTPRO_FFMPEG"), ffmpeg());
  request.environment.insert(QStringLiteral("CUTPRO_POOL_MODE"), usePool ? QStringLiteral("1") : QStringLiteral("0"));
  if (endpoint.isEmpty()) {
    request.environment.remove(QStringLiteral("CUTPRO_POOL_ENDPOINT"));
  } else {
    request.environment.insert(QStringLiteral("CUTPRO_POOL_ENDPOINT"), endpoint);
  }
  if (m_appSettings.value("transcriptionRecapMovie").toBool())
    request.environment.insert(QStringLiteral("CUTPRO_TRANSCRIPTION_RECAP"), QStringLiteral("1"));
  // Bulk mode starts one clip per ready account. Single mode uses this same
  // queue with one slot, preserving rotation, cancellation and partial results.
  request.concurrency = GeminiWorkPolicy::parallelism(m_appSettings,
      endpoint.isEmpty() ? 0 : PoolManager::instance().state().value("readyCount").toInt());
  QSet<QString> seen;
  for (const QString &mediaId : mediaIds) {
    if (seen.contains(mediaId))
      continue;
    const QVariantMap item = mediaById(mediaId);
    if (item.isEmpty()) {
      setError(QStringLiteral("A selected transcription source is no longer available."));
      return false;
    }
    seen.insert(mediaId);
    TranscriptionBatchItem job;
    job.media = item;
    const qint64 resume = m_transcriptCoverageMs.value(mediaId, 0);
    job.plan = TranscriptionPlanner::forSource(item.value("durationMs").toLongLong(), selectedModel, resume);
    if (resume > 0 && job.plan.chunked)
      job.resumeSegments = m_sourceTranscripts.value(mediaId);
    request.items.append(job);
  }
  m_translator.clearStatus();
  m_transcript.clear();
  m_transcriptLanguage.clear();
  m_transcriptionWindowCount = 0;
  clearError();
  return m_transcriptionBatch.start(request);
}

bool Backend::transcribeMedia(const QString &mediaId, const QString &model,
                              const QString &language) {
  if (transcriptionInProgress())
    return false;
  m_transcriptionBatch.clear();
  const auto item = mediaById(mediaId);
  if (item.isEmpty()) {
    setError(QStringLiteral("Select media before transcribing."));
    return false;
  }
  // The engine travels in the model name. The transcript panel's dropdown stores
  // the bare "gemini" token and the concrete model behind it is a separate
  // setting, so the tier can be changed without touching the selection - and a
  // literal "gemini-2.5-flash" passed straight in works too.
  QString selectedModel =
      model.trimmed().isEmpty()
          ? m_appSettings.value("transcriptionModel", "small").toString()
          : model.trimmed();
  const QString selectedLanguage =
      language.trimmed().isEmpty()
          ? m_appSettings.value("transcriptionLanguage", "auto").toString()
          : language;
  const bool useGemini = TranscriptionPlanner::isGeminiModel(selectedModel);
  const bool useOcr = TranscriptionPlanner::isOcrModel(selectedModel);
  // Both engines run under the bundled interpreter: Gemini is stdlib-only, and
  // Whisper (with torch) is installed into the bundle on demand from the
  // Requirements page, so there is no system Python to route to. OCR
  // (rapidocr_onnxruntime) is a bundled interpreter package too.
  const QString python = PythonRuntime::executable(
      m_appSettings.value("pythonExecutable", "python").toString());
  QString geminiKeys;
  if (useGemini) {
    if (selectedModel.compare(QStringLiteral("gemini"), Qt::CaseInsensitive) ==
        0)
      selectedModel = m_appSettings
                          .value("geminiModel", m_appSettings.value("transcriptionGeminiModel", "gemini-2.5-flash"))
                          .toString()
                          .trimmed();
    if (selectedModel.isEmpty())
      selectedModel = QStringLiteral("gemini-2.5-flash");
    // The same keys the translator uses. Asking for them twice would only give
    // the user two places to keep the same secret in step.
    geminiKeys =
        m_appSettings.value("translationGeminiApiKeys").toString().trimmed();
    if (geminiKeys.isEmpty() && (!m_appSettings.value("geminiUsePool", true).toBool() ||
                                cutpro::pool::endpointIfReady().isEmpty())) {
      setError(QStringLiteral("Start the Account Pool with a ready Google account, "
                              "or add a fallback Gemini API key in Settings > Translation."));
      return false;
    }
  }
  // Offline Whisper is a native, bundled exe now - no Python, no pip. Only the
  // cloud (Gemini) and OCR engines still launch a Python worker. Everything else
  // in this function (job.json, the prefetcher, the streamed-event handling,
  // cancel/resume) is identical whichever program is launched, because the
  // native worker speaks the same line-delimited JSON protocol.
  const bool useWhisper = !useGemini && !useOcr;
  QString program;
  QStringList workerArgs;
  if (useWhisper) {
    program = QDir(QCoreApplication::applicationDirPath())
                  .filePath(QStringLiteral("cutpro_whisper.exe"));
    if (!QFileInfo::exists(program)) {
      setError(QStringLiteral(
          "The offline transcription engine (cutpro_whisper.exe) is missing "
          "from the application folder. Reinstall Cut Pro to restore it."));
      return false;
    }
  } else {
    const QString workerFile = useGemini
                                   ? QStringLiteral("tools/gemini_transcribe.py")
                                   : QStringLiteral("tools/ocr_subtitles.py");
    const QString worker =
        QDir(QCoreApplication::applicationDirPath()).filePath(workerFile);
    if (python.isEmpty() || !QFileInfo::exists(worker)) {
      setError(useGemini ? QStringLiteral("Python Gemini transcription worker "
                                          "is not available.")
                         : QStringLiteral("On-screen subtitle OCR worker is "
                                          "not available."));
      return false;
    }
    program = python;
    workerArgs << worker;
  }
  m_translator.clearStatus();
  m_transcript.clear();
  m_transcriptLanguage.clear();
  m_transcriptionStatus = QStringLiteral("Transcribing...");
  m_transcriptionMediaId = mediaId;
  m_transcriptionProgress = 0.0;
  m_transcriptionStderr.clear();
  m_transcriptionStdout.clear();
  m_transcriptionLastLine.clear();
  m_transcriptionSegments.clear();
  m_transcriptionStreamed = false;
  m_transcriptionWindowIndex = 0;
  m_transcriptionWindowCount = 0;
  m_transcriptionWindowFraction = 0.0;
  m_transcriptionCancelRequested = false;
  // Anything left of an earlier run: the prefetcher owns ffmpeg processes and
  // scratch WAVs that this run's job directory is about to replace.
  m_windowAudio.stop();
  clearTranscriptionJobDir();

  // Whisper loads the whole audio track into memory before it decodes anything,
  // so the length of the source - not the model - is what decides whether this
  // machine survives the run. Long sources are cut into windows and transcribed
  // one window at a time. Gemini is windowed too, but by request size.
  const QString sourcePath = item.value("path").toString();
  const qint64 sourceDurationMs = item.value("durationMs").toLongLong();
  // Anything already transcribed by a cancelled run is kept and continued.
  const qint64 resumeFromMs = m_transcriptCoverageMs.value(mediaId, 0);
  m_transcriptionPlan =
      TranscriptionPlanner::forSource(sourceDurationMs, selectedModel,
                                      resumeFromMs);
  if (resumeFromMs > 0 && m_transcriptionPlan.chunked)
    m_transcriptionSegments = m_sourceTranscripts.value(mediaId);

  // Both forms of the run take the same environment. The line limits travel here
  // because the one-pass form has no job file, and the Gemini worker reads its
  // model and its keys from here so no secret is written to scratch on disk.
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("CUTPRO_SEGMENT_MAX_MS"),
                     QString::number(maxSegmentMs()));
  if (useGemini) {
    environment.insert(QStringLiteral("CUTPRO_TRANSCRIPTION_MODEL"),
                       selectedModel);
    environment.insert(QStringLiteral("CUTPRO_TRANSCRIPTION_API_KEYS"),
                       geminiKeys);
    environment.insert(QStringLiteral("CUTPRO_FFMPEG"), ffmpeg());
    // The account pool, when it is on and has ready accounts: the worker tries
    // the pool's loopback endpoint first and falls back to the keys above.
    const bool usePool = m_appSettings.value("geminiUsePool", true).toBool();
    const QString poolEndpoint = usePool ? cutpro::pool::endpointIfReady() : QString();
    environment.insert(QStringLiteral("CUTPRO_POOL_MODE"), usePool ? QStringLiteral("1") : QStringLiteral("0"));
    if (!poolEndpoint.isEmpty()) {
      environment.insert(QStringLiteral("CUTPRO_POOL_ENDPOINT"), poolEndpoint);
      const int ready = PoolManager::instance().state().value("readyCount").toInt();
      environment.insert(QStringLiteral("CUTPRO_POOL_PARALLELISM"),
          QString::number(GeminiWorkPolicy::parallelism(m_appSettings, ready)));
    } else {
      environment.remove(QStringLiteral("CUTPRO_POOL_ENDPOINT"));
      environment.insert(QStringLiteral("CUTPRO_POOL_PARALLELISM"), QStringLiteral("1"));
    }
    // Recap movie mode: when on, the transcription pass itself is asked to
    // return a speaker-attributed recap with connective phrasing instead of a
    // verbatim transcript, so no second Gemini call (and no extra credits) is
    // needed. The concrete prompt change lives in the worker.
    if (m_appSettings.value("transcriptionRecapMovie").toBool())
      environment.insert(QStringLiteral("CUTPRO_TRANSCRIPTION_RECAP"),
                         QStringLiteral("1"));
  }
  if (useOcr) {
    // OCR samples video frames itself, so it needs ffmpeg (and ffprobe beside
    // it) on the single-pass path where there is no job file to carry it.
    environment.insert(QStringLiteral("CUTPRO_FFMPEG"), ffmpeg());
    // Reading the subtitle band with Gemini vision is far faster than the
    // bundled CPU OCR, so when the user has a Gemini key configured (the same
    // one the translator uses) hand it and a vision model to the worker. With
    // no key the worker falls back to local RapidOCR on its own, so this stays
    // optional - OCR never demands a key the way the Gemini audio engine does.
    const QString ocrGeminiKeys =
        m_appSettings.value("translationGeminiApiKeys").toString().trimmed();
    if (!ocrGeminiKeys.isEmpty()) {
      environment.insert(QStringLiteral("CUTPRO_TRANSCRIPTION_API_KEYS"),
                         ocrGeminiKeys);
      QString visionModel =
          m_appSettings
              .value("transcriptionGeminiModel", "gemini-2.5-flash")
              .toString()
              .trimmed();
      if (visionModel.isEmpty())
        visionModel = QStringLiteral("gemini-2.5-flash");
      environment.insert(QStringLiteral("CUTPRO_OCR_GEMINI_MODEL"),
                         visionModel);
    }
  }
  m_transcriptionProcess.setProcessEnvironment(environment);

  if (!m_transcriptionPlan.chunked) {
    m_transcriptionStatus = QStringLiteral("Transcribing - %1")
                                .arg(m_transcriptionPlan.summary);
    emit transcriptChanged();
    QStringList args = workerArgs;
    args << sourcePath << selectedModel << selectedLanguage;
    m_transcriptionProcess.start(program, args);
    return m_transcriptionProcess.waitForStarted(1500);
  }

  const QString jobDir = startTranscriptionJobDir(mediaId);
  if (jobDir.isEmpty()) {
    m_transcriptionStatus.clear();
    m_transcriptionMediaId.clear();
    setError(QStringLiteral("Could not create a transcription scratch folder."));
    emit transcriptChanged();
    return false;
  }
  QJsonArray windows;
  for (const TranscriptionWindow &window : m_transcriptionPlan.windows)
    windows.append(QJsonObject{{"startMs", window.startMs},
                               {"lengthMs", window.lengthMs},
                               {"leadInMs", window.leadInMs}});
  const QJsonObject job{{"source", sourcePath},
                        {"model", selectedModel},
                        {"language", selectedLanguage},
                        {"ffmpeg", ffmpeg()},
                        {"tempDir", jobDir},
                        // The app extracts the window audio itself, a few windows
                        // ahead of the worker, so ffmpeg and the model run at the
                        // same time instead of taking turns. The worker waits for
                        // the file and falls back to extracting it itself if the
                        // app's attempt failed, so this can only cost time, never
                        // a transcript. OCR reads video frames, not the prefetched
                        // audio, so it opts out and samples the source itself.
                        {"prefetch", !useOcr},
                        {"prefetchTimeoutSeconds", 600},
                        // Where the worker cuts its lines. Only the ceiling is a
                        // setting; the pause that ends an utterance is a property
                        // of speech, not a preference.
                        {"segmentation",
                         QJsonObject{{"maxMs", maxSegmentMs()}}},
                        {"windows", windows}};
  const QString jobPath = QDir(jobDir).filePath(QStringLiteral("job.json"));
  QFile jobFile(jobPath);
  if (!jobFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    clearTranscriptionJobDir();
    m_transcriptionStatus.clear();
    m_transcriptionMediaId.clear();
    setError(QStringLiteral("Could not write the transcription job file."));
    emit transcriptChanged();
    return false;
  }
  jobFile.write(QJsonDocument(job).toJson(QJsonDocument::Compact));
  jobFile.close();

  m_transcriptionWindowCount = m_transcriptionPlan.windows.size();
  m_transcriptionStatus = QStringLiteral("Transcribing - %1")
                              .arg(m_transcriptionPlan.summary);
  emit transcriptChanged();
  // Started before the worker: window 0's extraction then runs while Python
  // imports torch and loads the model, which on the medium model is seconds the
  // run used to spend doing nothing. OCR does not use prefetched audio, so its
  // worker samples frames itself and nothing is extracted ahead of it.
  if (!useOcr)
    m_windowAudio.start(ffmpeg(), sourcePath, jobDir,
                        m_transcriptionPlan.windows);
  QStringList args = workerArgs;
  args << QStringLiteral("--job") << jobPath;
  m_transcriptionProcess.start(program, args);
  return m_transcriptionProcess.waitForStarted(1500);
}

// One JSON object per line from the worker. Anything that is not a complete line
// stays in the buffer until the rest of it arrives.
void Backend::consumeTranscriptionOutput() {
  m_transcriptionStdout += m_transcriptionProcess.readAllStandardOutput();
  int newline = m_transcriptionStdout.indexOf('\n');
  while (newline >= 0) {
    const QByteArray line = m_transcriptionStdout.left(newline).trimmed();
    m_transcriptionStdout.remove(0, newline + 1);
    if (!line.isEmpty()) {
      m_transcriptionLastLine = line;
      const QJsonDocument document = QJsonDocument::fromJson(line);
      if (document.isObject())
        handleTranscriptionEvent(document.object().toVariantMap());
    }
    newline = m_transcriptionStdout.indexOf('\n');
  }
}

void Backend::handleTranscriptionEvent(const QVariantMap &event) {
  const QString type = event.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("prefetch")) {
    m_windowAudio.noteWindowStarted(event.value(QStringLiteral("index")).toInt());
    return;
  }
  if (type.isEmpty()) {
    // No type field: either the single-pass reply, which the exit handler reads
    // from m_transcriptionLastLine, or a worker error. An error that arrives
    // mid-run is kept so the status line can name what actually went wrong.
    const QString error = event.value(QStringLiteral("error")).toString();
    if (!error.isEmpty())
      m_transcriptionStderr = error.toUtf8();
    return;
  }
  // A phase note from a one-pass run (no windows). It only updates the status
  // line - it must NOT set m_transcriptionStreamed, or the exit handler would
  // take the windowed branch and never read the single-pass reply.
  if (type == QStringLiteral("status")) {
    const QString message = event.value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
      m_transcriptionStatus = message;
      emit transcriptChanged();
    }
    return;
  }
  m_transcriptionStreamed = true;
  if (type == QStringLiteral("window")) {
    m_transcriptionWindowIndex = event.value(QStringLiteral("index")).toInt();
    m_transcriptionWindowCount =
        qMax(m_transcriptionWindowCount,
             event.value(QStringLiteral("count")).toInt());
    m_transcriptionWindowFraction = 0.0;
    // The worker announces a window before it waits for its audio, so the
    // prefetch horizon moves first and the extraction it is waiting for is
    // already running or done.
    m_windowAudio.noteWindowStarted(m_transcriptionWindowIndex);
    const qint64 startMs = event.value(QStringLiteral("startMs")).toLongLong();
    m_transcriptionStatus =
        QStringLiteral("Transcribing %1/%2 from %3")
            .arg(m_transcriptionWindowIndex + 1)
            .arg(m_transcriptionWindowCount)
            .arg(TranscriptionPlanner::formatDuration(startMs));
    const int parallel = event.value(QStringLiteral("parallelism"), 1).toInt();
    if (parallel > 1)
      m_transcriptionStatus += tr(" · %1 pool batches in parallel").arg(parallel);
    updateTranscriptionProgress();
    emit transcriptChanged();
    return;
  }
  if (type == QStringLiteral("segments")) {
    const int index = event.value(QStringLiteral("index")).toInt();
    if (index < 0 || index >= m_transcriptionPlan.windows.size())
      return;
    TranscriptionPlanner::mergeWindow(
        &m_transcriptionSegments,
        event.value(QStringLiteral("segments")).toList(),
        m_transcriptionPlan.windows.at(index));
    // This window's audio has been read, so the scratch WAV goes now. A long
    // source would otherwise leave every window of it on disk until the run ends.
    m_windowAudio.releaseWindow(index);
    // Published as it lands: on an eight hour source the panel fills in while
    // the run continues instead of staying empty for hours.
    if (!m_transcriptionMediaId.isEmpty()) {
      m_sourceTranscripts.insert(m_transcriptionMediaId,
                                 m_transcriptionSegments);
      m_transcriptCoverageMs.insert(
          m_transcriptionMediaId,
          TranscriptionPlanner::coveredMs(m_transcriptionSegments));
    }
    m_transcript = m_transcriptionSegments;
    m_transcriptionWindowFraction = 1.0;
    updateTranscriptionProgress();
    rebuildSequenceTranscript();
    emit transcriptChanged();
    return;
  }
  if (type == QStringLiteral("done")) {
    m_transcriptLanguage = event.value(QStringLiteral("language")).toString();
    return;
  }
}

void Backend::updateTranscriptionProgress() {
  if (m_transcriptionWindowCount <= 0)
    return;
  const double completed =
      double(m_transcriptionWindowIndex) + m_transcriptionWindowFraction;
  // Held below 1.0 until the process actually exits, so the dashboard cannot
  // report a finished job while a window is still running.
  m_transcriptionProgress =
      qBound(0.0, completed / double(m_transcriptionWindowCount), 0.999);
}

// The longest line the worker may leave intact. Only a ceiling: the boundaries
// themselves come from the pauses in the speech, and a setting cannot know where
// those are.
qint64 Backend::maxSegmentMs() const {
  return qBound<qint64>(
      Q_INT64_C(2000),
      m_appSettings.value("transcriptionMaxSegmentMs", 6200).toLongLong(),
      Q_INT64_C(15000));
}

bool Backend::requirementsSatisfied() const {
  // A cold start (nothing probed yet) reads as satisfied so the navbar icon does
  // not flash before the first probe returns. Once probed, every *required*
  // entry must be present for the icon to stay hidden.
  for (const QVariant &value : m_systemRequirements) {
    const QVariantMap entry = value.toMap();
    if (entry.value(QStringLiteral("required")).toBool() &&
        entry.value(QStringLiteral("status")).toString() !=
            QStringLiteral("ok"))
      return false;
  }
  return true;
}

void Backend::buildRequirementPlaceholders() {
  // Same rows, same order as buildSystemRequirements, but shown the instant the
  // page opens - before the out-of-process Python probe answers. Rows that need
  // no interpreter (FFmpeg, offline Whisper) carry their real state right away;
  // the pip-probed engines sit at "checking" (no Download button yet) until the
  // probe returns and buildSystemRequirements replaces the whole list.
  auto row = [](const QString &id, const QString &label, const QString &feature,
                const QString &status, bool required, bool installable,
                const QString &detail) {
    return QVariantMap{{QStringLiteral("id"), id},
                       {QStringLiteral("label"), label},
                       {QStringLiteral("feature"), feature},
                       {QStringLiteral("status"), status},
                       {QStringLiteral("required"), required},
                       {QStringLiteral("installable"), installable},
                       {QStringLiteral("detail"), detail}};
  };

  const QString whisperModelId = defaultWhisperModelId(
      m_appSettings
          .value(QStringLiteral("transcriptionModel"), QStringLiteral("small"))
          .toString());
  const bool whisperOk = whisperModelInstalled(whisperModelId);
  const bool ffmpegOk =
      !FfmpegRuntime::root().isEmpty() ||
      !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();

  const QString checking = QStringLiteral("checking");
  const QString checkingDetail = QStringLiteral("Checking...");

  QVariantList list;
  list << row(QStringLiteral("python"), QStringLiteral("Python runtime"),
              QStringLiteral("Transcription, translation and voice-over"),
              checking, true, false, checkingDetail);
  list << row(QStringLiteral("ffmpeg"), QStringLiteral("FFmpeg"),
              QStringLiteral("Media decoding, transcription and export"),
              ffmpegOk ? QStringLiteral("ok") : QStringLiteral("missing"), true,
              false,
              ffmpegOk ? QStringLiteral("Available")
                       : QStringLiteral("Not found"));
  list << row(QStringLiteral("edge_tts"), QStringLiteral("Edge TTS voices"),
              QStringLiteral("Text to Speech"), checking, true, false,
              checkingDetail);
  list << row(QStringLiteral("whisper"), QStringLiteral("Whisper (offline)"),
              QStringLiteral("Offline transcription without a cloud key"),
              whisperOk ? QStringLiteral("ok") : QStringLiteral("missing"),
              false, !whisperOk,
              whisperOk
                  ? QStringLiteral("Model ready (%1)").arg(whisperModelId)
                  : QStringLiteral("Model not downloaded - click Download"));
  list << row(QStringLiteral("demucs"), QStringLiteral("Demucs"),
              QStringLiteral("Vocal / music separation (highest quality)"),
              checking, false, false, checkingDetail);
  list << row(QStringLiteral("mdxnet"), QStringLiteral("MDX-Net (light)"),
              QStringLiteral("Vocal separation - lighter and faster on weak "
                             "computers"),
              checking, false, false, checkingDetail);

  m_systemRequirements = list;
  emit systemRequirementsChanged();
}

void Backend::buildSystemRequirements(const QVariantMap &probe) {
  const bool pythonOk = probe.value(QStringLiteral("pythonOk")).toBool();
  const QString pyVersion = probe.value(QStringLiteral("python")).toString();
  const QString pyPath = probe.value(QStringLiteral("pythonPath")).toString();
  const bool bundledPy = PythonRuntime::isBundled() &&
                         pyPath == PythonRuntime::bundledExecutable();
  const bool edgeOk = probe.value(QStringLiteral("edge_tts")).toBool();
  // Offline Whisper is a bundled native exe now: "installed" means the default
  // model's GGML weight file sits in models/whisper/ - a plain file check with
  // no interpreter, unlike the pip-probed engines below.
  const QString whisperModelId = defaultWhisperModelId(
      m_appSettings
          .value(QStringLiteral("transcriptionModel"), QStringLiteral("small"))
          .toString());
  const bool whisperOk = whisperModelInstalled(whisperModelId);
  const bool demucsOk = probe.value(QStringLiteral("demucs")).toBool();
  const bool mdxnetOk = probe.value(QStringLiteral("mdxnet")).toBool();
  // Optional engines install into the bundled interpreter, which always ships
  // with the app, so anything missing can be downloaded in-app.
  const bool canInstall = PythonRuntime::isBundled() || pythonOk;
  const bool ffmpegOk =
      !FfmpegRuntime::root().isEmpty() ||
      !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();

  auto entry = [](const QString &id, const QString &label,
                  const QString &feature, bool ok, bool required,
                  bool installable, const QString &detail) {
    return QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("feature"), feature},
        {QStringLiteral("status"),
         ok ? QStringLiteral("ok") : QStringLiteral("missing")},
        {QStringLiteral("required"), required},
        // Whether the app can pip-install it (needs a system Python and the
        // component still missing). Drives the Install button in the UI.
        {QStringLiteral("installable"), installable},
        {QStringLiteral("detail"), detail}};
  };
  QVariantList list;
  list << entry(QStringLiteral("python"), QStringLiteral("Python runtime"),
                QStringLiteral("Transcription, translation and voice-over"),
                pythonOk, true, false,
                pythonOk ? QStringLiteral("%1 (%2)").arg(
                               pyVersion.isEmpty()
                                   ? QStringLiteral("Available")
                                   : QStringLiteral("Python %1").arg(pyVersion),
                               bundledPy ? QStringLiteral("bundled")
                                         : QStringLiteral("system"))
                         : QStringLiteral("Not found"));
  list << entry(QStringLiteral("ffmpeg"), QStringLiteral("FFmpeg"),
                QStringLiteral("Media decoding, transcription and export"),
                ffmpegOk, true, false,
                ffmpegOk ? QStringLiteral("Available")
                         : QStringLiteral("Not found"));
  list << entry(QStringLiteral("edge_tts"), QStringLiteral("Edge TTS voices"),
                QStringLiteral("Text to Speech"), edgeOk, true, false,
                edgeOk ? QStringLiteral("Installed")
                       : QStringLiteral("Not installed"));
  list << entry(
      QStringLiteral("whisper"), QStringLiteral("Whisper (offline)"),
      QStringLiteral("Offline transcription without a cloud key"), whisperOk,
      false, !whisperOk,
      whisperOk
          ? QStringLiteral("Model ready (%1)").arg(whisperModelId)
          : QStringLiteral("Model not downloaded - click Download"));
  list << entry(QStringLiteral("demucs"), QStringLiteral("Demucs"),
                QStringLiteral("Vocal / music separation (highest quality)"),
                demucsOk, false, !demucsOk && canInstall,
                demucsOk ? QStringLiteral("Installed")
                         : QStringLiteral("Not installed - click Download"));
  list << entry(QStringLiteral("mdxnet"), QStringLiteral("MDX-Net (light)"),
                QStringLiteral("Vocal separation - lighter and faster on weak "
                               "computers"),
                mdxnetOk, false, !mdxnetOk && canInstall,
                mdxnetOk ? QStringLiteral("Installed")
                         : QStringLiteral("Not installed - click Download"));

  m_systemRequirements = list;
  m_requirementsChecking = false;
  // Save this result so the page can show it at next launch without re-probing.
  persistRequirements();
  emit systemRequirementsChanged();
}

// Imports each optional package in a child interpreter and reports what took.
// Kept on one logical line per statement so it survives as a -c argument.
static const QString kRequirementProbe = QStringLiteral(
    "import json,sys\n"
    "def has(m):\n"
    " try:\n"
    "  __import__(m)\n"
    "  return True\n"
    " except Exception:\n"
    "  return False\n"
    // Demucs is only usable if the module the worker actually loads
    // (demucs.separate) imports - which pulls in numpy and torch. A bare
    // 'import demucs' succeeds even when numpy is missing, so it would report
    // Installed for a build that dies the moment vocal removal is run.
    //
    // MDX-Net has the same trap: the worker loads audio_separator.separator,
    // which pulls in librosa and its audioread backend. A bare
    // 'import audio_separator' succeeds without them, so probing only the top
    // package reported Installed while the run died with "No module named
    // 'audioread'". Probe the submodule the worker really imports instead.
    "print(json.dumps({"
    "'python':'%d.%d.%d'%sys.version_info[:3],"
    "'edge_tts':has('edge_tts'),"
    "'mdxnet':has('audio_separator') and has('audio_separator.separator'),"
    "'demucs':has('demucs') and has('numpy') and has('demucs.separate')}))\n");

void Backend::checkSystemRequirements() {
  if (m_requirementsChecking)
    return;
  m_requirementsChecking = true;
  // Populate every row up front so opening the page never shows a blank panel:
  // the interpreter-free rows read real, the pip-probed ones sit at "checking".
  // On a re-check the list already holds the last result, so leave those rows in
  // place (no flicker) and only refresh placeholders on the very first probe.
  if (m_systemRequirements.isEmpty())
    buildRequirementPlaceholders();
  emit systemRequirementsChanged();

  // One interpreter answers for everything: the bundled Python is where the
  // cloud packages ship and where the optional engines get installed, so the
  // probe reflects exactly what the app will actually run.
  const QString python = PythonRuntime::executable(
      m_appSettings.value(QStringLiteral("pythonExecutable"),
                          QStringLiteral("python"))
          .toString());

  auto *proc = new QProcess(this);
  auto handled = std::make_shared<bool>(false);
  auto finish = [this, proc, handled](QVariantMap probe) {
    if (*handled)
      return;
    *handled = true;
    buildSystemRequirements(probe);
    proc->deleteLater();
  };

  connect(proc, &QProcess::errorOccurred, this,
          [finish, python](QProcess::ProcessError) {
            finish(QVariantMap{{QStringLiteral("pythonOk"), false},
                               {QStringLiteral("pythonPath"), python}});
          });
  connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [proc, finish, python](int code, QProcess::ExitStatus st) {
            QVariantMap probe;
            const bool ran = st == QProcess::NormalExit && code == 0;
            if (ran) {
              const QJsonDocument doc =
                  QJsonDocument::fromJson(proc->readAllStandardOutput());
              if (doc.isObject())
                probe = doc.object().toVariantMap();
            }
            probe[QStringLiteral("pythonOk")] = ran && !probe.isEmpty();
            probe[QStringLiteral("pythonPath")] = python;
            finish(probe);
          });
  proc->start(python, {QStringLiteral("-c"), kRequirementProbe});
}

void Backend::downloadWhisperModel() {
  const QString modelId = defaultWhisperModelId(
      m_appSettings
          .value(QStringLiteral("transcriptionModel"), QStringLiteral("small"))
          .toString());
  const QString fileName = WhisperModels::ggmlFileName(modelId);
  if (fileName.isEmpty()) {
    m_installStatus = QStringLiteral("No offline Whisper model to download.");
    emit requirementInstallChanged();
    return;
  }

  const QString dirPath = whisperModelsDir();
  if (!QDir().mkpath(dirPath)) {
    m_installStatus =
        QStringLiteral("Could not create the models/whisper folder.");
    emit requirementInstallChanged();
    return;
  }
  const QString finalPath = QDir(dirPath).filePath(fileName);
  // Written to .part and renamed on success, so a download killed halfway never
  // leaves a truncated file the worker would try to load.
  const QString partPath = finalPath + QStringLiteral(".part");
  auto file = std::make_shared<QFile>(partPath);
  if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_installStatus =
        QStringLiteral("Could not open %1 for writing.").arg(fileName);
    emit requirementInstallChanged();
    return;
  }

  if (!m_whisperNetwork)
    m_whisperNetwork = new QNetworkAccessManager(this);

  const QUrl url(
      QStringLiteral(
          "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/%1")
          .arg(fileName));
  QNetworkRequest request(url);
  // The resolve endpoint 302s to a CDN; follow it (Qt's default is safe-only).
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QByteArrayLiteral("CutPro"));

  m_installingId = QStringLiteral("whisper");
  m_installStatus =
      QStringLiteral("Downloading Whisper model %1...").arg(modelId);
  emit requirementInstallChanged();

  QNetworkReply *reply = m_whisperNetwork->get(request);
  m_whisperDownload = reply;

  connect(reply, &QNetworkReply::readyRead, this,
          [reply, file]() { file->write(reply->readAll()); });
  connect(reply, &QNetworkReply::downloadProgress, this,
          [this, modelId](qint64 received, qint64 total) {
            if (total > 0) {
              const int percent =
                  int(qBound<qint64>(0LL, received * 100 / total, 100LL));
              m_installStatus =
                  QStringLiteral("Downloading Whisper model %1 (%2%)")
                      .arg(modelId)
                      .arg(percent);
            } else {
              m_installStatus =
                  QStringLiteral("Downloading Whisper model %1 (%2 MB)")
                      .arg(modelId)
                      .arg(received / (1024 * 1024));
            }
            emit requirementInstallChanged();
          });
  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, file, finalPath, partPath, fileName]() {
        file->write(reply->readAll());
        file->close();
        const bool ok = reply->error() == QNetworkReply::NoError;
        m_whisperDownload = nullptr;
        m_installingId.clear();
        if (ok) {
          QFile::remove(finalPath);
          if (QFile::rename(partPath, finalPath)) {
            m_installStatus = QStringLiteral("Whisper model ready");
          } else {
            QFile::remove(partPath);
            m_installStatus =
                QStringLiteral("Downloaded but could not save %1").arg(fileName);
          }
        } else {
          QFile::remove(partPath);
          m_installStatus =
              QStringLiteral("Download failed - %1").arg(reply->errorString());
        }
        reply->deleteLater();
        emit requirementInstallChanged();
        // Reprobe so the row flips to Ready (or back to Download on failure).
        checkSystemRequirements();
      });
}

void Backend::installRequirement(const QString &id) {
  if (!m_installingId.isEmpty())
    return;

  // Offline Whisper is no longer a pip package: its "install" downloads the
  // default model's GGML weights straight into models/whisper/, so it works on
  // a machine with no Python at all - the whole reason for the native worker.
  if (id == QStringLiteral("whisper")) {
    downloadWhisperModel();
    return;
  }

  QString package;
  QStringList packages;
  if (id == QStringLiteral("demucs")) {
    package = QStringLiteral("demucs");
    // numpy is a real dependency of demucs, but on the embeddable interpreter
    // pip has been observed to resolve demucs without pulling it in, leaving a
    // build that imports demucs yet dies with "No module named 'numpy'" the
    // moment demucs.separate runs. Name it here so the install is complete.
    packages << package << QStringLiteral("numpy");
  } else if (id == QStringLiteral("mdxnet")) {
    // The [cpu] extra pulls onnxruntime (CPU) instead of torch, which is the
    // whole point: MDX-Net stays light enough for a weak machine.
    //
    // audioread is a real dependency (audio-separator -> librosa -> audioread)
    // but, like numpy for demucs, pip on the embeddable interpreter has been
    // observed to resolve audio-separator without landing it, leaving a build
    // that imports audio_separator yet dies with "No module named 'audioread'"
    // when audio_separator.separator loads. Name it here so the install is
    // complete.
    package = QStringLiteral("audio-separator");
    packages << QStringLiteral("audio-separator[cpu]")
             << QStringLiteral("audioread");
  } else {
    return;
  }

  // Install into the bundled interpreter (self-contained: no system Python
  // needed). pip was bootstrapped into it at build time.
  const QString python = PythonRuntime::executable(
      m_appSettings.value(QStringLiteral("pythonExecutable"),
                          QStringLiteral("python"))
          .toString());

  m_installingId = id;
  m_installStatus = QStringLiteral("Downloading %1 (this can take a while)...")
                        .arg(package);
  emit requirementInstallChanged();

  m_installProcess.disconnect();
  m_installProcess.setProcessChannelMode(QProcess::MergedChannels);
  connect(&m_installProcess, &QProcess::readyRead, this, [this]() {
    const QString text =
        QString::fromLocal8Bit(m_installProcess.readAll());
    const QStringList lines = text.split(
        QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts);
    if (!lines.isEmpty()) {
      m_installStatus = lines.last().trimmed().left(200);
      emit requirementInstallChanged();
    }
  });
  connect(&m_installProcess,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, package](int code, QProcess::ExitStatus st) {
            const bool ok = st == QProcess::NormalExit && code == 0;
            m_installStatus =
                ok ? QStringLiteral("Installed %1").arg(package)
                   : QStringLiteral("Install failed (%1) - see pip output")
                         .arg(code);
            m_installingId.clear();
            emit requirementInstallChanged();
            checkSystemRequirements();
          });
  connect(&m_installProcess, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError) {
            if (m_installingId.isEmpty())
              return;
            m_installStatus = QStringLiteral(
                "Could not start pip - is a system Python installed?");
            m_installingId.clear();
            emit requirementInstallChanged();
          });
  QStringList args{QStringLiteral("-m"), QStringLiteral("pip"),
                   QStringLiteral("install"), QStringLiteral("--upgrade")};
  args << packages;
  m_installProcess.start(python, args);
}

QString Backend::startTranscriptionJobDir(const QString &mediaId) {
  const QString root =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (root.isEmpty())
    return {};
  QDir dir(root);
  const QString relative =
      QStringLiteral("transcribe-windows/%1")
          .arg(QString(mediaId).replace(QRegularExpression("[^A-Za-z0-9_-]"),
                                        QStringLiteral("_")));
  if (!dir.mkpath(relative))
    return {};
  m_transcriptionJobDir = dir.filePath(relative);
  return m_transcriptionJobDir;
}

void Backend::clearTranscriptionJobDir() {
  if (m_transcriptionJobDir.isEmpty())
    return;
  // The whole directory goes, job file and any window WAV a killed worker left
  // behind. Nothing in here outlives the run that made it.
  QDir(m_transcriptionJobDir).removeRecursively();
  m_transcriptionJobDir.clear();
}

void Backend::cancelTranscription() {
  if (m_transcriptionBatch.inProgress()) {
    m_transcriptionBatch.cancel();
    return;
  }
  if (transcriptionInProgress()) {
    m_transcriptionCancelRequested = true;
    m_transcriptionProcess.kill();
    // Kill first: an extraction stopped while the worker is still waiting for it
    // would otherwise leave the worker blocked on a file nobody is writing.
    m_windowAudio.stop();
  }
}
bool Backend::importSubtitles(const QString &path) {
  QString error;
  QString language;
  const QString source = normalizePath(path);
  // read(), not readSrt(): the format is chosen by extension and then by the
  // file's own first bytes, so a YouTube .ttml download imports without the user
  // having to convert it first.
  const QVariantList segments = SubtitleIO::read(source, &error, &language);
  if (segments.isEmpty()) {
    setError(error);
    return false;
  }
  m_translator.clearStatus();
  m_transcript = segments;
  // TTML declares xml:lang, so an imported caption track already knows what it
  // is in and the translate step does not have to ask. SRT carries no such
  // thing, which is why this can still end up empty.
  m_transcriptLanguage = language.trimmed();
  m_transcriptionStatus =
      QStringLiteral("Imported %1 subtitle segments").arg(segments.size());
  markDirty();
  recordAction(QStringLiteral("subtitle-import"),
               QVariantMap{{"path", source},
                           {"segments", segments.size()},
                           {"language", m_transcriptLanguage}});
  emit transcriptChanged();
  return true;
}

bool Backend::exportTranscriptSrt(const QString &path) {
  if (m_transcript.isEmpty()) {
    setError(QStringLiteral("There is no transcript to export."));
    return false;
  }
  QString target = normalizePath(path);
  if (QFileInfo(target).suffix().isEmpty())
    target += QStringLiteral(".srt");
  QString error;
  // Dispatched on the extension rather than hardwired: this is the SRT command,
  // but a user who types a .ttml name into its dialog means TTML, and silently
  // writing SubRip into a file called .ttml would be the wrong answer.
  if (!SubtitleIO::write(target, m_transcript, &error)) {
    setError(error);
    return false;
  }
  const bool ttml = SubtitleIO::formatForPath(target) == SubtitleIO::Format::Ttml;
  m_transcriptionStatus =
      ttml ? QStringLiteral("TTML exported") : QStringLiteral("SRT exported");
  recordAction(QStringLiteral("subtitle-export"),
               QVariantMap{{"path", target}, {"segments", m_transcript.size()}});
  emit transcriptChanged();
  return true;
}

bool Backend::exportTranscriptTtml(const QString &path) {
  if (m_transcript.isEmpty()) {
    setError(QStringLiteral("There is no transcript to export."));
    return false;
  }
  QString target = normalizePath(path);
  if (QFileInfo(target).suffix().isEmpty())
    target += QStringLiteral(".ttml");
  QString error;
  if (!SubtitleIO::writeTtml(target, m_transcript, &error,
                             m_transcriptLanguage)) {
    setError(error);
    return false;
  }
  m_transcriptionStatus = QStringLiteral("TTML exported");
  recordAction(QStringLiteral("subtitle-export"),
               QVariantMap{{"path", target},
                           {"segments", m_transcript.size()},
                           {"format", QStringLiteral("ttml")}});
  emit transcriptChanged();
  return true;
}
bool Backend::updateTranscriptSegment(int index, const QString &text) {
  if (index < 0 || index >= m_transcript.size() || text.trimmed().isEmpty())
    return false;
  auto segment = m_transcript[index].toMap();
  segment["text"] = text.trimmed();
  m_transcript[index] = segment;
  // Same reason as a translation: the edit is only real once it reaches the
  // source list the sequence transcript is rebuilt from.
  writeTranscriptTextToSources(QVariantList{segment});
  recordAction(QStringLiteral("subtitle-edit"),
               QVariantMap{{"index", index}});
  bool subtitleChanged = false;
  for (auto &value : m_clips) {
    auto clip = value.toMap();
    if (clip.value("kind") != "subtitle" ||
        clip.value("transcriptIndex").toInt() != index)
      continue;
    clip["text"] = text.trimmed();
    clip["name"] = text.trimmed();
    value = clip;
    subtitleChanged = true;
  }
  emit transcriptChanged();
  markDirty();
  if (subtitleChanged) {
    emit clipsChanged();
    emit timelineChanged();
  }
  return true;
}
bool Backend::deleteTranscriptSegment(int index) {
  if (index < 0 || index >= m_transcript.size())
    return false;
  const QVariantMap segment = m_transcript.at(index).toMap();
  rememberState();

  // The transcript on screen is usually a view of m_sourceTranscripts, and
  // rebuildSequenceTranscript() regenerates it from there on the next
  // clipsChanged. Removing only the visible row would last exactly until then.
  QString ownerMediaId = segment.value(QStringLiteral("mediaId")).toString();
  int sourceIndex = segment.contains(QStringLiteral("sourceIndex"))
                        ? segment.value(QStringLiteral("sourceIndex")).toInt()
                        : -1;
  if (ownerMediaId.isEmpty() || sourceIndex < 0) {
    // A transcription whose media is not on the timeline yet is stored raw: the
    // rows carry no mediaId, and m_transcript is a copy of one source list. The
    // copy is only claimed when the row at this index is identical in both, so a
    // same-length list that happens to be a different take is left alone.
    for (auto it = m_sourceTranscripts.constBegin();
         it != m_sourceTranscripts.constEnd(); ++it) {
      if (it->size() != m_transcript.size())
        continue;
      const QVariantMap candidate = it->at(index).toMap();
      if (candidate.value(QStringLiteral("text")) !=
              segment.value(QStringLiteral("text")) ||
          candidate.value(QStringLiteral("start")) !=
              segment.value(QStringLiteral("start")) ||
          candidate.value(QStringLiteral("end")) !=
              segment.value(QStringLiteral("end")))
        continue;
      ownerMediaId = it.key();
      sourceIndex = index;
      break;
    }
  }
  bool sourcesChanged = false;
  if (!ownerMediaId.isEmpty() && sourceIndex >= 0) {
    const auto it = m_sourceTranscripts.find(ownerMediaId);
    if (it != m_sourceTranscripts.end() && sourceIndex < it->size()) {
      it->removeAt(sourceIndex);
      sourcesChanged = true;
    }
  }

  m_transcript.removeAt(index);
  // Every surviving clip above the hole moves down one, so the styling and the
  // timing already on those clips stay attached to the line they belong to.
  bool subtitleChanged = false;
  for (int clip = m_clips.size() - 1; clip >= 0; --clip) {
    QVariantMap value = m_clips.at(clip).toMap();
    if (value.value(QStringLiteral("kind")) != QStringLiteral("subtitle"))
      continue;
    const int mapped =
        value.value(QStringLiteral("transcriptIndex"), -1).toInt();
    if (mapped == index) {
      m_clips.removeAt(clip);
      subtitleChanged = true;
    } else if (mapped > index) {
      value[QStringLiteral("transcriptIndex")] = mapped - 1;
      m_clips[clip] = value;
      subtitleChanged = true;
    }
  }

  recordAction(QStringLiteral("subtitle-delete"),
               QVariantMap{{"index", index}});
  markDirty();
  emit transcriptChanged();
  if (subtitleChanged) {
    emit clipsChanged();
    emit timelineChanged();
  }
  // Re-derive so the sourceIndex stamped on the rows after the hole points at the
  // right source segment again; without this a later edit to one of them would
  // write its text over its neighbour. Returns untouched when the timeline has no
  // transcribable sources, which is the raw-transcript case handled above.
  if (sourcesChanged)
    rebuildSequenceTranscript();
  return true;
}
qint64 Backend::deleteTranscriptRange(int startIndex, int endIndex) {
  if (startIndex > endIndex)
    std::swap(startIndex, endIndex);
  if (startIndex < 0 || endIndex >= m_transcript.size())
    return 0;

  // Each row was stamped by rebuildSequenceTranscript() with its timeline
  // start/end (seconds) and the clip it sits in, so the span to remove is just
  // the first selected row's start to the last selected row's end.
  const QVariantMap firstSeg = m_transcript.at(startIndex).toMap();
  const QVariantMap lastSeg = m_transcript.at(endIndex).toMap();
  const QString clipId = firstSeg.value(QStringLiteral("clipId")).toString();
  if (clipId.isEmpty()) {
    // A raw transcription whose media is not on the timeline yet carries no
    // clipId - there is no footage under it to cut.
    setError(tr("Add this media to the timeline before editing it by "
                "transcript."));
    return 0;
  }
  const int clipIdx = clipIndex(clipId);
  if (clipIdx < 0)
    return 0;
  const QString primaryTrack =
      m_clips.at(clipIdx).toMap().value(QStringLiteral("track")).toString();
  if (trackLocked(primaryTrack)) {
    setError(tr("This track is locked."));
    return 0;
  }

  const qint64 aMs =
      qRound64(firstSeg.value(QStringLiteral("start")).toDouble() * 1000.0);
  const qint64 bMs =
      qRound64(lastSeg.value(QStringLiteral("end")).toDouble() * 1000.0);
  if (bMs <= aMs)
    return 0;

  // Tracks that follow the cut: the clip's own track plus every sync-locked
  // one. Reshaping exactly this set and rippling the same set keeps picture,
  // sound and captions aligned.
  QStringList syncLockedTracks;
  for (const auto &stateValue : trackStates()) {
    const auto state = stateValue.toMap();
    if (state.value(QStringLiteral("syncLocked"), true).toBool())
      syncLockedTracks.append(state.value(QStringLiteral("id")).toString());
  }
  const QVector<QPair<qint64, qint64>> cuts{{aMs, bMs}};

  rememberState();

  // Replace every media clip overlapping the removed span with the pieces that
  // survive (the part before the span and the part after it), each kept pointed
  // at the right moment of its source. Same reshape the silence cut does, for
  // one interval. Descending so an in-place splice does not move an index still
  // to be visited.
  bool anyReshape = false;
  for (int ci = m_clips.size() - 1; ci >= 0; --ci) {
    const QVariantMap original = m_clips.at(ci).toMap();
    const QString kind = original.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("video") && kind != QStringLiteral("audio") &&
        kind != QStringLiteral("image"))
      continue;
    const QString track = original.value(QStringLiteral("track")).toString();
    if ((track != primaryTrack && !syncLockedTracks.contains(track)) ||
        trackLocked(track))
      continue;
    const qint64 oStart = original.value(QStringLiteral("startMs")).toLongLong();
    const qint64 oDuration =
        original.value(QStringLiteral("durationMs")).toLongLong();
    const qint64 oEnd = oStart + oDuration;
    if (oEnd <= aMs || oStart >= bMs)
      continue; // no overlap with the removed span
    const qint64 oSourceIn =
        original.value(QStringLiteral("sourceInMs")).toLongLong();

    QVector<QPair<qint64, qint64>> survivors;
    if (oStart < aMs)
      survivors.append({oStart, qMin(aMs, oEnd)});
    if (oEnd > bMs)
      survivors.append({qMax(bMs, oStart), oEnd});

    QVariantList pieces;
    for (const auto &s : survivors) {
      QVariantMap piece = original;
      piece[QStringLiteral("id")] = id("clip");
      piece[QStringLiteral("startMs")] = s.first; // ripple pulls it back below
      piece[QStringLiteral("sourceInMs")] = oSourceIn + (s.first - oStart);
      piece[QStringLiteral("durationMs")] = s.second - s.first;
      pieces.append(piece);
    }
    m_clips.removeAt(ci);
    for (int k = pieces.size() - 1; k >= 0; --k)
      m_clips.insert(ci, pieces.at(k));
    anyReshape = true;
  }

  if (!anyReshape) {
    // Nothing under the span - undo the snapshot we just took.
    m_undo.removeLast();
    emit historyChanged();
    return 0;
  }

  // Pull everything after the span back to close the hole.
  TimelineEditor::rippleRemoveIntervals(m_clips, primaryTrack, cuts,
                                        syncLockedTracks);

  pruneEmptyTracks(true);
  clearError();
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  if (!m_selectedClipId.isEmpty() && clipIndex(m_selectedClipId) < 0) {
    m_selectedClipId.clear();
    emit selectionChanged();
    m_selectionDetailNotify.schedule();
    m_colorSettingsNotify.schedule();
  }
  // Land the playhead on the join, where the removed span used to begin.
  setPlayheadMs(aMs);
  // The transcript is derived from the clips; rebuild now rather than waiting
  // for the debounced clipsChanged so the panel updates in the same tick.
  rebuildSequenceTranscript();
  return bMs - aMs;
}
bool Backend::addTranscriptToTimeline() {
  if (m_transcript.isEmpty()) {
    setError(QStringLiteral("There is no transcript to add to the timeline."));
    return false;
  }
  QVariantList subtitleClips =
      SubtitleTimeline::clipsFromTranscript(m_transcript);
  if (subtitleClips.isEmpty()) {
    setError(QStringLiteral("The transcript has no timed subtitle segments."));
    return false;
  }
  if (m_sequenceId.isEmpty())
    createSequence();
  rememberState();
  for (int index = m_clips.size() - 1; index >= 0; --index) {
    if (m_clips[index].toMap().value("kind") == "subtitle")
      m_clips.removeAt(index);
  }
  for (auto &value : subtitleClips) {
    auto clip = value.toMap();
    clip["id"] = id("clip");
    clip["mediaId"] = QString();
    m_clips.append(clip);
  }
  markDirty();
  clearError();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}
bool Backend::removeTranscriptFromTimeline() {
  QStringList subtitleIds;
  for (const auto &value : m_clips) {
    const QVariantMap clip = value.toMap();
    if (clip.value("kind") == "subtitle")
      subtitleIds.append(clip.value("id").toString());
  }
  if (subtitleIds.isEmpty())
    return false;
  return removeClips(subtitleIds);
}
bool Backend::translateTranscript(const QString &targetLanguage,
                                  const QVariantMap &settings) {
  // The full set the translation worker and the UI dropdown offer. This used to
  // list only en/zh-CN/km/es, so Korean, Japanese and the rest failed silently
  // even though translate_transcript.py handles them.
  static const QStringList supported{
      QStringLiteral("en"), QStringLiteral("zh-CN"), QStringLiteral("km"),
      QStringLiteral("es"), QStringLiteral("fr"),    QStringLiteral("de"),
      QStringLiteral("ja"), QStringLiteral("ko"),    QStringLiteral("vi"),
      QStringLiteral("th"), QStringLiteral("id"),    QStringLiteral("pt"),
      QStringLiteral("ru"), QStringLiteral("it"),    QStringLiteral("hi"),
      QStringLiteral("ar")};
  if (m_transcript.isEmpty()) {
    setError(QStringLiteral("There is no transcript to translate."));
    return false;
  }
  if (!supported.contains(targetLanguage)) {
    setError(QStringLiteral("That translation language is not supported."));
    return false;
  }
  if (transcriptionInProgress()) {
    setError(QStringLiteral("Wait for transcription to finish first."));
    return false;
  }
  const QVariantMap translatorSettings =
      settings.isEmpty() ? m_appSettings : AppSettings::normalized(settings);
  if (!m_translator.start(m_transcript, targetLanguage, translatorSettings)) {
    setError(m_translator.status());
    return false;
  }
  recordAction(QStringLiteral("subtitle-translate"),
               QVariantMap{{"language", targetLanguage}});
  clearError();
  return true;
}
bool Backend::testTranslationProvider(const QVariantMap &settings) {
  const QVariantMap translatorSettings =
      settings.isEmpty() ? m_appSettings : AppSettings::normalized(settings);
  if (!m_translator.test(translatorSettings)) {
    setError(m_translator.status());
    return false;
  }
  clearError();
  return true;
}
void Backend::cancelTranslation() { m_translator.cancel(); }
bool Backend::generateRecap(const QVariantMap &settings) {
  if (m_transcript.isEmpty()) {
    setError(QStringLiteral("There is no transcript to recap."));
    return false;
  }
  if (transcriptionInProgress()) {
    setError(QStringLiteral("Wait for transcription to finish first."));
    return false;
  }
  if (recapInProgress()) {
    setError(QStringLiteral("A recap is already being generated."));
    return false;
  }
  QVariantMap recapSettings =
      settings.isEmpty() ? m_appSettings : AppSettings::normalized(settings);
  // Recap uses the Gemini translator's keys - the same secret the user already
  // configured once in Settings > Translation.
  if (recapSettings.value("translationGeminiApiKeys").toString().trimmed()
          .isEmpty()) {
    setError(QStringLiteral("Add a Gemini API key in Settings > Translation "
                            "before generating a recap."));
    return false;
  }
  // A human-readable title for the prompt, if the transcript came from a
  // known source clip.
  QString title;
  if (!m_transcriptionMediaId.isEmpty())
    title = mediaById(m_transcriptionMediaId).value("name").toString();
  if (!m_recap.start(m_transcript, m_transcriptLanguage, title, recapSettings)) {
    setError(m_recap.status());
    return false;
  }
  recordAction(QStringLiteral("subtitle-recap"),
               QVariantMap{{"segments", m_transcript.size()}});
  clearError();
  return true;
}
void Backend::cancelRecap() { m_recap.cancel(); }
bool Backend::saveRecapText(const QString &path, const QString &text) {
  QString target = path;
  if (target.startsWith(QStringLiteral("file://")))
    target = QUrl(target).toLocalFile();
  if (target.trimmed().isEmpty()) {
    setError(QStringLiteral("Choose where to save the recap."));
    return false;
  }
  QSaveFile file(target);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
      file.write(text.toUtf8()) < 0 || !file.commit()) {
    setError(QStringLiteral("Could not save the recap to %1.").arg(target));
    return false;
  }
  recordAction(QStringLiteral("subtitle-recap-save"), QVariantMap{{"path", target}});
  clearError();
  return true;
}
void Backend::undo() {
  if (m_undo.isEmpty())
    return;
  m_redo.append(stateVariant());
  restoreStateVariant(m_undo.takeLast(), true);
  recordAction(QStringLiteral("undo"));
  markDirty();
  emit historyChanged();
}
void Backend::redo() {
  if (m_redo.isEmpty())
    return;
  m_undo.append(stateVariant());
  restoreStateVariant(m_redo.takeLast(), true);
  recordAction(QStringLiteral("redo"));
  markDirty();
  emit historyChanged();
}
void Backend::clearError() {
  if (m_lastError.isEmpty())
    return;
  m_lastError.clear();
  emit errorChanged();
}

QString Backend::normalizePath(const QString &value) const {
  if (value.trimmed().isEmpty())
    return {};
  QUrl u(value);
  return u.isLocalFile() ? QDir::cleanPath(u.toLocalFile())
                         : QDir::cleanPath(value);
}
QStringList Backend::expandImportPaths(const QStringList &paths) const {
  // MediaScan owns the traversal: bounded depth, bounded file count, a time
  // budget, symlink-loop detection and O(1) duplicate keys. The old inline
  // QDirIterator walk had none of those and ran on the GUI thread.
  return MediaScan::expand(paths).files;
}
QVariantMap Backend::mediaById(const QString &idv) const {
  int i = mediaIndex(idv);
  return i < 0 ? QVariantMap{} : m_media[i].toMap();
}

void Backend::scheduleSequenceTranscriptRebuild() {
  // restart(), not start-if-inactive: the point is to fire once the timeline
  // has stopped moving, so every further change pushes the rebuild back.
  m_transcriptRebuildTimer.start();
}

bool Backend::writeTranscriptTextToSources(const QVariantList &segments) {
  bool changed = false;
  for (const QVariant &value : segments) {
    const QVariantMap segment = value.toMap();
    const QString mediaId =
        segment.value(QStringLiteral("mediaId")).toString();
    // Only a segment this build derived from a source carries both keys. An
    // imported SRT and a raw transcription result have no source list to write
    // back to, and they are the transcript themselves, so they need none.
    if (mediaId.isEmpty() || !segment.contains(QStringLiteral("sourceIndex")))
      continue;
    const auto it = m_sourceTranscripts.find(mediaId);
    if (it == m_sourceTranscripts.end())
      continue;
    const int sourceIndex = segment.value(QStringLiteral("sourceIndex")).toInt();
    if (sourceIndex < 0 || sourceIndex >= it->size())
      continue;
    const QString text = segment.value(QStringLiteral("text")).toString();
    QVariantMap source = it->at(sourceIndex).toMap();
    if (source.value(QStringLiteral("text")).toString() == text)
      continue;
    source[QStringLiteral("text")] = text;
    (*it)[sourceIndex] = source;
    changed = true;
  }
  return changed;
}

bool Backend::applyTranscriptTextToSubtitleClips() {
  bool changed = false;
  for (auto &value : m_clips) {
    QVariantMap clip = value.toMap();
    if (clip.value(QStringLiteral("kind")) != QStringLiteral("subtitle"))
      continue;
    const int index =
        clip.value(QStringLiteral("transcriptIndex"), -1).toInt();
    if (index < 0 || index >= m_transcript.size())
      continue;
    const QVariantMap segment = m_transcript.at(index).toMap();
    const QString text = segment.value(QStringLiteral("text")).toString();
    const QVariant wordTimes = segment.value(QStringLiteral("wordTimes"));
    if (text.isEmpty() || (clip.value(QStringLiteral("text")).toString() == text &&
                           clip.value(QStringLiteral("wordTimes")) == wordTimes))
      continue;
    clip[QStringLiteral("text")] = text;
    clip[QStringLiteral("name")] = text;
    // Word timings belong to the words that were said. A retimed or retranslated
    // line carries none, and the karaoke solver falls back rather than lighting
    // up the wrong word.
    if (wordTimes.isValid())
      clip[QStringLiteral("wordTimes")] = wordTimes;
    else
      clip.remove(QStringLiteral("wordTimes"));
    value = clip;
    changed = true;
  }
  return changed;
}

void Backend::rebuildSequenceTranscript() {
  CUTPRO_GUI_SCOPE("Backend::rebuildSequenceTranscript");
  m_transcriptRebuildTimer.stop();
  QVariantList rebuilt;
  struct TimedSegment {
    qint64 startMs = 0;
    QVariantMap segment;
  };
  QVector<TimedSegment> timed;
  bool hasSequenceSources = false;

  // Cues carry no media, so the whole subtitle track is skipped here without
  // being visited: on a transcribed timeline this is a handful of clips instead
  // of twenty thousand.
  const QVariantList candidates = mediaClips();
  for (const QVariant &value : candidates) {
    const QVariantMap clip = value.toMap();
    const QString kind = clip.value("kind").toString();
    const QString mediaId = clip.value("mediaId").toString();
    if ((kind != QStringLiteral("video") && kind != QStringLiteral("audio")) ||
        mediaId.isEmpty())
      continue;
    // isTranscribableMedia() answers from a cached set, so the generated-speech
    // test no longer scans the bin - and no longer converts a QVariantMap per
    // bin entry - once per clip.
    if (!isTranscribableMedia(mediaId))
      continue;
    hasSequenceSources = true;
    if (!m_sourceTranscripts.contains(mediaId))
      continue;

    const qint64 clipStart = clip.value("startMs").toLongLong();
    const qint64 sourceIn = clip.value("sourceInMs").toLongLong();
    const qint64 sourceOut = sourceIn + clip.value("durationMs").toLongLong();
    const QVariantList sourceSegments = m_sourceTranscripts.value(mediaId);
    for (int sourceIndex = 0; sourceIndex < sourceSegments.size();
         ++sourceIndex) {
      const QVariantMap source = sourceSegments.at(sourceIndex).toMap();
      const double sourceStart = source.value("start").toDouble();
      const double sourceEnd = source.value("end").toDouble();
      const qint64 startMs = qRound64(sourceStart * 1000.0);
      const qint64 endMs = qRound64(sourceEnd * 1000.0);
      if (endMs <= sourceIn || startMs >= sourceOut)
        continue;

      const qint64 clippedStart = qMax(startMs, sourceIn);
      const qint64 clippedEnd = qMin(endMs, sourceOut);
      QVariantMap segment = source;
      segment["start"] = (clipStart + clippedStart - sourceIn) / 1000.0;
      segment["end"] = (clipStart + clippedEnd - sourceIn) / 1000.0;
      segment["mediaId"] = mediaId;
      segment["clipId"] = clip.value("id").toString();
      // Where this line came from, so an edit or a translation of the derived
      // view can be written back to the exact source segment it belongs to.
      // Without it the write-back would have to match on text, which is the one
      // thing that changes.
      segment["sourceIndex"] = sourceIndex;
      timed.push_back({clipStart + clippedStart - sourceIn, segment});
    }
  }

  std::sort(timed.begin(), timed.end(), [](const TimedSegment &a,
                                           const TimedSegment &b) {
    return a.startMs < b.startMs;
  });
  qint64 previousEndMs = 0;
  for (const TimedSegment &entry : timed) {
    QVariantMap segment = entry.segment;
    const qint64 startMs =
        qMax(previousEndMs, qRound64(segment.value("start").toDouble() * 1000.0));
    const qint64 endMs = qMax(
        startMs + 1, qRound64(segment.value("end").toDouble() * 1000.0));
    segment["start"] = startMs / 1000.0;
    segment["end"] = endMs / 1000.0;
    rebuilt.append(segment);
    previousEndMs = endMs;
  }

  if (!hasSequenceSources)
    return;

  if (rebuilt.isEmpty()) {
    // Nothing on the timeline carries a per-source transcript, so there is
    // nothing to derive from - and a derivation from nothing must not be allowed
    // to pass for a result. This ran on every clipsChanged, so importing an SRT
    // and dropping it on the timeline destroyed the transcript with the very
    // signal that consumed it: the segment list emptied while
    // m_transcriptionStatus still read "Imported N subtitle segments".
    //
    // Only a transcript this function produced may be cleared by it. Its own
    // segments carry the clip they were cut from; an imported SRT and a raw
    // transcription result carry no clipId, so they survive a rebuild that has
    // no sources to work with.
    bool ownedByRebuild = false;
    for (const QVariant &value : m_transcript) {
      if (value.toMap().contains(QStringLiteral("clipId"))) {
        ownedByRebuild = true;
        break;
      }
    }
    if (!ownedByRebuild)
      return;
  }

  if (rebuilt != m_transcript) {
    m_transcript = rebuilt;
    emit transcriptChanged();
  }
}

int Backend::mediaIndex(const QString &v) const {
  ensureMediaCaches();
  return m_cachedMediaIndex.value(v, -1);
}
int Backend::clipIndex(const QString &v) const {
  ensureClipCaches();
  return m_cachedClipIndex.value(v, -1);
}
QVariantMap Backend::stateVariant() const {
  QVariantMap sourceTranscripts;
  for (auto it = m_sourceTranscripts.cbegin(); it != m_sourceTranscripts.cend();
       ++it)
    sourceTranscripts.insert(it.key(), it.value());
  QVariantMap sourceLanguages;
  for (auto it = m_sourceTranscriptLanguages.cbegin();
       it != m_sourceTranscriptLanguages.cend(); ++it)
    sourceLanguages.insert(it.key(), it.value());
  // How far a windowed transcription got, so a cancelled long run can be
  // continued in a later session instead of transcribing eight hours again.
  QVariantMap transcriptCoverage;
  for (auto it = m_transcriptCoverageMs.cbegin();
       it != m_transcriptCoverageMs.cend(); ++it)
    transcriptCoverage.insert(it.key(), it.value());
  return QVariantMap{
      {"schemaVersion", 4},
      {"projectId", m_projectId},
      {"projectName", m_projectName},
      {"projectLocation", m_projectLocation},
      {"sequenceId", m_sequenceId},
      {"sequenceName", m_sequenceName},
      {"videoTrackCount", m_videoTrackCount},
      {"audioTrackCount", m_audioTrackCount},
      {"minVideoTracks", m_minVideoTracks},
      {"minAudioTracks", m_minAudioTracks},
      {"mutedTracks", m_mutedTracks},
      {"trackStates", m_trackStates},
      {"markers", m_markers},
      {"snappingEnabled", m_snappingEnabled},
      {"captionStyle", m_captionStyle.toJson().toVariantMap()},
      {"colorSettings", m_colorSettings},
      {"media", m_media},
      {"clips", m_clips},
      // Animation channels. They used to live only in memory, so every
      // keyframe was lost on save - which made the stopwatch a
      // session-only toy.
      {"keyframes", m_keyframeEngine.serialize()},
      {"transcript", m_transcript},
      {"transcriptLanguage", m_transcriptLanguage},
      {"sourceTranscripts", sourceTranscripts},
      {"sourceTranscriptLanguages", sourceLanguages},
      // What the interface was set to. loadProject() prefers the whole-state JSON
      // row, so a new key here round-trips through the project file, the SQLite
      // mirror and undo with no schema migration.
      {"uiState", m_uiState},
      {"transcriptCoverageMs", transcriptCoverage}};
}

// Only the paths that genuinely need JSON - the project file, the SQLite mirror
// column - pay for this conversion. Undo does not.
QJsonObject Backend::stateObject() const {
  return QJsonObject::fromVariantMap(stateVariant());
}

// A price, not a measurement. Text length is gone as a proxy, so a state is
// charged for the list spines it may own outright plus a flat per-entry share of
// the maps inside them; comparing that against the same byte ceiling keeps deep
// histories of a long timeline from growing without bound.
qint64 Backend::approximateStateBytes(const QVariantMap &state) {
  static constexpr qint64 kPerEntryBytes = 320;
  qint64 entries = 0;
  for (const char *key : {"clips", "media", "markers", "transcript", "keyframes"})
    entries += state.value(QLatin1String(key)).toList().size();
  return entries * kPerEntryBytes;
}

QByteArray Backend::serializeState(bool pretty) const {
  return QJsonDocument(stateObject())
      .toJson(pretty ? QJsonDocument::Indented : QJsonDocument::Compact);
}
bool Backend::restoreState(const QByteArray &data, bool history) {
  QJsonParseError e;
  auto d = QJsonDocument::fromJson(data, &e);
  if (!d.isObject()) {
    setError(QStringLiteral("Invalid project file: ") + e.errorString());
    return false;
  }
  return restoreStateVariant(d.object().toVariantMap(), history);
}
bool Backend::restoreStateVariant(const QVariantMap &o, bool history) {
  // These three read like the QJsonValue accessors this function was written
  // against: a fallback wins only when the stored value is absent or of the
  // wrong type, never when it is a legitimately empty string or a zero.
  const auto stringOr = [&o](const char *key, const QString &fallback) {
    const QVariant v = o.value(QLatin1String(key));
    return v.typeId() == QMetaType::QString ? v.toString() : fallback;
  };
  const auto intOr = [&o](const char *key, int fallback) {
    const QVariant v = o.value(QLatin1String(key));
    bool ok = false;
    const int n = v.toInt(&ok);
    return ok ? n : fallback;
  };
  const auto boolOr = [&o](const char *key, bool fallback) {
    const QVariant v = o.value(QLatin1String(key));
    return v.typeId() == QMetaType::Bool ? v.toBool() : fallback;
  };
  const int schemaVersion = o.value(QStringLiteral("schemaVersion")).toInt();
  if (schemaVersion < 1 || schemaVersion > 4) {
    setError(QStringLiteral("Unsupported project file version."));
    return false;
  }
  if (!m_customBlurEditClipId.isEmpty() ||
      !m_customBlurEditInstanceId.isEmpty()) {
    m_customBlurEditClipId.clear();
    m_customBlurEditInstanceId.clear();
    emit customBlurEditChanged();
  }
  m_projectId = stringOr("projectId", id("project"));
  m_projectName = stringOr("projectName", QStringLiteral("Untitled"));
  m_projectLocation = stringOr("projectLocation", QString());
  m_sequenceId = stringOr("sequenceId", QString());
  m_sequenceName = stringOr("sequenceName", QStringLiteral("Sequence 01"));
  m_videoTrackCount = qBound(1, intOr("videoTrackCount", 1), 64);
  // 0 is a valid saved audio count: projects without audio clips have no
  // audio lane at all.
  m_audioTrackCount = qBound(0, intOr("audioTrackCount", 0), 64);
  // Projects saved before the floors existed fall back to the counts they were
  // saved with, so opening one does not silently delete its empty tracks.
  m_minVideoTracks =
      qBound(1, intOr("minVideoTracks", m_videoTrackCount), 64);
  m_minAudioTracks =
      qBound(0, intOr("minAudioTracks", m_audioTrackCount), 64);
  m_trackStates = o.value(QStringLiteral("trackStates")).toMap();
  m_markers = o.value(QStringLiteral("markers")).toList();
  m_snappingEnabled = boolOr("snappingEnabled", true);
  m_mutedTracks.clear();
  for (const QString &value :
       o.value(QStringLiteral("mutedTracks")).toStringList()) {
    const QString track = value.trimmed().toUpper();
    if (!m_mutedTracks.contains(track))
      m_mutedTracks.append(track);
  }
  m_captionStyle = CaptionStyle::fromJson(QJsonObject::fromVariantMap(
      o.value(QStringLiteral("captionStyle")).toMap()));
  m_colorSettings = ColorSettings::defaults();
  m_selectedClipId.clear();
  if (!history)
    m_effectEditSelection.clear();
  const QVariantMap savedColorSettings =
      o.value(QStringLiteral("colorSettings")).toMap();
  for (auto it = savedColorSettings.cbegin(); it != savedColorSettings.cend();
       ++it)
    ColorSettings::setProjectValue(&m_colorSettings, it.key(), it.value());
  m_media = o.value(QStringLiteral("media")).toList();
  // Older projects predate the long-media presentation policy. Normalize the
  // media records during load so their existing clips do not recreate the
  // eager timeline/monitor path on the first repaint.
  for (auto &value : m_media) {
    auto media = value.toMap();
    LargeMediaPolicy::applyPresentationFlags(&media);
    value = media;
  }
  m_clips = o.value(QStringLiteral("clips")).toList();
  m_keyframeEngine.restore(o.value(QStringLiteral("keyframes")).toList());
  const bool hasSavedTranscript = o.contains(QStringLiteral("transcript"));
  const QVariantList savedTranscript =
      o.value(QStringLiteral("transcript")).toList();
  const QString savedTranscriptLanguage = stringOr("transcriptLanguage", QString());
  m_sourceTranscripts.clear();
  const QVariantMap savedSourceTranscripts =
      o.value(QStringLiteral("sourceTranscripts")).toMap();
  for (auto it = savedSourceTranscripts.cbegin();
       it != savedSourceTranscripts.cend(); ++it)
    m_sourceTranscripts.insert(it.key(), it.value().toList());
  m_sourceTranscriptLanguages.clear();
  const QVariantMap savedSourceLanguages =
      o.value(QStringLiteral("sourceTranscriptLanguages")).toMap();
  for (auto it = savedSourceLanguages.cbegin();
       it != savedSourceLanguages.cend(); ++it)
    m_sourceTranscriptLanguages.insert(it.key(), it.value().toString());
  m_transcriptCoverageMs.clear();
  const QVariantMap savedCoverage =
      o.value(QStringLiteral("transcriptCoverageMs")).toMap();
  for (auto it = savedCoverage.cbegin(); it != savedCoverage.cend(); ++it)
    m_transcriptCoverageMs.insert(it.key(), it.value().toLongLong());
  // Interface state is merged, not replaced: a project saved before this existed
  // carries none, and forgetting the panel the user is in because they opened an
  // older project would be a regression of its own.
  const QVariantMap savedUiState = o.value(QStringLiteral("uiState")).toMap();
  if (!savedUiState.isEmpty()) {
    for (auto it = savedUiState.cbegin(); it != savedUiState.cend(); ++it)
      m_uiState.insert(it.key(), it.value());
    emit uiStateChanged();
  }
  if (schemaVersion < 4)
    TimelineClipBinding::collapseLegacyEmbeddedAudio(&m_clips, m_media);
  // Normalisation, but only where it changes something. Every write into a clip
  // map here detaches it from the snapshot it came from, so writing all of them
  // unconditionally meant an undo on a transcribed timeline deep-copied twenty
  // thousand maps to arrive at the values they already held.
  for (auto &value : m_clips) {
    QVariantMap clip = value.toMap();
    const QString kind = clip.value("kind").toString();
    const QVariantMap media = mediaById(clip.value("mediaId").toString());
    bool changed = false;
    if (LargeMediaPolicy::requiresLightweightHandling(media) &&
        clip.value("timelineRenderMode").toString() !=
            QLatin1String("lightweight")) {
      clip["timelineRenderMode"] = QStringLiteral("lightweight");
      changed = true;
    }
    QString track =
        TimelinePlacement::normalizedTrack(clip.value("track").toString());
    if (!TimelinePlacement::trackAcceptsKind(track, kind))
      track = TimelinePlacement::defaultTrackForKind(kind);
    if (clip.value("sourceDurationMs").toLongLong() <= 0) {
      const qint64 currentExtent = clip.value("sourceInMs").toLongLong() +
                                   clip.value("durationMs").toLongLong();
      clip["sourceDurationMs"] =
          qMax(currentExtent, media.value("durationMs").toLongLong());
      changed = true;
    }
    if (clip.value("track").toString() != track) {
      clip["track"] = track;
      changed = true;
    }
    const QVariantList savedStack = clip.value("effectStack").toList();
    if (savedStack.isEmpty()) {
      if (clip.contains(QStringLiteral("effectStack"))) {
        clip.remove(QStringLiteral("effectStack"));
        changed = true;
      }
    } else {
      QVariantList stack = EffectStack::normalized(savedStack);
      for (auto &effectValue : stack) {
        QVariantMap instance = effectValue.toMap();
        if (instance.value("id").toString().isEmpty())
          instance["id"] = id("effect");
        effectValue = instance;
      }
      if (stack.isEmpty()) {
        clip.remove(QStringLiteral("effectStack"));
        changed = true;
      } else if (stack != savedStack) {
        clip["effectStack"] = stack;
        changed = true;
      }
    }
    if (changed)
      value = clip;
    ensureTrackExists(track);
  }
  for (int i = m_markers.size() - 1; i >= 0; --i) {
    auto marker = m_markers.at(i).toMap();
    if (marker.value("id").toString().isEmpty())
      marker["id"] = id("marker");
    marker["positionMs"] = qBound<qint64>(
        0, marker.value("positionMs").toLongLong(), durationMs());
    marker["name"] = marker.value("name", QStringLiteral("Marker"));
    marker["color"] = marker.value("color", QStringLiteral("#59a7ff"));
    m_markers[i] = marker;
  }
  for (int i = m_mutedTracks.size() - 1; i >= 0; --i) {
    const QString track = m_mutedTracks.at(i);
    const bool valid =
        track.size() > 1 &&
        ((track.startsWith('V') && track.mid(1).toInt() <= m_videoTrackCount) ||
         (track.startsWith('A') && track.mid(1).toInt() <= m_audioTrackCount));
    if (!valid)
      m_mutedTracks.removeAt(i);
  }
  m_playheadMs = qBound<qint64>(0, m_playheadMs, durationMs());
  if (!history)
    clearError();
  rebuildSequenceTranscript();
  if (hasSavedTranscript) {
    m_transcript = savedTranscript;
    m_transcriptLanguage = savedTranscriptLanguage;
  }
  emitAllStateChanged();
  // A project saved before its long sources were filmstripped - or one whose
  // cache has since been cleared - carries media with no timeline preview at all.
  // Undo and redo restores are skipped: they replay maps this has already run
  // over, and the artefacts they need are on disk by then.
  if (!history) {
    for (const QVariant &value : m_media) {
      const QVariantMap item = value.toMap();
      if (needsDeferredPreview(item))
        startDeferredMediaPreview(item);
    }
  }
  return true;
}
void Backend::rememberState() {
  // Inside an edit batch the outermost beginEditBatch() already took the one
  // snapshot the whole action gets; the per-item mutators that call this again
  // must not each push another undo state (that was N undo steps for one scene
  // detect, and N full-project snapshots on the GUI thread).
  if (m_editBatchDepth > 0)
    return;
  // Taken as values, not as text. This used to serialise the entire project to
  // JSON here on every undoable edit - ~945 ms with a 19831-cue subtitle track,
  // on the GUI thread, for a snapshot the action log then usually threw away.
  const QVariantMap snapshot = stateVariant();
  recordAction(QStringLiteral("edit"), {}, snapshot);
  m_undo << snapshot;
  qint64 bytes = 0;
  for (const QVariantMap &state : m_undo)
    bytes += approximateStateBytes(state);
  while (m_undo.size() > 1 &&
         (m_undo.size() > kMaxUndoStates || bytes > kMaxUndoBytes)) {
    bytes -= approximateStateBytes(m_undo.constFirst());
    m_undo.removeFirst();
  }
  m_redo.clear();
  emit historyChanged();
}
void Backend::beginEditBatch(bool liveParameter) {
  // Snapshot once, before anything mutates, so one endEditBatch() undoes the
  // whole action. Nested begins just deepen the count.
  if (m_editBatchDepth == 0) {
    if (liveParameter)
      rememberStateForLiveEdit();
    else
      rememberState();
    m_editBatchPayloadOnly = false;
    m_editBatchClipsDirty = false;
    m_editBatchMarkersDirty = false;
  }
  ++m_editBatchDepth;
}
void Backend::endEditBatch() {
  if (m_editBatchDepth == 0)
    return;
  if (--m_editBatchDepth > 0)
    return;
  // One projection and one ruler update for the whole batch, in place of the N
  // the per-item mutators would otherwise have fired.
  const bool clips = m_editBatchClipsDirty;
  const bool markers = m_editBatchMarkersDirty;
  m_editBatchClipsDirty = false;
  m_editBatchMarkersDirty = false;
  if (clips) {
    if (m_editBatchPayloadOnly)
      publishClipStackUpdate();
    else
      emit clipsChanged();
  }
  if (markers)
    emit markersChanged();
  if ((clips && !m_editBatchPayloadOnly) || markers)
    emit timelineChanged();
  m_editBatchPayloadOnly = false;
}
void Backend::beginLiveParameterEdit() {
  m_liveParameterEditActive = true;
  m_liveParameterSnapshotTaken = false;
}
void Backend::endLiveParameterEdit() {
  m_liveParameterEditActive = false;
  m_liveParameterSnapshotTaken = false;
}
void Backend::rememberStateForLiveEdit() {
  if (m_liveParameterEditActive) {
    // One snapshot for the whole gesture: the first mutation of the drag takes
    // it, every later mouse-move tick skips straight past the expensive
    // stateVariant() deep-copy.
    if (m_liveParameterSnapshotTaken)
      return;
    m_liveParameterSnapshotTaken = true;
  }
  rememberState();
}
void Backend::markDirty(bool v) {
  if (v)
    scheduleProjectDatabaseSave();
  if (m_dirty == v)
    return;
  m_dirty = v;
  emit dirtyChanged();
}
void Backend::recordAction(const QString &type, const QVariantMap &payload,
                           const QVariantMap &stateSnapshot) {
  // Only needs the connection open to append one log row - not a full project
  // rewrite, which persistProjectDatabase() would do on the GUI thread here.
  if (!m_projectDatabase.isOpen() && !openProjectDatabase())
    return;
  // The state column used to be filled on every single edit with a freshly
  // serialised copy of the whole project. Nothing reads it - crash recovery in
  // loadProject() comes from the normalised project tables, undo comes from
  // m_undo in memory, and ProjectDatabase::actions() has no callers at all - yet
  // with a subtitle track on the timeline it was multi-megabyte JSON, widened to
  // UTF-16 and inserted synchronously. The tracer caught the whole chain at
  // 2027 ms of "Not Responding" on one button press:
  //   Backend::removeTranscriptFromTimeline -> removeClips -> rememberState
  //     -> recordAction -> serializeState -> QJsonArray::fromVariantList
  // The row is what makes the log a history, so it still goes in; the snapshot
  // only rides along when a caller already had one to give and enough time has
  // passed for another copy to be worth what it costs - and the JSON is built
  // here, on that rare row, rather than by every caller for every row.
  QByteArray state;
  if (!stateSnapshot.isEmpty()) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastActionSnapshotMs == 0 ||
        now - m_lastActionSnapshotMs >= kActionSnapshotIntervalMs) {
      state = QJsonDocument(QJsonObject::fromVariantMap(stateSnapshot))
                  .toJson(QJsonDocument::Compact);
      m_lastActionSnapshotMs = now;
    }
  }
  m_projectDatabase.appendAction(m_projectId, m_sequenceId, type, payload,
                                 state);
}
void Backend::setError(const QString &v) {
  if (m_lastError == v.trimmed())
    return;
  m_lastError = v.trimmed();
  emit errorChanged();
}
void Backend::emitAllStateChanged() {
  emit projectChanged();
  emit sequenceChanged();
  emit mediaChanged();
  emit clipsChanged();
  emit tracksChanged();
  emit markersChanged();
  emit snappingChanged();
  emit timelineChanged();
  emit playheadChanged();
  emit historyChanged();
  emit captionStyleChanged();
  emit selectionChanged();
  m_selectionDetailNotify.schedule();
  m_colorSettingsNotify.schedule();
}

void Backend::ensureTrackExists(const QString &track) {
  static const QRegularExpression pattern("^([VA])(\\d+)$");
  const auto match = pattern.match(track.toUpper());
  if (!match.hasMatch())
    return;

  const int number = qBound(1, match.captured(2).toInt(), 64);
  int &count = match.captured(1) == "V" ? m_videoTrackCount : m_audioTrackCount;
  if (number <= count)
    return;
  count = number;
  emit tracksChanged();
}

void Backend::pruneEmptyTracks(bool releaseUserTracks) {
  if (releaseUserTracks) {
    // The edit that got here emptied a lane. Let the floors go: keeping them
    // would leave a stack of tracks standing with nothing on them, which is what
    // a user reads as "the track did not get removed".
    m_minVideoTracks = 1;
    m_minAudioTracks = 0;
  }
  // The floors are the tracks the user added by hand. Without them this
  // function treats "empty" as "delete", so a lane created with V+ to receive a
  // clip disappeared on the next unrelated edit and the drop that followed had
  // nowhere to go but the track below.
  int highestVideoTrack = qBound(1, m_minVideoTracks, 64);
  // Audio starts at zero: with no audio clips the sequence has no audio lane,
  // the way CapCut only shows one once something is on it. Video keeps V1.
  int highestAudioTrack = qBound(0, m_minAudioTracks, 64);
  // Cached list: the cues are not on a numbered lane, so a subtitle track used to
  // make this scan twenty thousand entries of normalizedTrack() long for nothing.
  // Effect bars are still in here, and the kind test below still drops them.
  ensureClipCaches();
  for (const auto &value : m_cachedMediaClips) {
    const QVariantMap clip = value.toMap();
    const QString kind = clip.value("kind").toString();
    // The overlay lanes are not part of the video/audio stack, so nothing on
    // them may add or hold open a numbered track.
    if (kind == QStringLiteral("subtitle") || kind == QStringLiteral("effect"))
      continue;
    const QString track =
        TimelinePlacement::normalizedTrack(clip.value("track").toString());
    const int number = TimelinePlacement::trackNumber(track);
    if (track.startsWith('V'))
      highestVideoTrack = qMax(highestVideoTrack, number);
    else if (track.startsWith('A'))
      highestAudioTrack = qMax(highestAudioTrack, number);
  }

  const bool countsChanged = highestVideoTrack != m_videoTrackCount ||
                             highestAudioTrack != m_audioTrackCount;
  m_videoTrackCount = highestVideoTrack;
  m_audioTrackCount = highestAudioTrack;
  if (releaseUserTracks) {
    // Occupancy is the new floor, so a later additive edit does not resurrect
    // the tracks this call just took away.
    m_minVideoTracks = m_videoTrackCount;
    m_minAudioTracks = m_audioTrackCount;
  }

  const auto trackStillExists = [this](const QString &track) {
    const QString normalized = track.trimmed().toUpper();
    const int number = TimelinePlacement::trackNumber(normalized);
    if (normalized.startsWith('V'))
      return number >= 1 && number <= m_videoTrackCount;
    if (normalized.startsWith('A'))
      return number >= 1 && number <= m_audioTrackCount;
    if (normalized.startsWith('S'))
      return number >= 1 && number <= subtitleTrackCount();
    return normalized == QStringLiteral("F1");
  };

  for (int i = m_mutedTracks.size() - 1; i >= 0; --i) {
    if (!trackStillExists(m_mutedTracks.at(i)))
      m_mutedTracks.removeAt(i);
  }
  for (const QString &track : m_trackStates.keys()) {
    if (!trackStillExists(track))
      m_trackStates.remove(track);
  }

  if (countsChanged)
    emit tracksChanged();
}

void Backend::configureAutoSave() {
  m_autoSaveTimer.stop();
  if (!m_appSettings.value("autoSaveEnabled", true).toBool())
    return;
  const int minutes =
      qBound(1, m_appSettings.value("autoSaveIntervalMinutes", 5).toInt(), 120);
  m_autoSaveTimer.setInterval(minutes * 60 * 1000);
  m_autoSaveTimer.start();
}

QString Backend::projectDatabasePath() const {
  if (!m_projectFile.isEmpty())
    return m_projectFile + QStringLiteral(".sqlite");
  if (!m_projectDatabaseFile.isEmpty())
    return m_projectDatabaseFile;
  if (!m_projectLocation.isEmpty())
    return QDir(m_projectLocation)
        .filePath(safeFileName(m_projectName) +
                  QStringLiteral(".cutpro.json.sqlite"));
  // An untitled project used to have no database at all: this returned nothing,
  // so persistProjectDatabase(), scheduleProjectDatabaseSave() and recordAction()
  // all turned into no-ops and a whole session of work - state snapshots and
  // action log alike - was never written anywhere. It gets the session database in
  // app data instead. One file, not one per project id, so the log and the
  // interface state outlive the launch; saveProject() moves the project on to
  // <project>.cutpro.json.sqlite from there.
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath(QStringLiteral("session-history.sqlite"));
}

void Backend::restoreUiStateFromDatabase() {
  if (!openProjectDatabase())
    return;
  const QVariantMap settings = m_projectDatabase.allSettings();
  bool restored = false;
  for (auto it = settings.cbegin(); it != settings.cend(); ++it) {
    if (!it.key().startsWith(QLatin1String("ui/")))
      continue;
    m_uiState.insert(it.key().mid(3), it.value());
    restored = true;
  }
  if (restored)
    emit uiStateChanged();
}

bool Backend::openProjectDatabase() {
  const QString path = projectDatabasePath();
  if (path.isEmpty())
    return false;
  if (m_projectDatabase.isOpen() && m_projectDatabaseFile == path)
    return true;
  m_projectDatabase.close();
  QDir().mkpath(QFileInfo(path).absolutePath());
  if (!m_projectDatabase.open(path))
    return false;
  m_projectDatabaseFile = path;
  // The shared session file is the one that grows without an owner: every launch
  // is a new project id, and every one of those leaves a whole-state snapshot
  // behind that nothing will ever load again. The action log - the part the user
  // asked for - is kept in full.
  if (QFileInfo(path).fileName() == QStringLiteral("session-history.sqlite"))
    m_projectDatabase.pruneStateSnapshots(m_projectId);
  return true;
}

bool Backend::setUiState(const QString &key, const QVariant &value) {
  const QString normalized = key.trimmed();
  if (normalized.isEmpty())
    return false;
  if (m_uiState.contains(normalized) && m_uiState.value(normalized) == value)
    return false;
  m_uiState[normalized] = value;
  // Not undoable on purpose: undo should put clips back, not reopen the panel the
  // user was on three edits ago.
  if (openProjectDatabase())
    m_projectDatabase.setSetting(QStringLiteral("ui/") + normalized, value);
  markDirty();
  emit uiStateChanged();
  return true;
}

QVariant Backend::uiStateValue(const QString &key,
                               const QVariant &fallback) const {
  const QVariant value = m_uiState.value(key.trimmed());
  return value.isValid() ? value : fallback;
}

void Backend::logUiAction(const QString &type, const QVariantMap &payload) {
  const QString normalized = type.trimmed();
  if (normalized.isEmpty())
    return;
  recordAction(normalized, payload);
}

QVariantList Backend::recentActions(int limit) {
  if (!m_projectDatabase.isOpen() && !openProjectDatabase())
    return {};
  return m_projectDatabase.actions(m_projectId, qBound(1, limit, 5000));
}

bool Backend::persistProjectDatabase() {
  const QString desiredPath = projectDatabasePath();
  if (desiredPath.isEmpty())
    return true;

  if (!m_projectDatabase.isOpen() || m_projectDatabaseFile != desiredPath) {
    m_projectDatabase.close();
    m_projectDatabaseFile = desiredPath;
    QDir().mkpath(QFileInfo(desiredPath).absolutePath());
    if (!m_projectDatabase.open(desiredPath)) {
      setError(QStringLiteral("Could not open project SQLite database: ") +
               m_projectDatabase.lastError());
      return false;
    }
  }

  // Straight from the values. This used to be
  // QJsonDocument::fromJson(serializeState()).toVariant().toMap(): write every
  // clip out as JSON text, parse the text back, then convert the result to
  // variants - three passes over the whole project, on the GUI thread, 250 ms
  // after every edit. Then it was stateObject().toVariantMap(), which still made
  // a JSON copy of every clip only to convert it straight back.
  const QVariantMap state = stateVariant();
  if (!m_projectDatabase.saveProject(state)) {
    setError(QStringLiteral("Could not update project SQLite database: ") +
             m_projectDatabase.lastError());
    return false;
  }
  return true;
}

void Backend::scheduleProjectDatabaseSave() {
  if (projectDatabasePath().isEmpty())
    return;
  m_projectDatabaseSaveTimer.start();
}

void Backend::dispatchAsyncProjectSave() {
  const QString path = projectDatabasePath();
  if (path.isEmpty())
    return;
  // Make sure the directory exists on the GUI thread (cheap); the worker only
  // opens the file. stateVariant() is a shallow, copy-on-write snapshot of the
  // lists already in memory - building it is cheap and the worker reads it
  // without touching Backend, so GUI mutations after this point detach their
  // own copies and cannot race the write.
  QDir().mkpath(QFileInfo(path).absolutePath());
  m_projectSaveService.requestSave(path, stateVariant());
}

void Backend::performAutoSave() {
  if (!m_dirty || m_sequenceId.isEmpty())
    return;

  QString target;
  if (!m_projectFile.isEmpty()) {
    const QFileInfo project(m_projectFile);
    target = project.dir().filePath(project.completeBaseName() +
                                    QStringLiteral(".autosave.cutpro.json"));
  } else if (!m_projectLocation.isEmpty()) {
    target = QDir(m_projectLocation)
                 .filePath(m_projectName +
                           QStringLiteral(".autosave.cutpro.json"));
  }
  if (target.isEmpty())
    return;

  QDir().mkpath(QFileInfo(target).absolutePath());
  QSaveFile file(target);
  if (!file.open(QIODevice::WriteOnly) || file.write(serializeState()) < 0 ||
      !file.commit()) {
    setError(QStringLiteral("Auto save failed: ") + file.errorString());
    return;
  }
  // The .autosave.cutpro.json above is the authoritative autosave artifact and
  // was just written synchronously. Mirror the state into the SQLite store off
  // the GUI thread so a large project does not stall here on every autosave.
  dispatchAsyncProjectSave();
  emit autoSaveCompleted(target);
  recordAction(QStringLiteral("autosave"), QVariantMap{{"path", target}});
}

