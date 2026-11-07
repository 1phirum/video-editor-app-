// Voice-Over Record: capture the microphone onto an audio track.
//
// Two modes:
//
//  * ONE TAKE - everything between Record and Stop becomes a single clip.
//
//  * HANDS-FREE - the microphone stays open. Each burst of speech becomes its
//    own clip, committed to the timeline the moment you stop talking, and then
//    it listens again. Recording a dozen lines takes one button press, not
//    twelve, which is the whole point: a recap voice-over is read in short
//    pieces and clicking Record between each one is most of the work.
//
// Three places where this deliberately departs from Premiere, in each case
// because the reason behind Premiere's behaviour does not hold here:
//
//  * Premiere offers "Mute input during timeline recording", which silences
//    *input monitoring* so you do not hear yourself echoed. This app never
//    routes the microphone to the speakers, so there is no echo to mute and the
//    option would do nothing. What can actually spoil a take here is programme
//    audio from the speakers bleeding into the microphone, so the option
//    offered instead silences the *programme* - off by default, because on
//    headphones you want to hear what you are narrating to.
//
//  * Premiere's voice-over overwrites whatever is on the target track.
//    Overwriting here would have to be composed out of splitClip + removeClips,
//    and removeClips expands through A/V link groups (so it can take a clip on
//    another track) and then prunes empty tracks (so it can delete the very
//    lane being recorded onto). A take is unrepeatable; silently destroying
//    existing audio to place it is the wrong trade. If the chosen lane is
//    occupied over the take's span, the recording goes to the next free lane
//    and says so.
//
//  * Premiere has no hands-free mode at all.

#include "app/core_app/backend.h"

#include "app/media/media_path.h"
#include "app/preview/voice_over_recorder.h"
#include "app/preview/webcam_recorder.h"
#include "app/settings/app_settings.h"
#include "core/ids.h"

#include <QAudioDevice>
#include <QDateTime>
#include <QDir>
#include <QMediaDevices>
#include <QStandardPaths>

#include <cmath>

namespace {

// The microphone the user chose, or the system default when that device is gone
// (unplugged between sessions, which is the normal case for a USB mic).
QAudioDevice resolveInputDevice(const QString &preferredId) {
  const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
  if (!preferredId.isEmpty()) {
    for (const QAudioDevice &device : inputs) {
      if (QString::fromUtf8(device.id()) == preferredId)
        return device;
    }
  }
  const QAudioDevice fallback = QMediaDevices::defaultAudioInput();
  if (!fallback.isNull())
    return fallback;
  return inputs.isEmpty() ? QAudioDevice() : inputs.first();
}

// dBFS -> Int16 amplitude, the unit the capture buffers are measured in.
int amplitudeForDb(double db) {
  return int(std::lround(std::pow(10.0, qBound(-90.0, db, -1.0) / 20.0) *
                         32768.0));
}

} // namespace

QVariantList Backend::voiceOverInputDevices() {
  QVariantList list;
  const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
  for (const QAudioDevice &device : inputs) {
    list.append(QVariantMap{
        {QStringLiteral("id"), QString::fromUtf8(device.id())},
        {QStringLiteral("description"), device.description()},
        {QStringLiteral("isDefault"), device.isDefault()}});
  }
  return list;
}

bool Backend::voiceOverCapturing() const {
  return m_voiceOver && m_voiceOver->capturing();
}

qint64 Backend::voiceOverSegmentMs() const {
  return m_voiceOver ? m_voiceOver->segmentMs() : 0;
}

qint64 Backend::voiceOverSegmentStartMs() const {
  if (!m_voiceOver || !m_voiceOver->capturing())
    return m_voiceOverStartMs;
  // The segment began `segmentMs` ago in session time, and the session began at
  // m_voiceOverStartMs on the timeline.
  const qint64 sessionNow = m_voiceOver->sessionMs();
  const qint64 segment = m_voiceOver->segmentMs();
  return m_voiceOverStartMs + qMax<qint64>(0, sessionNow - segment);
}

