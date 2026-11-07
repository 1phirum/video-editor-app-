// Voice changer: pick a voice, hear it, then commit it.
//
// WHY THIS RENDERS INSTEAD OF FILTERING
//
// Every audio effect in this application is export-only. AudioEffectPipeline
// ::filters and ClipEffectsPipeline::audioFilters each have exactly one caller -
// SequenceExportBuilder - and nothing under src/app/preview/ ever builds an
// audio filter chain; the preview decoder emits a bare "-f s16le pipe:1" with no
// -af at all. A voice built as a clip effect would therefore be inaudible until
// the file was rendered, which is useless for a feature whose entire purpose is
// choosing between voices by ear.
//
// So a preset is rendered to a .wav and the clip is pointed at the result. That
// is the same trick vocal separation already uses - it substitutes Demucs's
// accompaniment stem for the original source - and it means preview, timeline
// playback and export all agree without touching the preview pipeline. The
// original media stays in the bin, and the clip remembers what it used to point
// at, so Revert is exact rather than an inverse filter.
//
// WHY EVERY CHAIN KEEPS ITS DURATION
//
// `rubberband` shifts pitch with tempo left at 1, so output length equals input
// length. This matters more than it looks: the clip keeps its timeline span, so
// applying a voice cannot shift everything after it. The naive pitch trick -
// asetrate, which just plays the samples faster - changes duration and is
// deliberately not used anywhere here.
//
// WHY FORMANTS ARE A SEPARATE AXIS
//
// Pitch alone gives you a chipmunk. What makes a voice sound like a *different
// person* rather than the same person sped up is the formants - the resonances
// of the throat and mouth, which scale with body size.
// `rubberband=formant=preserved` holds them while pitch moves, which reads as
// the same speaker pitched up or down; `formant=shifted` lets them move with
// the pitch, which reads as a different-sized speaker. The presets below use
// that distinction on purpose, and it is the single thing that separates this
// from a toy.

#include "app/core_app/backend.h"

#include "app/media/ffmpeg_runtime.h"
#include "app/media/media_path.h"
#include "app/settings/app_settings.h"
#include "core/ids.h"

#include <QAudioOutput>
#include <QDir>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QStandardPaths>
#include <QUrl>

