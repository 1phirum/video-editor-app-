#include "app/core_app/backend.h"
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
#include "app/timeline/timeline_clip_binding.h"
#include "app/timeline/timeline_placement.h"
#include "app/timeline/large_media_policy.h"
#include "app/timeline/long_media_timeline_handler.h"
#include "app/media/ffmpeg_runtime.h"
#include "app/media/media_metadata.h"
#include "app/media/media_path.h"
#include "app/media/media_scan.h"
#include "app/preview/audio_peak_builder.h"
#include "app/preview/filmstrip_builder.h"
#include "app/diagnostics/playback_trace.h"
#include "app/preview/gui_thread_watchdog.h"
#include "core/ids.h"
#include "core/version.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QFontDatabase>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <functional>

namespace {
QString id(const char *prefix) {
  return QString::fromStdString(core::new_id(prefix));
}
QString ffmpeg() {
  return FfmpegRuntime::executable();
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

QString exportFailureMessage(const QByteArray &data) {
  const QString text = QString::fromLocal8Bit(data);
  const QStringList lines = text.split(QRegularExpression("[\\r\\n]+"),
                                       Qt::SkipEmptyParts);
  static const QRegularExpression progressLine(
      QStringLiteral("^[a-z0-9_]+="), QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression errorLine(
      QStringLiteral("(error|failed|invalid|unable|not found|no such|"
                     "permission denied|could not|cannot)"),
      QRegularExpression::CaseInsensitiveOption);
  for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
    const QString line = it->trimmed();
    if (!progressLine.match(line).hasMatch() &&
        errorLine.match(line).hasMatch())
      return QStringLiteral("FFmpeg: %1").arg(line.left(700));
  }
  for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
    const QString line = it->trimmed();
    if (!line.isEmpty() && !progressLine.match(line).hasMatch())
      return QStringLiteral("FFmpeg: %1").arg(line.left(700));
  }
  return QStringLiteral("FFmpeg exited before completing the export.");
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
  m_timelineClipModel.setClips(m_clips);
  // Start with a bounded initial projection. TimelinePanel replaces this with
  // the exact viewport once its geometry is available.
  m_timelineClipModel.setViewport(0, 5 * 60 * 1000);
  const auto refreshPreviewTimeline = [this]() {
    m_videoPreviewHelper.setTimeline(m_clips, m_media);
  };
  connect(this, &Backend::clipsChanged, this, refreshPreviewTimeline);
  connect(this, &Backend::clipsChanged, this, [this]() {
    m_timelineClipModel.setClips(m_clips);
  });
  connect(this, &Backend::mediaChanged, this, refreshPreviewTimeline);
  connect(this, &Backend::tracksChanged, this, [this]() {
    m_videoPreviewHelper.setTrackStates(trackStates());
  });
  connect(this, &Backend::playheadChanged, this, [this]() {
    m_videoPreviewHelper.setPosition(m_playheadMs);
  });
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
  connect(&m_videoPreviewHelper, &VideoPreviewHelper::activeStateChanged, this,
          &Backend::colorSettingsChanged);
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
  connect(&m_largeMediaPreviewWatcher,
          &QFutureWatcher<QVariantMap>::finished, this,
          &Backend::finishDeferredMediaPreview);
  const QString fontRoot = QDir(QStandardPaths::writableLocation(
      QStandardPaths::AppDataLocation)).filePath(QStringLiteral("fonts"));
  QDir().mkpath(fontRoot);
  for (const QString &family : {QStringLiteral("Roboto"), QStringLiteral("Open Sans"),
                                QStringLiteral("Lato")}) {
    const QString file = QDir(fontRoot).filePath(family.toLower().replace(' ', '-') + ".ttf");
    if (QFileInfo::exists(file) && QFontDatabase::addApplicationFont(file) >= 0)
      m_downloadedCaptionFonts << family;
  }
  m_activeWorkspace = m_appSettings.value("startupWorkspace").toString();
  m_layoutPreset = m_appSettings.value("startupLayout").toString();
  m_videoTrackCount = m_appSettings.value("defaultVideoTracks").toInt();
  // Audio lanes are created by content, CapCut-style: a sequence opens with
  // none, and the first audio clip (or the A+ button) is what brings A1 into
  // existence. Video keeps its configured default so V1 is always there.
  m_audioTrackCount = 0;
  m_minVideoTracks = m_videoTrackCount;
  m_minAudioTracks = 0;
  m_snappingEnabled = m_appSettings.value("timelineSnapping").toBool();
  connect(&m_exportProcess, &QProcess::readyReadStandardError, this,
          &Backend::updateExportProgress);
  connect(&m_exportProcess, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError) {
            setError(m_exportProcess.errorString());
            m_exportStatus = QStringLiteral("Export failed");
            emit exportStateChanged();
          });
  connect(&m_exportProcess,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
            m_exportStandardError += m_exportProcess.readAllStandardError();
            const bool ok = status == QProcess::NormalExit && code == 0;
            m_exportStatus = ok ? QStringLiteral("Export complete")
                                : QStringLiteral("Export failed");
            if (!ok && m_lastError.isEmpty())
              setError(exportFailureMessage(m_exportStandardError));
            if (!m_exportConcatFile.isEmpty()) {
              QFile::remove(m_exportConcatFile);
              m_exportConcatFile.clear();
            }
            if (ok)
              m_exportProgress = 1.0;
            QFile::remove(m_exportOutputPath + QStringLiteral(".cutpro.srt"));
            emit exportStateChanged();
            emit exportFinished(ok, m_exportOutputPath);
          });
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
          m_transcriptionStatus = m_transcriptionCancelRequested
              ? QStringLiteral("Transcription cancelled")
              : (workerError.isEmpty()
                     ? QString::fromUtf8(m_transcriptionStderr).trimmed()
                     : workerError);
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
      m_demucsProgress = qBound(0.0, overall, 0.995);
    }
    m_demucsStatus = QStringLiteral("Separating vocals...");
    emit demucsChanged();
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
      if (!m_demucsOutput.trimmed().isEmpty())
        setError(QString::fromLocal8Bit(m_demucsOutput).trimmed().right(700));
    }
    m_demucsClipId.clear();
    m_demucsOutputDir.clear();
    m_demucsOutput.clear();
    m_demucsProgressPass = 0;
    m_demucsLastRawProgress = 0.0;
    m_demucsExpectedPasses = 1;
    emit demucsChanged();
    emit demucsFinished(success, clipId);
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
  connect(&m_textToSpeechEngine, &TextToSpeechEngine::finished, this,
          [this](bool success, const QVariantList &outputs,
                 const QString &error) {
            if (!success) {
              if (!error.contains(QStringLiteral("cancel"),
                                  Qt::CaseInsensitive))
                setError(error.isEmpty() ? m_textToSpeechEngine.status()
                                         : error);
              return;
            }

            rememberState();

            // Regeneration replaces the previous subtitle-voice batch. This
            // keeps A1 free of stale overlapping duplicates and also keeps the
            // Project panel from accumulating obsolete generated files.
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
              if (oldMediaIds.contains(
                      m_media[index].toMap().value("id").toString()))
                m_media.removeAt(index);
            }

            if (m_sequenceId.isEmpty()) {
              m_sequenceId = id("sequence");
              m_sequenceName = QStringLiteral("Sequence 01");
              emit sequenceChanged();
            }
            ensureTrackExists(QStringLiteral("A1"));

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
            const qint64 shown = m_previewDecoder.presentedSourceMs();
            if (shown != m_paintedSourceMs) {
              m_previousPaintedSourceMs = m_paintedSourceMs;
              m_paintedSourceMs = shown;
              m_paintedWallMs = QDateTime::currentMSecsSinceEpoch();
            }
            m_paintedFrameWidth = m_previewDecoder.presentedWidth();

            // Counted here rather than taken from the decoder: two producers
            // feed this URL now, and a revision that ever repeats would let QML
            // serve a cached image for a new frame.
            ++m_previewFrameRevision;
            m_previewFrameFromScrub = false;
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
          [this](quint64) {
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
            ++m_previewFrameRevision;
            m_previewFrameFromScrub = true;
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
          [this]() { persistProjectDatabase(); });
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
              markDirty();
            } else if (!error.isEmpty()) {
              setError(error);
            }
            emit transcriptChanged();
          });
  configureAutoSave();

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
  m_ttsImportTimer.stop();
  m_projectDatabaseSaveTimer.stop();
  persistProjectDatabase();
  // Before anything else: the import workers call back into probeMedia(), so
  // they must be stopped while this object is still whole.
  m_mediaImportQueue.shutdown();
  if (m_largeMediaPreviewWatcher.isRunning())
    m_largeMediaPreviewWatcher.waitForFinished();
  m_textToSpeechEngine.cancel();
  if (m_demucsProcess.state() != QProcess::NotRunning) {
    m_demucsProcess.kill();
    m_demucsProcess.waitForFinished(1000);
  }
}