void Backend::ensureVoiceOverRecorder() {
  if (m_voiceOver)
    return;
  m_voiceOver = new VoiceOverRecorder(this);
  connect(m_voiceOver, &VoiceOverRecorder::captureFailed, this,
          [this](const QString &message) {
    setError(message);
    if (voiceOverBusy()) stopVoiceOver();
    else stopVoiceOverMonitor();
  });
  connect(m_voiceOver, &VoiceOverRecorder::started, this, [this]() {
    m_voiceOverPending = false;
    m_voiceOverRecording = true;
    // Where the playhead actually is now that the microphone is open, not where
    // it was when Record was pressed. Opening an input client is a call into the
    // audio service, and the sequence has been rolling meanwhile: placing takes
    // from the requested start would put every word a device-open earlier than
    // it was spoken. With playback off the two are the same value.
    m_voiceOverStartMs = m_playheadMs;
    m_voiceOverStatus = m_voiceOverHandsFree
                            ? tr("Listening - start speaking")
                            : tr("Recording...");
    if (!m_voiceOverTickTimer) {
      m_voiceOverTickTimer = new QTimer(this);
      // Four times a second: fast enough that the growing bar tracks the voice,
      // slow enough that it costs nothing.
      m_voiceOverTickTimer->setInterval(250);
      connect(m_voiceOverTickTimer, &QTimer::timeout, this,
              [this]() { emit voiceOverTickChanged(); });
    }
    m_voiceOverTickTimer->start();
    emit voiceOverChanged();
  });
  connect(m_voiceOver, &VoiceOverRecorder::failed, this,
          [this](const QString &message) {
            m_voiceOverStatus = message;
            setError(message);
            m_voiceOverAudioEnded = true;
            if (m_voiceOverWebcam && m_voiceOverWebcam->busy()) {
              m_voiceOverStopping = true;
              m_voiceOverWebcam->finish(true);
              return;
            }
            resetVoiceOverState();
            emit voiceOverFinished(false, QString(), message);
          });
  // Each finished line, committed as it arrives. In hands-free mode this fires
  // repeatedly while the session stays open.
  connect(m_voiceOver, &VoiceOverRecorder::segmentReady, this,
          [this](const QString &path, qint64 durationMs, qint64 offsetMs) {
            const QString clipId =
                commitVoiceOverTake(path, durationMs, offsetMs);
            if (clipId.isEmpty()) {
              m_voiceOverStatus = tr("A take could not be added to the "
                                     "timeline.");
            } else {
              if (m_voiceOverWithWebcam)
                m_voiceOverWebcamTakes.append({clipId, offsetMs, durationMs});
              ++m_voiceOverTakeCount;
              m_voiceOverLastClipId = clipId;
              // Hand the take to the voice changer if a voice was chosen in the
              // recorder. Queued rather than applied inline: hands-free can
              // place several lines faster than one render finishes.
              const QString voiceId =
                  appSettings().value("voiceOverVoiceId").toString();
              if (!voiceId.isEmpty() && voiceId != QStringLiteral("none"))
                enqueueVoiceRender(clipId, voiceId);
              m_voiceOverStatus =
                  m_voiceOverHandsFree
                      ? tr("Placed take %1 (%2.%3 s) - listening again")
                            .arg(m_voiceOverTakeCount)
                            .arg(durationMs / 1000)
                            .arg((durationMs % 1000) / 100)
                      : tr("Recorded %1.%2 s")
                            .arg(durationMs / 1000)
                            .arg((durationMs % 1000) / 100);
            }
            emit voiceOverChanged();
            emit voiceOverTakePlaced(clipId, durationMs);
          });
  connect(m_voiceOver, &VoiceOverRecorder::sessionEnded, this,
          [this](bool keptAnything) {
            m_voiceOverAudioEnded = true;
            if (m_voiceOverWithWebcam && m_voiceOverWebcam->busy()) {
              m_voiceOverKeptAudio = keptAnything || m_voiceOverTakeCount > 0;
              m_voiceOverStatus = tr("Saving webcam recording...");
              emit voiceOverChanged();
              m_voiceOverWebcam->finish(m_voiceOverWebcamTakes.isEmpty());
              return;
            }
            finishVoiceOverSession(keptAnything);
          });
  connect(m_voiceOver, &VoiceOverRecorder::cancelled, this, [this]() {
    m_voiceOverAudioEnded = true;
    m_voiceOverStatus = tr("Recording discarded");
    if (m_voiceOverWithWebcam && m_voiceOverWebcam->busy()) {
      m_voiceOverKeptAudio = m_voiceOverTakeCount > 0;
      m_voiceOverWebcam->finish(m_voiceOverWebcamTakes.isEmpty());
      return;
    }
    resetVoiceOverState();
    emit voiceOverFinished(false, QString(), QString());
  });
  auto *webcam = qobject_cast<WebcamRecorder *>(voiceOverWebcam());
  connect(m_voiceOver, &VoiceOverRecorder::audioFormatReady, webcam,
          &WebcamRecorder::startEncoding);
  connect(m_voiceOver, &VoiceOverRecorder::audioBufferReady, webcam,
          &WebcamRecorder::enqueueAudio, Qt::DirectConnection);
}