namespace {

struct VoicePreset {
  const char *id;
  const char *name;
  const char *description;
  const char *chain;
};

// Each chain was checked against the bundled ffmpeg build (which does carry
// librubberband) and confirmed to return exactly the input duration.
const QVector<VoicePreset> &voicePresetTable() {
  static const QVector<VoicePreset> table{
      {"none", "Original", "Your voice, unprocessed", "anull"},

      // --- same speaker, moved in pitch (formants held) -------------------
      {"warm", "Warm",
       "Slightly deeper, same voice. Good for narration.",
       "rubberband=pitch=0.92:formant=preserved:pitchq=quality,"
       "equalizer=f=160:t=h:w=120:g=2,equalizer=f=3200:t=q:w=1.2:g=1.5"},
      {"deep", "Deep",
       "Noticeably lower, still recognisably you.",
       "rubberband=pitch=0.82:formant=preserved:pitchq=quality,"
       "asubboost=dry=0.9:wet=0.4:decay=0.5"},
      {"bright", "Bright",
       "A little higher and more forward.",
       "rubberband=pitch=1.10:formant=preserved:pitchq=quality,"
       "equalizer=f=4500:t=q:w=1.4:g=2"},

      // --- a different speaker (formants move with the pitch) -------------
      {"bigger", "Bigger person",
       "Lower pitch and larger-sounding throat.",
       "rubberband=pitch=0.78:formant=shifted:pitchq=quality"},
      {"smaller", "Smaller person",
       "Higher pitch and a smaller-sounding throat.",
       "rubberband=pitch=1.28:formant=shifted:pitchq=quality"},
      {"child", "Child",
       "Much higher, with child-sized resonance.",
       "rubberband=pitch=1.48:formant=shifted:pitchq=quality,"
       "equalizer=f=250:t=h:w=200:g=-2"},
      {"chipmunk", "Chipmunk",
       "Cartoon high. Unmistakably an effect.",
       "rubberband=pitch=1.85:formant=shifted:pitchq=quality"},
      {"monster", "Monster",
       "Very low and heavy, with sub weight added.",
       "rubberband=pitch=0.60:formant=shifted:pitchq=quality,"
       "asubboost=dry=0.7:wet=0.85:decay=0.6,"
       "equalizer=f=3000:t=q:w=1.5:g=-3"},

      // --- character / transmission ---------------------------------------
      {"robot", "Robot",
       "Bit-crushed and ring-modulated.",
       "rubberband=pitch=0.95:formant=shifted:pitchq=quality,"
       "acrusher=bits=6:mix=0.5:mode=log,tremolo=f=58:d=0.55"},
      {"telephone", "Telephone",
       "Band-limited to a phone line.",
       "highpass=f=320,lowpass=f=3200,"
       "compand=attacks=0:decays=0.2:points=-70/-70|-30/-12|0/-6"},
      {"megaphone", "Megaphone",
       "Harsh, mid-forward, with a short slap.",
       "highpass=f=480,lowpass=f=4200,acrusher=bits=8:mix=0.35:mode=log,"
       "aecho=0.8:0.55:11:0.3,equalizer=f=1800:t=q:w=1.2:g=4"},
      {"radio", "Radio announcer",
       "Not a different voice - de-essed, compressed and polished.",
       "deesser=i=0.4,equalizer=f=120:t=h:w=100:g=-3,"
       "equalizer=f=2600:t=q:w=1.3:g=2.5,"
       "compand=attacks=0.02:decays=0.3:points=-70/-70|-24/-10|-6/-5|0/-4,"
       "aexciter=level_in=1:level_out=1:amount=1.2"},
      {"hall", "Distant hall",
       "Pushed back into a room.",
       "aecho=0.7:0.6:45|75|110:0.32|0.24|0.16,"
       "equalizer=f=5000:t=q:w=1.5:g=-2"}};
  return table;
}

const VoicePreset *findVoicePreset(const QString &id) {
  for (const VoicePreset &preset : voicePresetTable()) {
    if (id == QLatin1String(preset.id))
      return &preset;
  }
  return nullptr;
}

// Preview length. Long enough to judge a voice, short enough that a render feels
// immediate even with rubberband's quality mode on.
constexpr qint64 kVoicePreviewMs = 5000;

QString voiceFxRoot() {
  const QString root =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      QStringLiteral("/voice-fx");
  QDir().mkpath(root);
  return root;
}

} // namespace

QVariantList Backend::voicePresets() const {
  QVariantList list;
  for (const VoicePreset &preset : voicePresetTable()) {
    list.append(QVariantMap{
        {QStringLiteral("id"), QString::fromLatin1(preset.id)},
        {QStringLiteral("name"), tr(preset.name)},
        {QStringLiteral("description"), tr(preset.description)}});
  }
  return list;
}

QString Backend::clipVoiceId(const QString &clipId) const {
  const int index = clipIndex(clipId);
  if (index < 0)
    return {};
  return m_clips.at(index).toMap().value("voiceFxId").toString();
}