bool Backend::generateTimedTextToSpeech(const QVariantList &segments,
                                        const QString &language,
                                        const QString &gender) {
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
      worker, outputDir, ffmpeg());
}

void Backend::beginTimedSpeechImport(const QVariantList &outputs) {
  if (outputs.isEmpty()) {
    if (!m_undo.isEmpty()) {
      m_undo.removeLast();
      emit historyChanged();
    }
    setError(QStringLiteral("No generated subtitle voices could be imported."));
    return;
  }

  m_pendingTtsOutputs = outputs;
  std::sort(m_pendingTtsOutputs.begin(), m_pendingTtsOutputs.end(),
            [](const QVariant &left, const QVariant &right) {
              return left.toMap().value("startMs").toLongLong() <
                     right.toMap().value("startMs").toLongLong();
            });
  m_pendingTtsIndex = 0;
  m_pendingTtsAdded = 0;
  m_ttsImportActive = true;
  buildSpeechLaneIndex();
  m_ttsMediaByPath.clear();
  // The whole import is known up front, so the two lists are grown once instead of
  // reallocating and copying a 19831-entry spine on the way through.
  m_media.reserve(m_media.size() + m_pendingTtsOutputs.size());
  m_clips.reserve(m_clips.size() + m_pendingTtsOutputs.size());
  m_textToSpeechEngine.beginTimelineImport(m_pendingTtsOutputs.size());
  m_ttsPublishedMediaCount = m_media.size();
  m_ttsPublishedVideoTracks = m_videoTrackCount;
  m_ttsPublishedAudioTracks = m_audioTrackCount;
  m_ttsPublishClock.start();
  m_ttsImportTimer.start();
}