void Backend::teardownVoiceOverRecorder() {
  // Stop the PCM producer before QObject child destruction reaches the
  // webcam encoder that receives its direct, thread-safe buffer callbacks.
  if (m_voiceOver) {
    m_voiceOver->disconnect(this);
    delete m_voiceOver;
    m_voiceOver = nullptr;
  }
}

void Backend::finishVoiceOverSession(bool keptAnything) {
  const bool ok = keptAnything || m_voiceOverTakeCount > 0;
  if (!ok)
    m_voiceOverStatus = tr("Nothing was recorded.");
  const int placed = m_voiceOverTakeCount;
  resetVoiceOverState();
  if (ok && placed > 1)
    m_voiceOverStatus = tr("Placed %1 takes").arg(placed);
  emit voiceOverChanged();
  emit voiceOverFinished(ok, QString(), ok ? QString() : m_voiceOverStatus);
}

QObject *Backend::voiceOverWebcam() {
  if (!m_voiceOverWebcam) {
    m_voiceOverWebcam = new WebcamRecorder(this);
    connect(m_voiceOverWebcam, &WebcamRecorder::finished, this,
            [this](const QString &path, qint64 duration, bool discarded) {
      if (!m_voiceOverWithWebcam)
        return;
      if (!discarded)
        commitWebcamTakes(path, duration);
      // A camera failure can happen while audio still records. Keep the voice
      // session alive until its worker has actually finished.
      if (m_voiceOverAudioEnded)
        finishVoiceOverSession(m_voiceOverKeptAudio);
    });
    connect(m_voiceOverWebcam, &WebcamRecorder::failed, this,
            [this](const QString &message) {
      if (m_voiceOverWithWebcam && voiceOverBusy()) {
        setError(message);
        stopVoiceOver();
      }
    });
  }
  return m_voiceOverWebcam;
}

void Backend::warmVoiceOverInput() {
  ensureVoiceOverRecorder();
  const QVariantMap settings = appSettings();
  const QAudioDevice device =
      resolveInputDevice(settings.value("voiceOverDeviceId").toString());
  if (device.isNull())
    return;
  m_voiceOver->warm(device, settings.value("voiceOverSampleRate", 48000).toInt(),
                    settings.value("voiceOverChannels", 1).toInt());
}

void Backend::startVoiceOverMonitor() {
  if (voiceOverBusy())
    return;
  ensureVoiceOverRecorder();
  const QVariantMap settings = appSettings();
  const QAudioDevice device =
      resolveInputDevice(settings.value("voiceOverDeviceId").toString());
  if (device.isNull())
    return;
  VoiceOverCaptureConfig config;
  config.meterOnly = true;
  // No path is ever used in metering mode, but begin() takes one.
  m_voiceOver->begin(device,
                     settings.value("voiceOverSampleRate", 48000).toInt(),
                     settings.value("voiceOverChannels", 1).toInt(),
                     QStringLiteral("monitor-%1"), config);
}

void Backend::stopVoiceOverMonitor() {
  // Only tears down a metering session; a real take is left alone.
  if (m_voiceOver && m_voiceOver->monitoring())
    m_voiceOver->discard();
}

QString Backend::voiceOverPathPattern() const {
  // Beside the project when it has been saved, so takes travel with the
  // project; otherwise in app data, because an untitled project has nowhere to
  // put them and refusing to record would be worse.
  QString folder;
  if (!m_projectLocation.isEmpty()) {
    QDir dir(m_projectLocation);
    dir.mkpath(QStringLiteral("Media"));
    folder = dir.filePath(QStringLiteral("Media"));
  } else {
    folder =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/voice-over");
    QDir().mkpath(folder);
  }
  // Timestamped and id-suffixed, with %1 left for the segment number:
  // importMedia rejects a path it has already imported, so no two takes may
  // ever share a filename.
  const QString base =
      appSettings().value("voiceOverName", QStringLiteral("Voiceover")).toString();
  const QString stamp =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  const QString unique = QString::fromStdString(core::new_id("vo")).right(4);
  return QDir(folder).filePath(
      QStringLiteral("%1 %2 %3 take%4.wav")
          .arg(base, stamp, unique, QStringLiteral("%1")));
}