bool Backend::startVoiceRender(const QString &clipId, const QString &presetId,
                               bool wholeClip) {
  if (m_voiceFxBusy)
    return false;
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  const VoicePreset *preset = findVoicePreset(presetId);
  if (!preset) {
    setError(tr("That voice is not available."));
    return false;
  }

  const QVariantMap clip = m_clips.at(index).toMap();
  const QString path =
      mediaById(clip.value("mediaId").toString()).value("path").toString();
  if (path.isEmpty()) {
    setError(tr("This clip has no source audio."));
    return false;
  }
  const QString ffmpegExe = FfmpegRuntime::executable();
  if (ffmpegExe.isEmpty() || !QFileInfo::exists(ffmpegExe)) {
    setError(tr("The bundled ffmpeg could not be found."));
    return false;
  }

  // The span of source the clip actually uses, scaled by its speed - the same
  // arithmetic the scene scan uses, so a retimed clip renders the right range.
  const QVariantMap effects = clip.value("effects").toMap();
  const double speed = qMax(0.01, effects.value("speed", 100.0).toDouble());
  const qint64 sourceIn = clip.value("sourceInMs").toLongLong();
  const qint64 usedMs =
      qRound64(clip.value("durationMs").toLongLong() * speed / 100.0);
  const qint64 renderMs = wholeClip ? usedMs : qMin(usedMs, kVoicePreviewMs);
  if (renderMs <= 0) {
    setError(tr("This clip is too short to render."));
    return false;
  }

  // Cached per preset and per source span, so re-auditioning a voice is
  // instant and a different trim renders separately.
  const QString folder =
      voiceFxRoot() + QStringLiteral("/") + QString::fromLatin1(preset->id);
  QDir().mkpath(folder);
  const QString stem =
      QStringLiteral("%1-%2-%3-%4")
          .arg(MediaPath::duplicateKey(path).right(12))
          .arg(sourceIn)
          .arg(renderMs)
          .arg(wholeClip ? QStringLiteral("full") : QStringLiteral("prev"));
  m_voiceFxOutputPath = QDir(folder).filePath(stem + QStringLiteral(".wav"));
  m_voiceFxClipId = clipId;
  m_voiceFxClipSnapshot = clip;
  m_voiceFxPresetId = presetId;
  m_voiceFxApplying = wholeClip;

  // A cache hit skips the child process entirely.
  if (QFileInfo::exists(m_voiceFxOutputPath) &&
      QFileInfo(m_voiceFxOutputPath).size() > 1024) {
    m_voiceFxBusy = false;
    handleVoiceFxFinished(0, QProcess::NormalExit);
    return true;
  }

  if (!m_voiceFxWired) {
    m_voiceFxWired = true;
    connect(&m_voiceFxProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
              handleVoiceFxFinished(code, status);
            });
  }
#ifdef Q_OS_WIN
  m_voiceFxProcess.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
      });
#endif

  const QStringList args{
      QStringLiteral("-hide_banner"),
      QStringLiteral("-nostats"),
      QStringLiteral("-ss"),
      QString::number(sourceIn / 1000.0, 'f', 3),
      QStringLiteral("-t"),
      QString::number(renderMs / 1000.0, 'f', 3),
      QStringLiteral("-i"),
      path,
      QStringLiteral("-vn"),
      QStringLiteral("-sn"),
      QStringLiteral("-af"),
      QString::fromLatin1(preset->chain),
      QStringLiteral("-ac"),
      QStringLiteral("2"),
      QStringLiteral("-ar"),
      QStringLiteral("48000"),
      QStringLiteral("-c:a"),
      QStringLiteral("pcm_s16le"),
      QStringLiteral("-y"),
      m_voiceFxOutputPath};

  m_voiceFxBusy = true;
  m_voiceFxPreviewId = presetId;
  m_voiceFxStatus = wholeClip ? tr("Applying %1...").arg(tr(preset->name))
                              : tr("Rendering %1...").arg(tr(preset->name));
  emit voiceFxChanged();
  m_voiceFxProcess.start(ffmpegExe, args);
  if (!m_voiceFxProcess.waitForStarted(2000)) {
    m_voiceFxBusy = false;
    m_voiceFxStatus = tr("The voice render could not be started.");
    emit voiceFxChanged();
    emit voiceFxFinished(false, presetId, m_voiceFxStatus);
    return false;
  }
  return true;
}