void Backend::buildSpeechLaneIndex() {
  constexpr int kLaneCount = 64;
  m_ttsLanes.assign(kLaneCount, SpeechLane{});
  for (int number = 1; number <= kLaneCount; ++number) {
    // Same rule the per-cue search used: a locked lane is off limits only while it
    // is a lane the sequence actually has.
    if (number <= m_audioTrackCount &&
        trackLocked(QStringLiteral("A%1").arg(number)))
      m_ttsLanes[number - 1].blocked = true;
  }
  // One pass over the timeline for all 64 lanes, and one QVariantMap conversion
  // per clip rather than one per clip per cue per lane.
  for (const QVariant &clipValue : m_clips) {
    const QVariantMap clip = clipValue.toMap();
    if (clip.value(QStringLiteral("enabled"), true).toBool() == false)
      continue;
    const QString track = clip.value(QStringLiteral("track")).toString();
    if (track.size() < 2 || !track.startsWith(QLatin1Char('A')))
      continue;
    bool ok = false;
    const int number = QStringView(track).mid(1).toInt(&ok);
    if (!ok || number < 1 || number > kLaneCount)
      continue;
    const qint64 start = clip.value(QStringLiteral("startMs")).toLongLong();
    const qint64 end =
        start + clip.value(QStringLiteral("durationMs")).toLongLong();
    if (end > start)
      m_ttsLanes[number - 1].intervals.append({start, end});
  }
  for (SpeechLane &lane : m_ttsLanes)
    std::sort(lane.intervals.begin(), lane.intervals.end());
}

