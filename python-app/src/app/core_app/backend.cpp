#include "app/core_app/backend.h"
#include "app/settings/app_settings.h"
#include "app/lumetri/color_settings.h"
#include "app/effects/effect_registry.h"
#include "app/effects/effect_stack.h"
#include "app/preview/media_preview_generator.h"
#include "app/preview/large_media_preview_job.h"
#include "app/export/sequence_export_builder.h"
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
#include "app/preview/gui_thread_watchdog.h"
#include "core/ids.h"
#include "core/version.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
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
    m_videoPreviewHelper.setCustomPreviewClipId(m_customBlurEditClipId);
  });
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
  m_audioTrackCount = m_appSettings.value("defaultAudioTracks").toInt();
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
        const QVariantMap sourceClip = m_clips.at(index).toMap();
        const QString linkGroupId = sourceClip.value("linkGroupId").toString();
        for (int i = 0; i < m_clips.size(); ++i) {
          QVariantMap clip = m_clips.at(i).toMap();
          const bool sameClip = i == index;
          const bool linked = !linkGroupId.isEmpty() &&
                              clip.value("linkGroupId").toString() == linkGroupId;
          if (!sameClip && !linked)
            continue;
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
          &Backend::rebuildSequenceTranscript);
  connect(this, &Backend::tracksChanged, this,
          &Backend::rebuildSequenceTranscript);
  connect(&m_effectPreviewGenerator, &EffectPreviewGenerator::previewReady,
          this, &Backend::effectPreviewReady);
  connect(&m_previewDecoder, &FfmpegPreviewDecoder::frameReady, this,
          [this](quint64) {
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
  m_textToSpeechEngine.beginTimelineImport(m_pendingTtsOutputs.size());
  m_ttsImportTimer.start();
}

void Backend::importNextTimedSpeechOutput() {
  if (!m_ttsImportActive)
    return;
  if (m_textToSpeechEngine.importCancellationRequested()) {
    m_pendingTtsOutputs.clear();
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

  constexpr int batchSize = 2;
  int processed = 0;
  while (m_pendingTtsIndex < m_pendingTtsOutputs.size() &&
         processed < batchSize) {
    const QVariantMap output =
        m_pendingTtsOutputs.at(m_pendingTtsIndex++).toMap();
    ++processed;
    const QString path = output.value("path").toString();
    const qint64 startMs =
        qMax<qint64>(0, output.value("startMs").toLongLong());
    const qint64 endMs = output.value("endMs").toLongLong();
    const qint64 durationMs = endMs - startMs;
    if (path.isEmpty() || durationMs <= 0 || !QFileInfo::exists(path))
      continue;

    const QFileInfo source(path);
    QVariantMap media{{"path", source.absoluteFilePath()},
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
    const QString mediaId = id("media");
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

    QString destination;
    for (int number = 1; number <= 64 && destination.isEmpty(); ++number) {
      const QString candidate = QStringLiteral("A%1").arg(number);
      if (trackLocked(candidate) && number <= m_audioTrackCount)
        continue;
      bool occupied = false;
      for (const QVariant &clipValue : m_clips) {
        const QVariantMap clip = clipValue.toMap();
        if (clip.value("track").toString() != candidate ||
            clip.value("enabled", true).toBool() == false)
          continue;
        const qint64 clipStart = clip.value("startMs").toLongLong();
        const qint64 clipEnd =
            clipStart + clip.value("durationMs").toLongLong();
        if (startMs < clipEnd && endMs > clipStart) {
          occupied = true;
          break;
        }
      }
      if (!occupied)
        destination = candidate;
    }
    if (destination.isEmpty())
      destination = QStringLiteral("A64");
    ensureTrackExists(destination);

    const qint64 sourceDurationMs =
        qMax<qint64>(1, media.value("durationMs").toLongLong());
    m_clips.append(QVariantMap{{"id", id("clip")},
                               {"mediaId", mediaId},
                               {"name", media.value("name")},
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
  }

  m_textToSpeechEngine.updateTimelineImport(m_pendingTtsIndex,
                                             m_pendingTtsOutputs.size());
  if (m_pendingTtsIndex < m_pendingTtsOutputs.size()) {
    // Publish only this small batch, then yield to Qt. Timeline delegates and
    // waveform/layout bindings never have to create the full batch in one
    // blocking GUI-thread pass.
    emit mediaChanged();
    emit clipsChanged();
    emit tracksChanged();
    emit timelineChanged();
    m_ttsImportTimer.start();
    return;
  }

  const int added = m_pendingTtsAdded;
  m_pendingTtsOutputs.clear();
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
  qint64 mediaEnd = 0;
  qint64 subtitleEnd = 0;
  for (const auto &v : m_clips) {
    auto c = v.toMap();
    const qint64 end =
        c.value("startMs").toLongLong() + c.value("durationMs").toLongLong();
    if (c.value("kind") == "subtitle")
      subtitleEnd = std::max(subtitleEnd, end);
    else
      mediaEnd = std::max(mediaEnd, end);
  }
  return mediaEnd > 0 ? mediaEnd : subtitleEnd;
}

bool Backend::hasSubtitleClips() const {
  return std::any_of(m_clips.cbegin(), m_clips.cend(), [](const auto &value) {
    return value.toMap().value("kind") == "subtitle";
  });
}

bool Backend::canExport() const {
  return hasSequence() &&
         std::any_of(m_clips.cbegin(), m_clips.cend(), [](const auto &value) {
           return value.toMap().value("kind") != "subtitle";
         });
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
  m_playheadMs = v;
  emit playheadChanged();
}

void Backend::setPlaying(bool playing) {
  if (m_playing == playing)
    return;
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
  m_audioTrackCount = m_appSettings.value("defaultAudioTracks").toInt();
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
    const QString linkGroupId = clip.value("linkGroupId").toString();
    for (int i = 0; i < m_clips.size(); ++i) {
      QVariantMap updated = m_clips.at(i).toMap();
      const bool sameClip = i == index;
      const bool linked = !linkGroupId.isEmpty() &&
                          updated.value("linkGroupId").toString() == linkGroupId;
      if (!sameClip && !linked)
        continue;
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
    const QString linkGroupId = clip.value("linkGroupId").toString();
    for (int candidate = 0; candidate < m_clips.size(); ++candidate) {
      const QVariantMap linked = m_clips.at(candidate).toMap();
      if (linked.value("linkGroupId").toString() == linkGroupId &&
          linked.value("linkedRole").toString() == QStringLiteral("audio")) {
        index = candidate;
        clip = linked;
        break;
      }
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
  return instanceId;
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
    // Large media deliberately imports without generating previews so a
    // multi-hour file never blocks the import; whatever is still missing - the
    // poster, the timeline filmstrip or the waveform - is produced in the
    // background now that the item is visible in the bin.
    if (item.value(QStringLiteral("largeMedia")).toBool() &&
        needsDeferredPreview(item))
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
  pruneEmptyTracks();
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
  ensureTrackExists(c.value("track").toString());
  m_clips.append(c);
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
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
  ensureTrackExists(primaryTrack);
  addedIds.append(clip.value("id").toString());
  m_clips.append(clip);

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
    const QString destination = track.isEmpty()
                                    ? TimelinePlacement::defaultTrackForKind(kind)
                                    : TimelinePlacement::normalizedTrack(track);
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
  pruneEmptyTracks();
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
  pruneEmptyTracks();
  markDirty();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  return true;
}
bool Backend::splitClip(const QString &clipId, qint64 pos) {
  int i = clipIndex(clipId);
  if (i < 0)
    return false;
  auto c = m_clips[i].toMap();
  if (trackLocked(c.value("track").toString()))
    return false;
  qint64 rel = pos - c.value("startMs").toLongLong(),
         dur = c.value("durationMs").toLongLong();
  if (rel <= 0 || rel >= dur)
    return false;
  rememberState();
  auto r = c;
  r["id"] = id("clip");
  r["startMs"] = pos;
  r["sourceInMs"] = c.value("sourceInMs").toLongLong() + rel;
  r["durationMs"] = dur - rel;
  c["durationMs"] = rel;
  m_clips[i] = c;
  m_clips.insert(i + 1, r);
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
  const qint64 minimumStart = qMax<qint64>(0, oldStart - sourceIn);
  const qint64 newStart = qBound(minimumStart, requestedStart, oldEnd - 1);
  if (newStart == oldStart)
    return false;
  rememberState();
  clip["startMs"] = newStart;
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
    if (!group.isEmpty())
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
  QVector<int> indexes;
  for (const auto &clipId : removalIds) {
    const int index = clipIndex(clipId);
    if (index >= 0 && !indexes.contains(index))
      indexes.append(index);
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
  pruneEmptyTracks();
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

QString Backend::addTrack(const QString &kind) {
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
  if (count <= 1)
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
  markDirty();
  emit tracksChanged();
  emit timelineChanged();
  return true;
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
  static const QRegularExpression pattern(QStringLiteral("^([VAS])(\\d+)$"));
  const auto match = pattern.match(track);
  if (!match.hasMatch() || (match.captured(1) == QStringLiteral("S") &&
                            track != QStringLiteral("S1")))
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
  pruneEmptyTracks();
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
  QSaveFile f(target);
  if (!f.open(QIODevice::WriteOnly) || f.write(serializeState()) < 0 ||
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
      QVariantMap{{"path", target}}, serializeState());
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
  const QStringList a = SequenceExportBuilder::build(
      m_media, m_clips, m_mutedTracks, trackStates(), durationMs(),
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
  m_redo.append(serializeState());
  restoreState(m_undo.takeLast(), true);
  recordAction(QStringLiteral("undo"));
  markDirty();
  emit historyChanged();
}
void Backend::redo() {
  if (m_redo.isEmpty())
    return;
  m_undo.append(serializeState());
  restoreState(m_redo.takeLast(), true);
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

void Backend::rebuildSequenceTranscript() {
  QVariantList rebuilt;
  struct TimedSegment {
    qint64 startMs = 0;
    QVariantMap segment;
  };
  QVector<TimedSegment> timed;
  bool hasSequenceSources = false;

  for (const QVariant &value : m_clips) {
    const QVariantMap clip = value.toMap();
    const QString kind = clip.value("kind").toString();
    const QString mediaId = clip.value("mediaId").toString();
    if ((kind != QStringLiteral("video") && kind != QStringLiteral("audio")) ||
        mediaId.isEmpty())
      continue;
    const QVariantMap media = mediaById(mediaId);
    const QString normalizedMediaPath =
        QDir::fromNativeSeparators(media.value("path").toString());
    const bool generatedSpeech =
        media.value("excludeFromTranscript").toBool() ||
        media.value("generatedBy").toString() ==
            QStringLiteral("text_to_speech") ||
        normalizedMediaPath.contains(QStringLiteral("/generated-speech/"),
                                     Qt::CaseInsensitive);
    if (generatedSpeech)
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

  if (rebuilt != m_transcript) {
    m_transcript = rebuilt;
    emit transcriptChanged();
  }
}

int Backend::mediaIndex(const QString &v) const {
  for (int i = 0; i < m_media.size(); ++i)
    if (m_media[i].toMap().value("id") == v)
      return i;
  return -1;
}
int Backend::clipIndex(const QString &v) const {
  for (int i = 0; i < m_clips.size(); ++i)
    if (m_clips[i].toMap().value("id") == v)
      return i;
  return -1;
}
QByteArray Backend::serializeState() const {
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
  QJsonObject o{{"schemaVersion", 4},
                {"projectId", m_projectId},
                {"projectName", m_projectName},
                {"projectLocation", m_projectLocation},
                {"sequenceId", m_sequenceId},
                {"sequenceName", m_sequenceName},
                {"videoTrackCount", m_videoTrackCount},
                {"audioTrackCount", m_audioTrackCount},
                {"mutedTracks", QJsonArray::fromStringList(m_mutedTracks)},
                {"trackStates", QJsonObject::fromVariantMap(m_trackStates)},
                {"markers", QJsonArray::fromVariantList(m_markers)},
                {"snappingEnabled", m_snappingEnabled},
                {"captionStyle", m_captionStyle.toJson()},
                {"colorSettings", QJsonObject::fromVariantMap(m_colorSettings)},
                {"media", QJsonArray::fromVariantList(m_media)},
                {"clips", QJsonArray::fromVariantList(m_clips)},
                {"transcript", QJsonArray::fromVariantList(m_transcript)},
                {"transcriptLanguage", m_transcriptLanguage},
                {"sourceTranscripts", QJsonObject::fromVariantMap(sourceTranscripts)},
                {"sourceTranscriptLanguages", QJsonObject::fromVariantMap(sourceLanguages)},
                {"transcriptCoverageMs", QJsonObject::fromVariantMap(transcriptCoverage)}};
  return QJsonDocument(o).toJson(QJsonDocument::Indented);
}
bool Backend::restoreState(const QByteArray &data, bool history) {
  QJsonParseError e;
  auto d = QJsonDocument::fromJson(data, &e);
  if (!d.isObject()) {
    setError(QStringLiteral("Invalid project file: ") + e.errorString());
    return false;
  }
  auto o = d.object();
  const int schemaVersion = o.value("schemaVersion").toInt();
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
  m_projectId = o.value("projectId").toString(id("project"));
  m_projectName = o.value("projectName").toString("Untitled");
  m_projectLocation = o.value("projectLocation").toString();
  m_sequenceId = o.value("sequenceId").toString();
  m_sequenceName = o.value("sequenceName").toString("Sequence 01");
  m_videoTrackCount = qBound(1, o.value("videoTrackCount").toInt(1), 64);
  m_audioTrackCount = qBound(1, o.value("audioTrackCount").toInt(1), 64);
  m_trackStates = o.value("trackStates").toObject().toVariantMap();
  m_markers = o.value("markers").toArray().toVariantList();
  m_snappingEnabled = o.value("snappingEnabled").toBool(true);
  m_mutedTracks.clear();
  for (const auto &value : o.value("mutedTracks").toArray()) {
    const QString track = value.toString().trimmed().toUpper();
    if (!m_mutedTracks.contains(track))
      m_mutedTracks.append(track);
  }
  m_captionStyle = CaptionStyle::fromJson(o.value("captionStyle").toObject());
  m_colorSettings = ColorSettings::defaults();
  m_selectedClipId.clear();
  const QVariantMap savedColorSettings =
      o.value("colorSettings").toObject().toVariantMap();
  for (auto it = savedColorSettings.cbegin(); it != savedColorSettings.cend();
       ++it)
    ColorSettings::setProjectValue(&m_colorSettings, it.key(), it.value());
  m_media = o.value("media").toArray().toVariantList();
  // Older projects predate the long-media presentation policy. Normalize the
  // media records during load so their existing clips do not recreate the
  // eager timeline/monitor path on the first repaint.
  for (auto &value : m_media) {
    auto media = value.toMap();
    LargeMediaPolicy::applyPresentationFlags(&media);
    value = media;
  }
  m_clips = o.value("clips").toArray().toVariantList();
  const bool hasSavedTranscript = o.contains("transcript");
  const QVariantList savedTranscript =
      o.value("transcript").toArray().toVariantList();
  const QString savedTranscriptLanguage =
      o.value("transcriptLanguage").toString();
  m_sourceTranscripts.clear();
  const QVariantMap savedSourceTranscripts =
      o.value("sourceTranscripts").toObject().toVariantMap();
  for (auto it = savedSourceTranscripts.cbegin();
       it != savedSourceTranscripts.cend(); ++it)
    m_sourceTranscripts.insert(it.key(), it.value().toList());
  m_sourceTranscriptLanguages.clear();
  const QVariantMap savedSourceLanguages =
      o.value("sourceTranscriptLanguages").toObject().toVariantMap();
  for (auto it = savedSourceLanguages.cbegin();
       it != savedSourceLanguages.cend(); ++it)
    m_sourceTranscriptLanguages.insert(it.key(), it.value().toString());
  m_transcriptCoverageMs.clear();
  const QVariantMap savedCoverage =
      o.value("transcriptCoverageMs").toObject().toVariantMap();
  for (auto it = savedCoverage.cbegin(); it != savedCoverage.cend(); ++it)
    m_transcriptCoverageMs.insert(it.key(), it.value().toLongLong());
  if (schemaVersion < 4)
    TimelineClipBinding::collapseLegacyEmbeddedAudio(&m_clips, m_media);
  for (auto &value : m_clips) {
    auto clip = value.toMap();
    const QString kind = clip.value("kind").toString();
    const QVariantMap media = mediaById(clip.value("mediaId").toString());
    if (LargeMediaPolicy::requiresLightweightHandling(media))
      clip["timelineRenderMode"] = QStringLiteral("lightweight");
    QString track =
        TimelinePlacement::normalizedTrack(clip.value("track").toString());
    if (!TimelinePlacement::trackAcceptsKind(track, kind))
      track = TimelinePlacement::defaultTrackForKind(kind);
    if (clip.value("sourceDurationMs").toLongLong() <= 0) {
      const qint64 currentExtent = clip.value("sourceInMs").toLongLong() +
                                   clip.value("durationMs").toLongLong();
      clip["sourceDurationMs"] =
          qMax(currentExtent, media.value("durationMs").toLongLong());
    }
    clip["track"] = track;
    QVariantList stack = EffectStack::normalized(
        clip.value("effectStack").toList());
    for (auto &effectValue : stack) {
      QVariantMap instance = effectValue.toMap();
      if (instance.value("id").toString().isEmpty())
        instance["id"] = id("effect");
      effectValue = instance;
    }
    if (stack.isEmpty())
      clip.remove("effectStack");
    else
      clip["effectStack"] = stack;
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
  recordAction(QStringLiteral("edit"));
  m_undo << serializeState();
  if (m_undo.size() > 100)
    m_undo.removeFirst();
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
void Backend::recordAction(const QString &type, const QVariantMap &payload) {
  if (!m_projectDatabase.isOpen() && !persistProjectDatabase())
    return;
  m_projectDatabase.appendAction(m_projectId, m_sequenceId, type, payload,
                                 serializeState());
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

void Backend::pruneEmptyTracks() {
  int highestVideoTrack = 1;
  int highestAudioTrack = 1;
  for (const auto &value : m_clips) {
    const QVariantMap clip = value.toMap();
    if (clip.value("kind") == QStringLiteral("subtitle"))
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

  const auto trackStillExists = [this](const QString &track) {
    const QString normalized = track.trimmed().toUpper();
    const int number = TimelinePlacement::trackNumber(normalized);
    if (normalized.startsWith('V'))
      return number >= 1 && number <= m_videoTrackCount;
    if (normalized.startsWith('A'))
      return number >= 1 && number <= m_audioTrackCount;
    return normalized == QStringLiteral("S1");
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

  const QVariantMap state =
      QJsonDocument::fromJson(serializeState()).toVariant().toMap();
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