void Backend::handleVoiceFxFinished(int exitCode, QProcess::ExitStatus status) {
  m_voiceFxBusy = false;
  const QString presetId = m_voiceFxPresetId;
  const bool applying = m_voiceFxApplying;
  const QString path = m_voiceFxOutputPath;

  if (status != QProcess::NormalExit || exitCode != 0 ||
      !QFileInfo::exists(path)) {
    m_voiceBatchFailed = true;
    m_voiceFxStatus = tr("The voice could not be rendered.");
    m_voiceFxPreviewId.clear();
    emit voiceFxChanged();
    emit voiceFxFinished(false, presetId, m_voiceFxStatus);
    pumpVoiceQueue();
    return;
  }

  if (!applying) {
    // Audition it. QMediaPlayer rather than the preview decoder on purpose: the
    // monitor's session is slaved to the sequence and hijacking it to play a
    // five-second sample would move the playhead and stop the picture.
    if (!m_voicePreviewPlayer) {
      m_voicePreviewPlayer = new QMediaPlayer(this);
      m_voicePreviewOutput = new QAudioOutput(this);
      m_voicePreviewPlayer->setAudioOutput(m_voicePreviewOutput);
      connect(m_voicePreviewPlayer, &QMediaPlayer::playbackStateChanged, this,
              [this](QMediaPlayer::PlaybackState state) {
                if (state == QMediaPlayer::StoppedState) {
                  m_voiceFxPreviewId.clear();
                  m_voiceFxStatus.clear();
                  emit voiceFxChanged();
                }
              });
    }
    m_voicePreviewPlayer->setSource(QUrl::fromLocalFile(path));
    m_voicePreviewOutput->setVolume(1.0);
    m_voicePreviewPlayer->play();
    m_voiceFxStatus = tr("Playing sample");
    emit voiceFxChanged();
    emit voiceFxFinished(true, presetId, QString());
    return;
  }

  const int index = clipIndex(m_voiceFxClipId);
  const QVariantMap current = index >= 0 ? m_clips.at(index).toMap() : QVariantMap{};
  bool unchanged = !current.isEmpty();
  for (const QString &key : {QString("mediaId"), QString("sourceInMs"), QString("durationMs")})
    unchanged = unchanged && current.value(key) == m_voiceFxClipSnapshot.value(key);
  unchanged = unchanged && current.value("effects").toMap().value("speed", 100.0) ==
                               m_voiceFxClipSnapshot.value("effects").toMap().value("speed", 100.0);
  if (!unchanged || editableClipIds({m_voiceFxClipId}, "audio").isEmpty()) {
    m_voiceBatchFailed = true;
    m_voiceFxStatus = tr("The clip changed while its voice was rendering.");
    emit voiceFxChanged();
    emit voiceFxFinished(false, presetId, m_voiceFxStatus);
    pumpVoiceQueue();
    return;
  }
  const qint64 durationMs =
      index >= 0 ? m_clips.at(index).toMap().value("durationMs").toLongLong() : 0;
  const QString clipId =
      applyRenderedVoice(m_voiceFxClipId, presetId, path, durationMs);
  const bool ok = !clipId.isEmpty();
  if (!ok)
    m_voiceBatchFailed = true;
  m_voiceFxPreviewId.clear();
  m_voiceFxStatus = ok ? tr("Voice applied") : tr("The voice could not be applied.");
  emit voiceFxChanged();
  emit voiceFxFinished(ok, presetId, ok ? QString() : m_voiceFxStatus);
  pumpVoiceQueue();
}

QString Backend::applyRenderedVoice(const QString &clipId,
                                    const QString &presetId,
                                    const QString &path, qint64 durationMs) {
  const int index = clipIndex(clipId);
  if (index < 0 || path.isEmpty())
    return {};

  // Import outside any batch: importMedia's failure path pops the undo stack
  // without checking whether a batch is open.
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

  // Keep the rendered voice out of the project panel. It is an intermediate of
  // one clip, not a piece of source footage the user went and found, and a bin
  // filling up with one entry per audition is noise.
  for (int i = 0; i < m_media.size(); ++i) {
    QVariantMap entry = m_media.at(i).toMap();
    if (entry.value("id").toString() != mediaId)
      continue;
    entry["generatedBy"] = QStringLiteral("voice_fx");
    entry["hiddenInProjectPanel"] = true;
    m_media[i] = entry;
    break;
  }

  rememberState();
  QVariantMap clip = m_clips.at(index).toMap();
  // Remember the original once, so applying a second voice still reverts to the
  // real source rather than to the previous render.
  if (!clip.contains("voiceFxOriginalMediaId")) {
    clip["voiceFxOriginalMediaId"] = clip.value("mediaId");
    clip["voiceFxOriginalSourceInMs"] = clip.value("sourceInMs");
  }
  clip["voiceFxId"] = presetId;
  clip["mediaId"] = mediaId;
  // The render covers exactly the clip's used span, so the new source starts at
  // zero. Duration is left alone: every chain preserves length.
  clip["sourceInMs"] = 0;
  if (durationMs > 0)
    clip["durationMs"] = durationMs;
  m_clips[index] = clip;

  markDirty();
  emit mediaChanged();
  notifyClipEdit(true);
  return clipId;
}