QString Backend::reserveSpeechLane(qint64 startMs, qint64 endMs) {
  for (int number = 1; number <= m_ttsLanes.size(); ++number) {
    SpeechLane &lane = m_ttsLanes[number - 1];
    if (lane.blocked)
      continue;
    // Cues arrive in ascending start order, so everything that ends at or before
    // this cue begins is behind us for good.
    while (lane.cursor < lane.intervals.size() &&
           lane.intervals.at(lane.cursor).second <= startMs)
      ++lane.cursor;
    if (lane.appendedEndMs > startMs)
      continue;
    if (lane.cursor < lane.intervals.size() &&
        lane.intervals.at(lane.cursor).first < endMs)
      continue;
    lane.appendedEndMs = endMs;
    return QStringLiteral("A%1").arg(number);
  }
  return QStringLiteral("A64");
}

void Backend::importNextTimedSpeechOutput() {
  if (!m_ttsImportActive)
    return;
  if (m_textToSpeechEngine.importCancellationRequested()) {
    m_pendingTtsOutputs.clear();
    m_ttsLanes.clear();
    m_ttsMediaByPath.clear();
    m_ttsImportActive = false;
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
  while (m_pendingTtsIndex < m_pendingTtsOutputs.size()) {
    const QVariantMap output =
        m_pendingTtsOutputs.at(m_pendingTtsIndex++).toMap();
    const QString path = output.value("path").toString();
    const qint64 startMs =
        qMax<qint64>(0, output.value("startMs").toLongLong());
    const qint64 endMs = output.value("endMs").toLongLong();
    const qint64 durationMs = endMs - startMs;
    if (path.isEmpty() || durationMs <= 0)
      continue;

    const QFileInfo source(path);
    const QString absolute = source.absoluteFilePath();
    QString mediaId = m_ttsMediaByPath.value(absolute);
    if (mediaId.isEmpty()) {
      // Statted once per distinct file rather than once per cue. Cue tracks
      // repeat themselves, and a stat per cue is twenty thousand trips to the
      // filesystem on the GUI thread to re-answer a question the worker already
      // audited before it wrote the manifest.
      if (!source.isFile())
        continue;
      QVariantMap media{{"path", absolute},
                        {"name", source.fileName()},
                        {"kind", QStringLiteral("audio")},
                        {"sizeBytes", source.size()},
                        {"durationMs", durationMs},
                        {"width", 0},
                        {"height", 0},
                        {"frameRate", 0.0},
                        {"sampleRate", 24000},
                        {"channels", 1},
                        {"thumbnailUrl", QString()},
                        {"timelineThumbnailUrl", QString()},
                        {"filmstripFrames", 0},
                        {"filmstripFrameWidth", 0},
                        {"filmstripFrameHeight", 0},
                        {"waveformUrl", QString()}};
      mediaId = id("media");
      media["id"] = mediaId;
      media["importedAt"] =
          QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
      media["generatedBy"] = QStringLiteral("text_to_speech");
      media["excludeFromTranscript"] = true;
      media["hiddenInProjectPanel"] = true;
      media["ttsSubtitleIndex"] = output.value("index");
      media["ttsStartMs"] = startMs;
      media["ttsEndMs"] = endMs;
      m_media.append(media);
      m_ttsMediaByPath.insert(absolute, mediaId);
    }

    const QString destination = reserveSpeechLane(startMs, endMs);
    ensureTrackExists(destination);

    // From the cue, not from the file: this is the span the clip occupies, and it
    // is what the old code put here too.
    const qint64 sourceDurationMs = qMax<qint64>(1, durationMs);
    m_clips.append(QVariantMap{{"id", id("clip")},
                               {"mediaId", mediaId},
                               {"name", source.fileName()},
                               {"kind", QStringLiteral("audio")},
                               {"track", destination},
                               {"startMs", startMs},
                               {"sourceInMs", 0},
                               {"sourceDurationMs", sourceDurationMs},
                               {"durationMs", durationMs},
                               {"enabled", true},
                               {"generatedBy",
                                QStringLiteral("text_to_speech")},
                               {"ttsSubtitleIndex", output.value("index")},
                               {"subtitleText", output.value("text")}});
    ++m_pendingTtsAdded;
    if (budget.elapsed() >= kBudgetMs)
      break;
  }

  m_textToSpeechEngine.updateTimelineImport(m_pendingTtsIndex,
                                             m_pendingTtsOutputs.size());
  if (m_pendingTtsIndex < m_pendingTtsOutputs.size()) {
    // Publish what has accumulated, at most a few times a second, then yield to
    // Qt. What is published is only what actually changed: with identical cue text
    // sharing one bin entry, most stretches of the import add no media entry and
    // no track, and a notification nothing follows is pure cost.
    constexpr qint64 kPublishIntervalMs = 150;
    if (m_ttsPublishClock.elapsed() >= kPublishIntervalMs) {
      m_ttsPublishClock.restart();
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
    m_ttsImportTimer.start();
    return;
  }
  // The final turn falls through to the unconditional emits below, which publish
  // the finished timeline whether or not this last turn added anything.

  const int added = m_pendingTtsAdded;
  m_pendingTtsOutputs.clear();
  m_ttsLanes.clear();
  m_ttsMediaByPath.clear();
  m_ttsImportActive = false;
  if (added == 0) {
    if (!m_undo.isEmpty()) {
      m_undo.removeLast();
      emit historyChanged();
    }
    m_textToSpeechEngine.finishTimelineImport(
        false, 0, QStringLiteral("No generated subtitle voices could be imported."));
    setError(QStringLiteral("No generated subtitle voices could be imported."));
    return;
  }

  pruneEmptyTracks();
  markDirty();
  emit mediaChanged();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  recordAction(QStringLiteral("text_to_speech"),
               QVariantMap{{"segmentCount", added},
                           {"placement", QStringLiteral("first-free-audio-track")}});
  m_textToSpeechEngine.finishTimelineImport(true, added);
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

    if (isSubtitle)
      m_cachedHasSubtitleClips = true;
    else
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
  emit playheadChanged();
}

void Backend::setPlaying(bool playing) {
  if (m_playing == playing)
    return;
  CUTPRO_PLAYBACK_TRACE("playing",
                        QStringLiteral("%1 -> %2 at playhead %3 ms")
                            .arg(m_playing ? QStringLiteral("true")
                                           : QStringLiteral("false"),
                                 playing ? QStringLiteral("true")
                                         : QStringLiteral("false"))
                            .arg(m_playheadMs));
  m_playing = playing;
  emit playingChanged();
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
  if (!m_projectLocation.isEmpty()) {
    m_projectDatabaseFile = projectDatabasePath();
    if (m_projectDatabase.open(m_projectDatabaseFile))
      persistProjectDatabase();
  }
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
  rememberState();
  clip["effects"] = effects;
  m_clips[index] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  if (key == QStringLiteral("vocalRemoval") && value.toBool())
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
  const QString configuredPython =
      m_appSettings.value("pythonExecutable", "python").toString();
  QString python = QStandardPaths::findExecutable(configuredPython);
  if (python.isEmpty() && QFileInfo::exists(configuredPython))
    python = QFileInfo(configuredPython).absoluteFilePath();
  const QString worker = QDir(QCoreApplication::applicationDirPath())
                             .filePath("tools/demucs_separate.py");
  if (python.isEmpty() || !QFileInfo::exists(worker)) {
    setError(QStringLiteral("Python Demucs worker is not available."));
    return false;
  }
  m_demucsClipId = clipId;
  m_demucsOutputDir = QDir(QStandardPaths::writableLocation(
      QStandardPaths::AppDataLocation)).filePath(
      QStringLiteral("demucs-v4-balanced/%1").arg(clip.value("mediaId").toString()));
  const QString configuredModel =
      qEnvironmentVariable("CUTPRO_DEMUCS_MODEL", QStringLiteral("htdemucs"));
  const int configuredShifts =
      qMax(1, qEnvironmentVariableIntValue("CUTPRO_DEMUCS_SHIFTS"));
  m_demucsExpectedPasses =
      (configuredModel.contains(QStringLiteral("ft"), Qt::CaseInsensitive) ? 4 : 1)
      * configuredShifts;
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
  return true;
}

void Backend::cancelDemucs() {
  if (demucsInProgress()) {
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
  emit clipsChanged();
  emit timelineChanged();
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
  emit clipsChanged();
  emit timelineChanged();
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
  emit clipsChanged();
  emit timelineChanged();
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
  emit clipsChanged();
  emit timelineChanged();
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
  rememberState();
  clip["effectStack"] = stack;
  m_clips[index] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
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
  emit clipsChanged();
  emit timelineChanged();
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
  emit clipsChanged();
  emit timelineChanged();
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
  emit mediaChanged();
  emit timelineChanged();
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
    m_undo.removeLast();
    emit historyChanged();
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
  emit clipsChanged();
  emit timelineChanged();
  return c.value("id").toString();
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
    m_clips[i] = head;
    m_clips.insert(i + 1, tail);
  }
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
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

QVariantList Backend::trackStates() const {
  QVariantList result;
  result.append(trackState(QStringLiteral("S1")));
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
  emit markersChanged();
  emit timelineChanged();
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
  if (!restoreState(f.readAll()))
    return false;
  pruneEmptyTracks();
  m_projectFile = QFileInfo(f.fileName()).absoluteFilePath();
  m_projectLocation = QFileInfo(f.fileName()).absolutePath();
  m_projectDatabaseFile = m_projectFile + QStringLiteral(".sqlite");
  if (QFileInfo::exists(m_projectDatabaseFile)) {
    m_projectDatabase.close();
    if (m_projectDatabase.open(m_projectDatabaseFile)) {
      const QVariantMap sqliteState =
          m_projectDatabase.loadProject(m_projectId);
      if (!sqliteState.isEmpty()) {
        const QByteArray sqliteJson =
            QJsonDocument::fromVariant(sqliteState).toJson();
        if (!restoreState(sqliteJson))
          return false;
      }
    }
  }
  m_undo.clear();
  m_redo.clear();
  markDirty(false);
  emitAllStateChanged();
  return true;
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

void Backend::startDeferredMediaPreview(const QVariantMap &media) {
  if (media.isEmpty())
    return;
  const QString mediaId = media.value(QStringLiteral("id")).toString();
  // One job per source. The same file can be dropped on the timeline several
  // times, and a second job would only rebuild what the first is already
  // producing into the same cache entry.
  if (!mediaId.isEmpty()) {
    if (m_largeMediaPreviewMediaId == mediaId)
      return;
    for (const QVariant &pending : m_pendingLargeMediaPreviews) {
      if (pending.toMap().value(QStringLiteral("id")).toString() == mediaId)
        return;
    }
  }
  if (m_largeMediaPreviewWatcher.isRunning()) {
    // Bounded: importing a folder of a hundred long videos would otherwise
    // queue a hundred full media maps, and the newest entries are the ones the
    // user is most likely looking at.
    if (m_pendingLargeMediaPreviews.size() >= kMaxPendingLargePreviews)
      m_pendingLargeMediaPreviews.removeFirst();
    m_pendingLargeMediaPreviews.append(media);
    return;
  }
  m_largeMediaPreviewMediaId = media.value(QStringLiteral("id")).toString();
  m_largeMediaPreviewWatcher.setFuture(QtConcurrent::run(
      [] (const QVariantMap &value) { return LargeMediaPreviewJob::generate(value); },
      media));
}

void Backend::finishDeferredMediaPreview() {
  const QVariantMap result = m_largeMediaPreviewWatcher.result();
  const QString id = result.value(QStringLiteral("id")).toString();
  m_largeMediaPreviewMediaId.clear();
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
        emit mediaChanged();
        // The timeline draws clip thumbnails from the media entry, so the clips
        // already placed for this source need to repaint too.
        emit timelineChanged();
      }
    }
  }
  if (!m_pendingLargeMediaPreviews.isEmpty())
    startDeferredMediaPreview(m_pendingLargeMediaPreviews.takeFirst().toMap());
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

bool Backend::startExportWithSettings(const QString &output,
                                      const QVariantMap &settings) {
  QVariantMap exportSettings = settings;
  exportSettings.insert(QStringLiteral("captionFontFamily"),
                        m_captionStyle.fontFamily);
  exportSettings.insert(QStringLiteral("captionFontSize"), m_captionStyle.fontSize);
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
  if (exportInProgress() || !canExport()) {
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
  clearError();
  QString buildError;
  // Hand each clip its animation channels so the rendered file follows the same
  // curves the monitor shows. Attached to the clip rather than passed as a
  // parallel argument because everything downstream already works clip by clip.
  QVariantList exportClips = m_clips;
  for (QVariant &value : exportClips) {
    QVariantMap clip = value.toMap();
    const QVariantMap channels =
        m_keyframeEngine.channelsForClip(clip.value("id").toString());
    if (!channels.isEmpty()) {
      clip.insert(QStringLiteral("keyframes"), channels);
      value = clip;
    }
  }
  const QStringList a = SequenceExportBuilder::build(
      m_media, exportClips, m_mutedTracks, trackStates(), durationMs(),
      m_colorSettings, exportSettings, target, &buildError);
  if (a.isEmpty()) {
    m_exportStatus = QStringLiteral("Export failed");
    setError(buildError);
    emit exportStateChanged();
    return false;
  }
  m_exportProcess.setWorkingDirectory(outputFolder);
  m_exportProcess.start(ffmpeg(), a);
  if (!m_exportProcess.waitForStarted(1500)) {
    m_exportStatus = QStringLiteral("Export failed");
    setError(QStringLiteral("FFmpeg could not be started."));
    QFile::remove(m_exportConcatFile);
    m_exportConcatFile.clear();
    emit exportStateChanged();
    return false;
  }
  emit exportStateChanged();
  return true;
}
void Backend::cancelExport() {
  if (exportInProgress())
    m_exportProcess.kill();
}
bool Backend::transcribeMedia(const QString &mediaId, const QString &model,
                              const QString &language) {
  if (transcriptionInProgress())
    return false;
  const auto item = mediaById(mediaId);
  if (item.isEmpty()) {
    setError(QStringLiteral("Select media before transcribing."));
    return false;
  }
  const QString configuredPython =
      m_appSettings.value("pythonExecutable", "python").toString();
  QString python = QStandardPaths::findExecutable(configuredPython);
  if (python.isEmpty() && QFileInfo::exists(configuredPython))
    python = QFileInfo(configuredPython).absoluteFilePath();
  const QString worker = QDir(QCoreApplication::applicationDirPath())
                             .filePath("tools/whisper_transcribe.py");
  if (python.isEmpty() || !QFileInfo::exists(worker)) {
    setError(QStringLiteral("Python Whisper worker is not available."));
    return false;
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
  clearTranscriptionJobDir();
  const QString selectedModel =
      model.trimmed().isEmpty()
          ? m_appSettings.value("transcriptionModel", "small").toString()
          : model;
  const QString selectedLanguage =
      language.trimmed().isEmpty()
          ? m_appSettings.value("transcriptionLanguage", "auto").toString()
          : language;

  // Whisper loads the whole audio track into memory before it decodes anything,
  // so the length of the source - not the model - is what decides whether this
  // machine survives the run. Long sources are cut into windows and transcribed
  // one window at a time.
  const QString sourcePath = item.value("path").toString();
  const qint64 sourceDurationMs = item.value("durationMs").toLongLong();
  // Anything already transcribed by a cancelled run is kept and continued.
  const qint64 resumeFromMs = m_transcriptCoverageMs.value(mediaId, 0);
  m_transcriptionPlan =
      TranscriptionPlanner::forSource(sourceDurationMs, selectedModel,
                                      resumeFromMs);
  if (resumeFromMs > 0 && m_transcriptionPlan.chunked)
    m_transcriptionSegments = m_sourceTranscripts.value(mediaId);

  if (!m_transcriptionPlan.chunked) {
    m_transcriptionStatus = QStringLiteral("Transcribing - %1")
                                .arg(m_transcriptionPlan.summary);
    emit transcriptChanged();
    m_transcriptionProcess.start(
        python, {worker, sourcePath, selectedModel, selectedLanguage});
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
  m_transcriptionProcess.start(python,
                               {worker, QStringLiteral("--job"), jobPath});
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
  if (type.isEmpty()) {
    // No type field: either the single-pass reply, which the exit handler reads
    // from m_transcriptionLastLine, or a worker error. An error that arrives
    // mid-run is kept so the status line can name what actually went wrong.
    const QString error = event.value(QStringLiteral("error")).toString();
    if (!error.isEmpty())
      m_transcriptionStderr = error.toUtf8();
    return;
  }
  m_transcriptionStreamed = true;
  if (type == QStringLiteral("window")) {
    m_transcriptionWindowIndex = event.value(QStringLiteral("index")).toInt();
    m_transcriptionWindowCount =
        qMax(m_transcriptionWindowCount,
             event.value(QStringLiteral("count")).toInt());
    m_transcriptionWindowFraction = 0.0;
    const qint64 startMs = event.value(QStringLiteral("startMs")).toLongLong();
    m_transcriptionStatus =
        QStringLiteral("Transcribing %1/%2 from %3")
            .arg(m_transcriptionWindowIndex + 1)
            .arg(m_transcriptionWindowCount)
            .arg(TranscriptionPlanner::formatDuration(startMs));
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
  if (transcriptionInProgress()) {
    m_transcriptionCancelRequested = true;
    m_transcriptionProcess.kill();
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
  static const QStringList supported{
      QStringLiteral("en"), QStringLiteral("zh-CN"), QStringLiteral("km"),
      QStringLiteral("es")};
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
    for (const QVariant &sourceValue : sourceSegments) {
      const QVariantMap source = sourceValue.toMap();
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
  if (!m_projectDatabase.isOpen() && !persistProjectDatabase())
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
    return normalized == QStringLiteral("S1") ||
           normalized == QStringLiteral("F1");
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
  if (m_projectLocation.isEmpty())
    return {};
  return QDir(m_projectLocation)
      .filePath(safeFileName(m_projectName) +
                QStringLiteral(".cutpro.json.sqlite"));
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
  if (!persistProjectDatabase())
    return;
  emit autoSaveCompleted(target);
  recordAction(QStringLiteral("autosave"), QVariantMap{{"path", target}});
}

void Backend::updateExportProgress() {
  const QByteArray chunk = m_exportProcess.readAllStandardError();
  m_exportStandardError += chunk;
  constexpr qsizetype maximumLogSize = 512 * 1024;
  if (m_exportStandardError.size() > maximumLogSize)
    m_exportStandardError = m_exportStandardError.right(maximumLogSize);
  const QString s = QString::fromLocal8Bit(chunk);
  QRegularExpression r("out_time_ms=(\\d+)");
  auto m = r.match(s);
  if (m.hasMatch() && durationMs() > 0)
    m_exportProgress =
        qBound(0.0, m.captured(1).toDouble() / 1000.0 / durationMs(), 0.99);
  emit exportStateChanged();
}