bool Backend::startVoiceOver(const QString &track, qint64 startMs, bool webcam) {
  if (voiceOverBusy())
    return false;
  ensureVoiceOverRecorder();

  const QVariantMap settings = appSettings();
  const QAudioDevice device =
      resolveInputDevice(settings.value("voiceOverDeviceId").toString());
  if (device.isNull()) {
    setError(tr("No microphone was found. Connect one and try again."));
    return false;
  }
  if (webcam && !qobject_cast<WebcamRecorder *>(voiceOverWebcam())->active()) {
    setError(tr("The webcam is not ready. Choose a camera or use Voice only."));
    return false;
  }
  m_voiceOverWithWebcam = webcam;
  m_voiceOverWebcamTakes.clear();
  m_voiceOverKeptAudio = false;
  m_voiceOverAudioEnded = false;
  m_voiceOverStopping = false;

  m_voiceOverStartMs = startMs >= 0 ? startMs : m_playheadMs;
  m_voiceOverTakeCount = 0;
  m_voiceOverLastClipId.clear();
  m_voiceOverHandsFree =
      settings.value("voiceOverHandsFree", true).toBool();
  m_voiceOverPathPattern = voiceOverPathPattern();

  // Resolve the lane up front so the growing bar appears where the take will
  // actually land. Occupancy is re-checked per segment at commit, since the
  // span is not known until a line ends.
  m_voiceOverTrack = track;
  if (m_voiceOverTrack.isEmpty() || trackLocked(m_voiceOverTrack))
    m_voiceOverTrack = freeAudioTrack(m_voiceOverStartMs, m_voiceOverStartMs);

  const int countdown =
      qBound(0, settings.value("voiceOverCountdownSec", 3).toInt(), 10);
  if (countdown > 0) {
    m_voiceOverCountdown = countdown;
    m_voiceOverStatus = tr("Starting in %1...").arg(countdown);
    emit voiceOverChanged();
    if (!m_voiceOverCountdownTimer) {
      m_voiceOverCountdownTimer = new QTimer(this);
      m_voiceOverCountdownTimer->setInterval(1000);
      connect(m_voiceOverCountdownTimer, &QTimer::timeout, this, [this]() {
        if (--m_voiceOverCountdown > 0) {
          m_voiceOverStatus = tr("Starting in %1...").arg(m_voiceOverCountdown);
          emit voiceOverChanged();
          return;
        }
        m_voiceOverCountdownTimer->stop();
        beginVoiceOverCapture();
      });
    }
    m_voiceOverCountdownTimer->start();
    return true;
  }

  beginVoiceOverCapture();
  return true;
}

void Backend::beginVoiceOverCapture() {
  m_voiceOverCountdown = 0;
  m_voiceOverPending = true;
  const QVariantMap settings = appSettings();
  const QAudioDevice device =
      resolveInputDevice(settings.value("voiceOverDeviceId").toString());
  if (device.isNull()) {
    setError(tr("The microphone is no longer available."));
    resetVoiceOverState();
    return;
  }

  // Silencing the programme is left to QML. MonitorPanel already owns the
  // preview level as a binding over mutedTracks / master volume / clip volume
  // and pushes it through setPreviewVolume on every change; Backend cannot read
  // that value back, so saving and restoring it here would guess. Instead the
  // binding folds in voiceOverRecording, which means un-muting at the end of a
  // take is just the binding re-evaluating to whatever is correct by then.

  // Perform to picture: roll the sequence from the take's start position.
  if (settings.value("voiceOverPlayWhileRecording", true).toBool()) {
    m_voiceOverWasPlaying = m_playing;
    setPlayheadMs(m_voiceOverStartMs);
    setPlaying(true);
  }

  VoiceOverCaptureConfig config;
  config.webcam = m_voiceOverWithWebcam;
  if (config.webcam) {
    QString videoPath = m_voiceOverPathPattern;
    videoPath.replace(QStringLiteral("take%1.wav"), QStringLiteral("webcam.mp4"));
    if (!m_voiceOverWebcam->active() || !m_voiceOverWebcam->prepare(videoPath)) {
      m_voiceOverStatus = tr("The webcam is no longer available.");
      resetVoiceOverState();
      emit voiceOverFinished(false, {}, m_voiceOverStatus);
      return;
    }
  }
  config.handsFree = m_voiceOverHandsFree;
  config.silenceFloor = m_voiceOverHandsFree
                            ? amplitudeForDb(settings
                                                 .value("voiceOverSilenceDb",
                                                        -42.0)
                                                 .toDouble())
                            : 0;
  config.hangoverMs = qBound<qint64>(
      Q_INT64_C(300), settings.value("voiceOverHangoverMs", 1200).toLongLong(),
      Q_INT64_C(10000));

  m_voiceOverStatus = tr("Opening the microphone...");
  emit voiceOverChanged();
  m_voiceOver->begin(device,
                     settings.value("voiceOverSampleRate", 48000).toInt(),
                     settings.value("voiceOverChannels", 1).toInt(),
                     m_voiceOverPathPattern, config);
}