void Backend::enqueueVoiceRender(const QString &clipId,
                                 const QString &presetId) {
  if (clipId.isEmpty() || presetId.isEmpty() ||
      presetId == QStringLiteral("none"))
    return;
  m_voiceFxQueue.append({clipId, presetId});
  pumpVoiceQueue();
}

void Backend::pumpVoiceQueue() {
  if (m_voiceFxBusy || m_voiceQueuePumping)
    return;
  m_voiceQueuePumping = true;
  while (!m_voiceFxBusy && !m_voiceFxQueue.isEmpty()) {
    const auto job = m_voiceFxQueue.takeFirst();
    if (editableClipIds({job.first}, "audio").isEmpty()) {
      m_voiceBatchFailed = true;
      continue;
    }
    if (!startVoiceRender(job.first, job.second, true))
      m_voiceBatchFailed = true;
    // Cache hits finish synchronously; the guard prevents recursive pumping.
  }
  m_voiceQueuePumping = false;
  emit voiceFxChanged();
  if (!m_voiceFxBusy && m_voiceFxQueue.isEmpty() && m_voiceBatchActive) {
    m_voiceBatchActive = false;
    m_voiceFxStatus = m_voiceBatchFailed
        ? tr("Some selected voices could not be applied.") : tr("Voice applied to selected clips");
    emit voiceFxChanged();
    emit voiceFxFinished(!m_voiceBatchFailed, m_voiceFxPresetId,
                         m_voiceBatchFailed ? m_voiceFxStatus : QString());
  }
}

bool Backend::applyClipsVoice(const QStringList &requested, const QString &presetId) {
  if (voiceFxBusy())
    return false;
  const QStringList ids = editableClipIds(requested, "audio");
  if (ids.isEmpty())
    return false;
  stopVoicePreview();
  if (presetId == QStringLiteral("none")) {
    beginEditBatch();
    for (const QString &id : ids)
      revertVoice(id);
    endEditBatch();
    emit voiceFxFinished(true, presetId, QString());
    return true;
  }
  if (!findVoicePreset(presetId))
    return false;
  m_voiceBatchActive = true;
  m_voiceBatchFailed = false;
  for (const QString &id : ids)
    m_voiceFxQueue.append({id, presetId});
  pumpVoiceQueue();
  return true;
}

bool Backend::previewVoice(const QString &clipId, const QString &presetId) {
  stopVoicePreview();
  // "Original" is rendered too, through anull. Auditioning the untouched voice
  // the same way as the processed ones is what makes an A/B comparison fair -
  // decoding the raw source by a different path would differ in level and
  // sample rate as well as in voice.
  return startVoiceRender(clipId, presetId, false);
}

void Backend::stopVoicePreview() {
  if (m_voicePreviewPlayer &&
      m_voicePreviewPlayer->playbackState() != QMediaPlayer::StoppedState)
    m_voicePreviewPlayer->stop();
  if (m_voiceFxBusy && !m_voiceFxApplying) {
    m_voiceFxProcess.kill();
    m_voiceFxBusy = false;
  }
  m_voiceFxPreviewId.clear();
  m_voiceFxStatus.clear();
  emit voiceFxChanged();
}

bool Backend::applyVoice(const QString &clipId, const QString &presetId) {
  stopVoicePreview();
  if (presetId == QStringLiteral("none"))
    return revertVoice(clipId);
  return startVoiceRender(clipId, presetId, true);
}

bool Backend::revertVoice(const QString &clipId) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  if (!clip.contains("voiceFxOriginalMediaId"))
    return false;

  rememberState();
  clip["mediaId"] = clip.value("voiceFxOriginalMediaId");
  clip["sourceInMs"] = clip.value("voiceFxOriginalSourceInMs");
  clip.remove("voiceFxOriginalMediaId");
  clip.remove("voiceFxOriginalSourceInMs");
  clip.remove("voiceFxId");
  m_clips[index] = clip;

  markDirty();
  notifyClipEdit(true);
  m_voiceFxStatus = tr("Original voice restored");
  emit voiceFxChanged();
  return true;
}