void Backend::stopVoiceOver() {
  if (m_voiceOverCountdown > 0) {
    // Stopped during the lead-in: nothing was captured, so this is a cancel.
    if (m_voiceOverCountdownTimer)
      m_voiceOverCountdownTimer->stop();
    m_voiceOverStatus.clear();
    resetVoiceOverState();
    emit voiceOverFinished(false, QString(), QString());
    return;
  }
  if ((!m_voiceOverRecording && !m_voiceOverPending) || !m_voiceOver || m_voiceOverStopping)
    return;
  m_voiceOverStopping = true;
  m_voiceOverStatus = tr("Finishing...");
  emit voiceOverChanged();
  m_voiceOver->finish();
}

void Backend::cancelVoiceOver() {
  if (m_voiceOverCountdown > 0) {
    if (m_voiceOverCountdownTimer)
      m_voiceOverCountdownTimer->stop();
    m_voiceOverStatus.clear();
    resetVoiceOverState();
    emit voiceOverFinished(false, QString(), QString());
    return;
  }
  if ((!m_voiceOverRecording && !m_voiceOverPending) || !m_voiceOver || m_voiceOverStopping)
    return;
  m_voiceOverStopping = true;
  // Only the segment in progress is thrown away. Lines already placed are
  // ordinary clips now, and silently deleting them would be a surprise - undo
  // is the way back from those.
  m_voiceOver->discard();
}

void Backend::resetVoiceOverState() {
  m_voiceOverRecording = false;
  m_voiceOverPending = false;
  m_voiceOverStopping = false;
  m_voiceOverWithWebcam = false;
  m_voiceOverCountdown = 0;
  if (m_voiceOverTickTimer)
    m_voiceOverTickTimer->stop();
  if (m_playing && !m_voiceOverWasPlaying)
    setPlaying(false);
  m_voiceOverWasPlaying = false;
  m_voiceOverPathPattern.clear();
  emit voiceOverChanged();
  emit voiceOverTickChanged();
}

QVariantList Backend::voiceOverMeterLevels() {
  float left = 0.0f;
  float right = 0.0f;
  if (m_voiceOver)
    m_voiceOver->peakLevels(&left, &right);
  return QVariantList{double(left), double(right)};
}

qint64 Backend::voiceOverElapsedMs() const {
  return m_voiceOver ? m_voiceOver->sessionMs() : 0;
}

QString Backend::commitVoiceOverTake(const QString &path, qint64 durationMs,
                                     qint64 offsetMs) {
  if (path.isEmpty() || durationMs <= 0)
    return {};

  // A take needs somewhere to go. A fresh sequence has no audio tracks at all.
  if (m_sequenceId.isEmpty())
    createSequence();

  // Import first, outside the batch: importMedia's failure path pops the undo
  // stack without checking whether a batch is open, so calling it inside one
  // would discard the batch's own snapshot.
  const QString normalized = normalizePath(path);
  const QString key = MediaPath::duplicateKey(normalized);
  QString mediaId;
  const auto findExisting = [&]() {
    for (int i = 0; i < m_media.size() && mediaId.isEmpty(); ++i) {
      const QVariantMap entry = m_media.at(i).toMap();
      if (!key.isEmpty() &&
          MediaPath::duplicateKey(entry.value("path").toString()) == key)
        mediaId = entry.value("id").toString();
    }
  };
  findExisting();
  if (mediaId.isEmpty()) {
    if (importMedia({normalized}, false) != 1)
      return {};
    findExisting();
  }
  if (mediaId.isEmpty())
    return {};

  // Tag it so a bulk cleanup can find generated takes. Deliberately not
  // "text_to_speech", which visibleMedia() filters out of the project panel.
  for (int i = 0; i < m_media.size(); ++i) {
    QVariantMap entry = m_media.at(i).toMap();
    if (entry.value("id").toString() != mediaId)
      continue;
    entry["generatedBy"] = QStringLiteral("voice_over");
    // The recorder counted the samples it wrote; trust that over a probe of a
    // file that was closed a moment ago.
    entry["durationMs"] = durationMs;
    m_media[i] = entry;
    break;
  }

  // Session time -> timeline time. Each line lands where it was actually
  // spoken, which also means lines never overlap and can share one lane.
  const qint64 startMs = m_voiceOverStartMs + qMax<qint64>(0, offsetMs);
  const qint64 endMs = startMs + durationMs;

  QString track = m_voiceOverTrack;
  if (!track.isEmpty()) {
    bool occupied = false;
    for (const QVariant &value : m_clips) {
      const QVariantMap clip = value.toMap();
      if (clip.value("track").toString() != track)
        continue;
      const qint64 clipStart = clip.value("startMs").toLongLong();
      const qint64 clipEnd = clipStart + clip.value("durationMs").toLongLong();
      if (clipStart < endMs && clipEnd > startMs) {
        occupied = true;
        break;
      }
    }
    if (occupied || trackLocked(track))
      track.clear();
  }
  if (track.isEmpty())
    track = freeAudioTrack(startMs, endMs);

  beginEditBatch();
  ensureTrackExists(track);
  const QString clipId = addClip(mediaId, startMs, track);
  endEditBatch();
  // endEditBatch defers only clipsChanged / markersChanged / timelineChanged, so
  // the bin and a newly opened lane have to be published here.
  markDirty();
  emit mediaChanged();
  emit tracksChanged();
  // Deliberately not selecting the clip in hands-free mode: the session is
  // still running and stealing the selection between lines would fight whatever
  // the user is doing in the panel.
  if (!clipId.isEmpty() && !m_voiceOverHandsFree)
    setSelectedClipId(clipId);
  return clipId;
}

void Backend::commitWebcamTakes(const QString &path, qint64 durationMs) {
  if (m_voiceOverWebcamTakes.isEmpty() || durationMs <= 0)
    return;
  if (importMedia({path}, false) != 1) {
    setError(tr("Webcam video was saved but could not be added to the timeline."));
    return;
  }
  QString mediaId;
  for (const auto &value : m_media) {
    const auto media = value.toMap();
    if (MediaPath::duplicateKey(media.value("path").toString()) ==
        MediaPath::duplicateKey(path)) {
      mediaId = media.value("id").toString();
      break;
    }
  }
  if (mediaId.isEmpty())
    return;
  beginEditBatch();
  for (const auto &take : m_voiceOverWebcamTakes) {
    const int audioIndex = clipIndex(take.clipId);
    if (audioIndex < 0)
      continue; // A take deleted while listening should stay deleted.
    auto audio = m_clips[audioIndex].toMap();
    const qint64 sourceIn = take.offsetMs + audio.value("sourceInMs").toLongLong();
    const qint64 length = qMin(audio.value("durationMs").toLongLong(),
                               durationMs - sourceIn);
    if (length <= 0)
      continue;
    const qint64 start = audio.value("startMs").toLongLong();
    QString track;
    for (int lane = 1; lane <= 64; ++lane) {
      const QString candidate = QStringLiteral("V%1").arg(lane);
      if (trackLocked(candidate))
        continue;
      bool occupied = false;
      for (const auto &value : m_clips) {
        const auto clip = value.toMap();
        const qint64 at = clip.value("startMs").toLongLong();
        if (clip.value("track").toString() == candidate &&
            at < start + length && at + clip.value("durationMs").toLongLong() > start) {
          occupied = true;
          break;
        }
      }
      if (!occupied) { track = candidate; break; }
    }
    if (track.isEmpty()) {
      setError(tr("No free video track is available. Webcam video is saved in the project media."));
      break;
    }
    const QString videoId = addClip(mediaId, start, track);
    const int videoIndex = clipIndex(videoId);
    if (videoIndex < 0)
      continue;
    auto video = m_clips[videoIndex].toMap();
    const QString group = QString::fromStdString(core::new_id("link"));
    video["sourceInMs"] = sourceIn;
    video["sourceDurationMs"] = durationMs;
    video["durationMs"] = length;
    // The MP4 also contains the original microphone. Playback/export use the
    // linked WAV instead, including its chosen voice, so it must not play twice.
    video["separateAudio"] = true;
    video["linkedRole"] = QStringLiteral("video");
    video["linkGroupId"] = group;
    audio["linkedRole"] = QStringLiteral("audio");
    audio["linkGroupId"] = group;
    m_clips[videoIndex] = video;
    m_clips[audioIndex] = audio;
  }
  endEditBatch();
  emit mediaChanged();
  emit tracksChanged();
}
