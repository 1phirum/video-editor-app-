#include "app/core_app/backend.h"

#include "app/settings/app_settings.h"
#include "app/diagnostics/crash_reporter_host.h"
#include "app/diagnostics/model_guard.h"
#include "app/diagnostics/playback_trace.h"
#include "app/lumetri/color_settings.h"
#include "app/preview/audio_peak_window_service.h"
#include "app/preview/decode_cost_model.h"
#include "app/preview/decode_work_governor.h"
#include "app/preview/gui_dispatch.h"
#include "app/preview/gui_thread_watchdog.h"
#include "app/preview/preview_decode_policy.h"
#include "app/preview/preview_failure_registry.h"
#include "app/preview/startup_warmup.h"
#include "app/preview/timeline_preview_prefetcher.h"
#include "app/preview/timeline_thumbnail_service.h"
#include "tools/whisper_worker/model_paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFuture>
#include <QMutexLocker>
#include <QSet>
#include <QSize>
#include <QVector>
#include <QVector4D>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <utility>

namespace {
// How long a timeline gesture's hold survives without being touched. A drag
// delivers move events far more often than this, so it only ever fires for a
// gesture that ended without reporting it.
constexpr int kTimelineInteractionLapseMs = 1500;

QString formatBytes(qint64 bytes) {
  const double value = qMax<qint64>(0, bytes);
  if (value < 1024.0)
    return QStringLiteral("%1 B").arg(qRound64(value));
  if (value < 1024.0 * 1024.0)
    return QStringLiteral("%1 KB").arg(value / 1024.0, 0, 'f', 1);
  if (value < 1024.0 * 1024.0 * 1024.0)
    return QStringLiteral("%1 MB")
        .arg(value / (1024.0 * 1024.0), 0, 'f', 1);
  return QStringLiteral("%1 GB")
      .arg(value / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

// Upper bound on a scrub still. Deliberately not a small constant any more: a
// still that is softer than the source is a quality loss the user cannot undo at
// export, and the monitor is what they judge the file by. PreviewDecodePolicy
// returns the source's own resolution unless the panel on screen is smaller, so
// the frame cache buys positions at the resolution actually being displayed.
QSize previewFrameBound(int sourceWidth, int sourceHeight) {
  return PreviewDecodePolicy::stillSize(sourceWidth, sourceHeight);
}

// How long the checkpoint folder has to stay quiet before the picker is told
// again. A model download writes to a .part file and renames on completion, so
// the folder is touched when the file lands; the delay coalesces the flurry of
// events a rename plus its temp cleanup produce into one picker rebuild.
constexpr int kWhisperCacheRescanDelayMs = 3000;

struct WhisperCheckpoint {
  const char *model;
  const char *fileName;
  qint64 minimumBytes;
};

constexpr qint64 kMegabyte = 1024 * 1024;

// The GGML weight file each model name lands in, and the size below which it
// cannot be a finished checkpoint. The file names are the ones the native
// worker resolves (tools/whisper_worker/model_paths.h) and downloads from
// huggingface.co/ggerganov/whisper.cpp - keep the two in step.
//
// A partial download is written as "<name>.bin.part" and renamed on success, so
// an interrupted transfer never matches these names at all. The floors are a
// second guard at ~80% of each real file, well above any stray fragment and
// below any complete one.
const QVector<WhisperCheckpoint> &whisperCheckpoints() {
  static const QVector<WhisperCheckpoint> checkpoints{
      {"tiny", "ggml-tiny.bin", 60 * kMegabyte},
      {"base", "ggml-base.bin", 110 * kMegabyte},
      {"small", "ggml-small.bin", 360 * kMegabyte},
      {"medium", "ggml-medium.bin", 1200 * kMegabyte},
      {"large-v3", "ggml-large-v3.bin", 2400 * kMegabyte},
      {"turbo", "ggml-large-v3-turbo.bin", 1200 * kMegabyte}};
  return checkpoints;
}

// Where the native worker looks for its weights, in search order: the
// CUTPRO_WHISPER_CACHE override, the writable per-user dir (%LOCALAPPDATA%\Cut
// Pro\models\whisper, where downloads now land), then the legacy beside-exe dir
// (pre-fix builds and dev/portable trees). The worker and this probe share the
// same model_paths.h resolver, so they cannot disagree.
QStringList whisperCacheRoots() { return WhisperModels::searchRoots(); }
} // namespace

QVariantMap Backend::defaultAppSettings() const {
  return AppSettings::defaults();
}

QString Backend::mediaCachePath() const { return AppSettings::cacheRoot(); }

void Backend::measureMediaCache() const {
  if (m_mediaCacheMeasuring || m_mediaCacheClearing)
    return;
  m_mediaCacheMeasuring = true;
  // const because the getters that need the answer are const property reads. The
  // continuation needs to emit, and signals are not const, so it works through a
  // non-const handle to the same object.
  Backend *self = const_cast<Backend *>(this);
  QtConcurrent::run([]() {
    return std::pair<qint64, int>(AppSettings::cacheSizeBytes(),
                                  AppSettings::generatedSpeechFileCount());
  }).then(self, [self](std::pair<qint64, int> result) {
    self->m_mediaCacheMeasuring = false;
    self->m_mediaCacheMeasured = true;
    self->m_mediaCacheSizeText = formatBytes(result.first);
    self->m_generatedSpeechFiles = result.second;
    emit self->mediaCacheChanged();
  });
}

QString Backend::mediaCacheSize() const {
  // Started by whichever binding reads this first, and answered by a later
  // mediaCacheChanged. Measuring inline used to be a stat per file on the GUI
  // thread while the settings page was opening - fine for two preview folders,
  // not for the tens of thousands of generated voice files now counted with them.
  if (m_mediaCacheClearing)
    return tr("Clearing...");
  if (!m_mediaCacheMeasured) {
    measureMediaCache();
    return tr("Measuring...");
  }
  return m_mediaCacheSizeText;
}

QString Backend::mediaCacheDetail() const {
  if (m_mediaCacheClearing || !m_mediaCacheMeasured)
    return QString();
  if (m_generatedSpeechFiles <= 0)
    return tr("Previews only");
  return m_generatedSpeechFiles == 1
             ? tr("Includes 1 generated voice file")
             : tr("Includes %1 generated voice files")
                   .arg(m_generatedSpeechFiles);
}

QStringList Backend::downloadedWhisperModels() const {
  QStringList downloaded;
  const QStringList roots = whisperCacheRoots();
  for (const WhisperCheckpoint &checkpoint : whisperCheckpoints()) {
    for (const QString &root : roots) {
      const QFileInfo file(QDir(root).filePath(checkpoint.fileName));
      if (file.isFile() && file.size() >= checkpoint.minimumBytes) {
        downloaded.append(checkpoint.model);
        break;
      }
    }
  }
  return downloaded;
}

void Backend::configureWhisperCacheWatch() {
  if (m_whisperCacheWatcher)
    return;
  m_whisperCacheWatcher = new QFileSystemWatcher(this);
  m_whisperCacheRescanTimer.setSingleShot(true);
  m_whisperCacheRescanTimer.setInterval(kWhisperCacheRescanDelayMs);
  connect(&m_whisperCacheRescanTimer, &QTimer::timeout, this, [this]() {
    // Re-arm first: the folder that had to be created is now there, and this is
    // where a first-ever download starts being watched directly.
    updateWhisperCacheWatchPaths();
    const QStringList current = downloadedWhisperModels();
    if (current == m_whisperModelsSnapshot)
      return;
    m_whisperModelsSnapshot = current;
    emit whisperModelsChanged();
  });
  connect(m_whisperCacheWatcher, &QFileSystemWatcher::directoryChanged, this,
          [this](const QString &) { m_whisperCacheRescanTimer.start(); });
  m_whisperModelsSnapshot = downloadedWhisperModels();
  updateWhisperCacheWatchPaths();
}

void Backend::updateWhisperCacheWatchPaths() {
  if (!m_whisperCacheWatcher)
    return;
  QStringList wanted;
  for (const QString &root : whisperCacheRoots()) {
    if (QFileInfo(root).isDir()) {
      wanted.append(root);
      continue;
    }
    // No cache folder yet, which is the state a machine that has never
    // downloaded a checkpoint is in. Watching the parent catches the folder
    // being created, and the timer's re-arm then moves the watch onto it.
    const QString parent = QFileInfo(root).absolutePath();
    if (!parent.isEmpty() && parent != root && QFileInfo(parent).isDir() &&
        !wanted.contains(parent))
      wanted.append(parent);
  }
  const QStringList existing = m_whisperCacheWatcher->directories();
  QStringList stale;
  for (const QString &path : existing) {
    if (!wanted.contains(path))
      stale.append(path);
  }
  if (!stale.isEmpty())
    m_whisperCacheWatcher->removePaths(stale);
  QStringList added;
  for (const QString &path : wanted) {
    if (!existing.contains(path))
      added.append(path);
  }
  if (!added.isEmpty())
    m_whisperCacheWatcher->addPaths(added);
}

QString Backend::previewFrameUrl() const {
  if (previewFrameImage().isNull())
    return {};
  // Only the frame revision. The grade used to be part of this URL, because a
  // slider release changed the pixels the provider would hand back; with the
  // chain in the fragment stage it changes a uniform instead, and re-requesting
  // the image per slider tick would cost a full decode-size copy and a texture
  // upload for a picture that has not changed.
  return QStringLiteral("image://ffmpeg-preview/frame?revision=%1")
      .arg(m_previewFrameRevision);
}

void Backend::refreshPreviewGrade() {
  // The clip the monitor decodes, not activeColorClip(): that one prefers the
  // selected clip, which can be on a hidden track or nowhere near the playhead.
  const QVariantMap clipColor =
      m_videoPreviewHelper.activeClipMap().value("lumetri").toMap();
  const QVariantMap mediaColor =
      m_videoPreviewHelper.activeMediaMap().value("color").toMap();
  if (m_previewGradeSeeded && clipColor == m_previewGradeClipColor &&
      mediaColor == m_previewGradeMediaColor &&
      m_colorSettings == m_previewGradeProjectColor)
    return;
  m_previewGradeClipColor = clipColor;
  m_previewGradeMediaColor = mediaColor;
  m_previewGradeProjectColor = m_colorSettings;
  m_previewGradeSeeded = true;

  const LumetriPreview::Correction correction =
      LumetriPreview::build(clipColor, mediaColor, m_colorSettings);
  {
    QMutexLocker locker(&m_previewGradeMutex);
    m_previewGrade = correction;
    m_previewGradedFrame = QImage();
    ++m_previewGradeGeneration;
  }
  rebuildLumetriUniforms();
  emit lumetriGradeChanged();
  // Only the CPU path changes the frame the provider hands out, so only it needs
  // QML to re-read the image.
  if (!m_gpuGrading)
    emit previewFrameChanged();
}

void Backend::setGpuGrading(bool enabled) {
  if (m_gpuGrading == enabled)
    return;
  m_gpuGrading = enabled;
  {
    // The CPU path is about to start or stop being the one that matters, and its
    // cached frame was graded under the other rule.
    QMutexLocker locker(&m_previewGradeMutex);
    m_previewGradedFrame = QImage();
  }
  emit lumetriGradeChanged();
  emit previewFrameChanged();
}

QImage Backend::gradedPreviewFrameImage() const {
  const QImage source = previewFrameImage();
  // With a shader chain in front of the picture this would grade it twice.
  if (m_gpuGrading || source.isNull())
    return source;
  const quint64 revision = m_previewFrameRevision;
  LumetriPreview::Correction correction;
  quint64 generation = 0;
  {
    QMutexLocker locker(&m_previewGradeMutex);
    if (m_previewGrade.identity)
      return source;
    if (!m_previewGradedFrame.isNull() &&
        m_previewGradedRevision == revision &&
        m_previewGradedGeneration == m_previewGradeGeneration)
      return m_previewGradedFrame;
    correction = m_previewGrade;
    generation = m_previewGradeGeneration;
  }
  // A deep copy: the decoder and the scrub service both hand out a view of a
  // buffer they still own, and grading in place would corrupt the frame cache.
  //
  // Cap the long edge first. This is the software fallback - the GPU path above
  // graded in the fragment stage and returned already - so a paused 4K still
  // would otherwise run LumetriPreview::apply() over ~8M pixels on the GUI thread
  // on every scrub and every grade tweak. 2560 is the same ceiling playback
  // decodes to (PreviewDecodePolicy::kPlaybackCeilingSide), past which per-frame
  // work, not detail, is what the monitor is bound by; the Scopes path already
  // caps for the same reason (scopeSourceImage). KeepAspectRatio + Smooth so the
  // reduction does not invent colour the source never had.
  constexpr int kCpuGradeMaxSide = 2560;
  QImage graded;
  if (source.width() > kCpuGradeMaxSide || source.height() > kCpuGradeMaxSide)
    graded = source.scaled(kCpuGradeMaxSide, kCpuGradeMaxSide,
                           Qt::KeepAspectRatio, Qt::SmoothTransformation);
  else
    graded = source.copy();
  LumetriPreview::apply(&graded, correction);
  QMutexLocker locker(&m_previewGradeMutex);
  m_previewGradedFrame = graded;
  m_previewGradedRevision = revision;
  m_previewGradedGeneration = generation;
  return graded;
}

void Backend::rebuildLumetriUniforms() {
  LumetriPreview::Correction c;
  {
    QMutexLocker locker(&m_previewGradeMutex);
    c = m_previewGrade;
  }

  const bool hueActive = std::abs(c.hueDegrees) > 0.0001f;
  const bool saturationActive = std::abs(c.saturation - 1.0f) > 0.0001f;
  const bool lightnessActive = std::abs(c.lightness) > 0.0001f;
  const double radians = double(c.hueDegrees) * M_PI / 180.0;
  const float lutMode = c.lut ? (c.lutTetrahedral ? 1.0f : 2.0f) : 0.0f;

  QVariantMap u;
  u.insert(QStringLiteral("gFlags"),
           QVector4D(lutMode, c.toneActive ? 1.0f : 0.0f,
                     c.curvesActive ? 1.0f : 0.0f,
                     c.balanceActive ? 1.0f : 0.0f));
  u.insert(QStringLiteral("gBasic"),
           QVector4D(c.lut ? float(c.lut->size) : 2.0f, c.saturation,
                     float(std::cos(radians)), float(std::sin(radians))));
  u.insert(QStringLiteral("gMix"),
           QVector4D(hueActive ? 1.0f : 0.0f, c.lightness,
                     c.hslActive ? 1.0f : 0.0f,
                     // The same 170 degree cutoff LumetriPreview::apply() uses:
                     // past it the key stops discriminating on hue at all.
                     c.hslHueWidth >= 170.0f ? 1.0f : 0.0f));
  u.insert(QStringLiteral("gShadow"),
           QVector4D(c.shadow[0], c.shadow[1], c.shadow[2], 0.0f));
  u.insert(QStringLiteral("gMidtone"),
           QVector4D(c.midtone[0], c.midtone[1], c.midtone[2], 0.0f));
  u.insert(QStringLiteral("gHighlight"),
           QVector4D(c.highlight[0], c.highlight[1], c.highlight[2], 0.0f));
  u.insert(QStringLiteral("gHslKey"),
           QVector4D(c.hslHueCenter, c.hslHueWidth, c.hslSatMin, c.hslSatMax));
  u.insert(QStringLiteral("gHslRange"),
           QVector4D(c.hslLumaMin, c.hslLumaMax, 0.0f, 0.0f));
  u.insert(QStringLiteral("gHslAdjust"),
           QVector4D(c.hslHue, c.hslSaturation, c.hslLuma, 0.0f));
  u.insert(QStringLiteral("gVignette"),
           QVector4D(c.vignetteAngle, c.vignetteAspect,
                     c.vignetteBackward ? 1.0f : 0.0f, 0.0f));

  // The per-pass switches QML builds the chain from. A pass that is off is not
  // instantiated at all, so an ungraded monitor costs no render target.
  u.insert(QStringLiteral("colorActive"),
           bool(c.lut || c.toneActive || c.curvesActive || c.balanceActive ||
                saturationActive || hueActive || lightnessActive ||
                c.hslActive));
  u.insert(QStringLiteral("sharpen"), double(c.sharpen));
  u.insert(QStringLiteral("denoise"), double(c.denoise));
  u.insert(QStringLiteral("blurSigma"), double(c.blurSigma));
  u.insert(QStringLiteral("vignetteActive"),
           std::abs(c.vignette) > 0.0001f);
  u.insert(QStringLiteral("identity"), c.identity);
  m_lumetriUniforms = u;
}

QString Backend::lumetriCurveUrl() const {
  QMutexLocker locker(&m_previewGradeMutex);
  if (m_previewGrade.identity)
    return {};
  // The generation is the cache key: the tables are rebuilt in place, so without
  // it QML would keep the texture it already decoded under the same URL.
  return QStringLiteral("image://lumetri-lut/curves?gen=%1")
      .arg(m_previewGradeGeneration);
}

QString Backend::lumetriCubeUrl() const {
  QMutexLocker locker(&m_previewGradeMutex);
  if (!m_previewGrade.lut)
    return {};
  return QStringLiteral("image://lumetri-lut/cube?gen=%1")
      .arg(m_previewGradeGeneration);
}

QImage Backend::lumetriCurveTexture() const {
  QMutexLocker locker(&m_previewGradeMutex);
  QImage image(256, 1, QImage::Format_RGBA8888);
  uchar *line = image.bits();
  for (int i = 0; i < 256; ++i) {
    line[i * 4 + 0] = m_previewGrade.curveR[i];
    line[i * 4 + 1] = m_previewGrade.curveG[i];
    line[i * 4 + 2] = m_previewGrade.curveB[i];
    line[i * 4 + 3] = m_previewGrade.tone[i];
  }
  return image;
}

QImage Backend::lumetriCubeTexture() const {
  QMutexLocker locker(&m_previewGradeMutex);
  const std::shared_ptr<const LumetriPreview::Lut3d> lut = m_previewGrade.lut;
  if (!lut || lut->size < 2)
    return {};
  const int n = lut->size;
  // One row per green step, the blue slices laid side by side along x. A 3D
  // sampler would be tidier, but ShaderEffect only binds 2D textures, so the
  // shader walks the strip itself.
  QImage image(n * n, n, QImage::Format_RGBA8888);
  const float *data = lut->data.constData();
  for (int b = 0; b < n; ++b) {
    for (int g = 0; g < n; ++g) {
      uchar *line = image.scanLine(g) + qsizetype(b) * n * 4;
      for (int r = 0; r < n; ++r) {
        const float *entry = data + (qsizetype(b) * n + g) * n * 3 + r * 3;
        for (int k = 0; k < 3; ++k)
          line[r * 4 + k] =
              uchar(qBound(0.0f, entry[k] * 255.0f + 0.5f, 255.0f));
        line[r * 4 + 3] = 255;
      }
    }
  }
  return image;
}

QImage Backend::previewFrameImage() const {
  QMutexLocker locker(&m_previewFrameMutex);
  return m_previewFrame;
}

QString Backend::previewError() const {
  const QString decoderError = m_previewDecoder.error();
  if (!decoderError.isEmpty())
    return decoderError;
  return m_scrubFrames.error();
}

bool Backend::startPreviewDecode(
    const QString &path, const QString &mediaKind, qint64 sourcePositionMs,
    qint64 durationMs, int sourceWidth, int sourceHeight, double frameRate,
    bool audioEnabled, double volume, const QString &audioPath) {
  if (m_pausingPreview)
    return false;
  const QString clean = normalizePath(path);
  const QString cleanAudio = normalizePath(audioPath);
  const QVariantMap clip = m_videoPreviewHelper.activeClipMap();
  // A retained workspace acquiring the same active session only changes its
  // audio level. It must not reopen the container or reset the playback clock.
  if ((m_previewDecoder.running() || m_previewDecoder.paused()) && m_playing &&
      (!m_previewDecoder.paused() || qAbs(sourcePositionMs - m_paintedSourceMs) <= 1) &&
      m_previewSessionSeekSerial == m_previewSeekSerial &&
      m_previewSessionPath == clean && m_previewSessionAudioPath == cleanAudio &&
      m_previewSessionClip.value("id") == clip.value("id") &&
      m_previewSessionClip.value("startMs") == clip.value("startMs") &&
      m_previewSessionClip.value("sourceInMs") == clip.value("sourceInMs") &&
      m_previewSessionClip.value("durationMs") == clip.value("durationMs") &&
      m_previewSessionClip.value("effects").toMap().value("speed", 100) ==
          clip.value("effects").toMap().value("speed", 100)) {
    setPreviewVolume(audioEnabled ? volume : 0);
    if (m_previewDecoder.paused()) {
      m_paintedWallMs = m_previewMonotonic.elapsed();
      m_previewDecoder.resume();
      CUTPRO_PLAYBACK_TRACE("session.resume", QStringLiteral("retained decoder at source %1 ms").arg(m_paintedSourceMs));
    }
    return true;
  }
  m_previewSessionClip = clip;
  m_previewSessionSeekSerial = m_previewSeekSerial;
  m_previewSessionPath = clean;
  m_previewSessionAudioPath = cleanAudio;
  m_previewAudioVolume = audioEnabled ? volume : 0;
  m_previewPlaybackClock.start(m_previewMonotonic.elapsed(), m_playheadMs);
  // A new session opens the container and seeks before a picture exists. An
  // unchanged resume takes the retained-session branch above; the trace names
  // callers that need a real restart.
  CUTPRO_PLAYBACK_TRACE("session.start",
                        QStringLiteral("%1 at source %2 ms, kind %3")
                            .arg(QFileInfo(path).fileName())
                            .arg(sourcePositionMs)
                            .arg(mediaKind));
  // Playback owns the monitor from here; a scrub still that is still decoding
  // would otherwise land on top of the first played frame.
  m_acceptScrubFrames = false;
  m_scrubFrames.cancel();
  m_minimumScrubRevision = m_scrubFrames.revision() + 1;
  // No picture belongs to this session yet. Leaving the previous session's frame
  // recorded would let the first UI tick anchor the clock on it.
  m_paintedSourceMs = -1;
  m_previousPaintedSourceMs = -1;
  m_paintedWallMs = 0;
  m_paintedFrameWidth = 0;
  return m_previewDecoder.start(normalizePath(path), mediaKind,
                                sourcePositionMs, durationMs, sourceWidth,
                                sourceHeight, frameRate, audioEnabled, volume,
                                normalizePath(audioPath),
                                clip.value("effects").toMap().value("speed", 100).toDouble() / 100);
}

void Backend::setPreviewVolume(double volume) {
  m_previewAudioVolume = volume;
  // The live half of mute. QML recomputes this whenever mutedTracks, the track's
  // enabled flag, the master volume or the clip's volume effect changes, so a mute
  // during playback is heard immediately instead of at the next seek - and, because
  // the stream is no longer torn down for a muted track, so is an un-mute.
  m_previewDecoder.setAudioVolume(volume);
}

QVariantList Backend::audioMeterLevels() {
  float left = 0.0f;
  float right = 0.0f;
  m_previewDecoder.audioPeakLevels(&left, &right);
  return {double(left), double(right)};
}

bool Backend::requestPreviewFrame(const QString &path,
                                  qint64 sourcePositionMs, int sourceWidth,
                                  int sourceHeight, bool exact) {
  CUTPRO_GUI_SCOPE("Backend::requestPreviewFrame");
  const QString clean = normalizePath(path);
  if (clean.isEmpty())
    return false;
  m_requestedPreviewClip = m_videoPreviewHelper.activeClipMap();
  m_requestedPreviewPath = clean;
  PreviewPlaybackClock::Sample sample;
  sample.clipStartMs = m_requestedPreviewClip.value("startMs").toLongLong();
  sample.sourceInMs = m_requestedPreviewClip.value("sourceInMs").toLongLong();
  sample.speed = m_requestedPreviewClip.value("effects").toMap().value("speed", 100).toDouble() / 100;
  m_requestedPreviewTimelineMs = PreviewPlaybackClock::timelinePosition(sample, sourcePositionMs);
  m_minimumScrubRevision = m_scrubFrames.revision() + 1;
  if (ScrubFrameService::available()) {
    // Playback and scrubbing cannot share the monitor: stop the streaming
    // decoder, then serve the position from the warm session. Only when it is
    // actually running - stop() tears down the WASAPI sink on this thread, and a
    // scrub asks for a frame every few milliseconds.
    if (m_previewDecoder.running() || m_previewDecoder.paused())
      m_previewDecoder.stop();
    m_acceptScrubFrames = true;
    return m_scrubFrames.request(clean, sourcePositionMs,
                                 previewFrameBound(sourceWidth, sourceHeight),
                                 exact);
  }
  m_acceptScrubFrames = false;
  return m_previewDecoder.requestFrame(clean, sourcePositionMs, sourceWidth,
                                       sourceHeight);
}

void Backend::prewarmPreviewSource(const QString &path, int sourceWidth,
                                   int sourceHeight) {
  CUTPRO_GUI_SCOPE("Backend::prewarmPreviewSource");
  const QString clean = normalizePath(path);
  if (clean.isEmpty() || !ScrubFrameService::available())
    return;
  m_scrubFrames.prewarm(clean, previewFrameBound(sourceWidth, sourceHeight));
}

void Backend::releasePreviewSource(const QString &path) {
  const QString clean = normalizePath(path);
  if (clean.isEmpty())
    return;
  m_scrubFrames.forget(clean);
  TimelineThumbnailService::instance().forget(clean);
  AudioPeakWindowService::instance().forget(clean);
}

QString Backend::timelineTileToken(const QString &path) {
  const QString clean = normalizePath(path);
  if (clean.isEmpty())
    return {};
  return TimelineThumbnailService::instance().tokenFor(clean);
}

bool Backend::timelineTilesAvailable() const {
  return TimelineThumbnailService::available();
}

// The token comes from the same registry the tiles use, so a clip's thumbnail
// URL and its waveform URL carry one handle for one source.
QString Backend::waveformWindowToken(const QString &path) {
  const QString clean = normalizePath(path);
  if (clean.isEmpty())
    return {};
  return AudioPeakWindowService::instance().tokenFor(clean);
}

bool Backend::waveformWindowsAvailable() const {
  return AudioPeakWindowService::available();
}

namespace {
// Signal-to-signal, so the property's NOTIFY fires whenever the prefetcher
// reports progress. Qt::UniqueConnection makes it idempotent, which is what lets
// this be wired from whichever request lands first instead of from the
// constructor - the prefetcher is not built until the timeline actually wants
// something.
void wirePrefetcher(Backend *backend) {
  QObject::connect(&TimelinePreviewPrefetcher::instance(),
                   &TimelinePreviewPrefetcher::progressed, backend,
                   &Backend::timelinePreviewRevisionChanged,
                   Qt::UniqueConnection);
}

QVector<qint64> toPositions(const QVariantList &values) {
  QVector<qint64> positions;
  positions.reserve(values.size());
  for (const QVariant &value : values) {
    bool ok = false;
    // QML hands whole numbers over as doubles, so this is a conversion rather
    // than a cast; a value that is not a number at all is skipped instead of
    // becoming position zero.
    const qint64 position = qint64(value.toDouble(&ok));
    if (ok)
      positions.append(qMax<qint64>(0, position));
  }
  return positions;
}
} // namespace

int Backend::timelinePreviewRevision() const {
  return TimelinePreviewPrefetcher::instance().revision();
}

void Backend::requestTimelineTiles(const QString &requesterId,
                                   const QString &token,
                                   const QVariantList &bucketsMs) {
  if (requesterId.isEmpty() || token.isEmpty() || bucketsMs.isEmpty())
    return;
  const QString path =
      TimelineThumbnailService::instance().pathForToken(token);
  if (path.isEmpty())
    return;
  wirePrefetcher(this);
  TimelinePreviewPrefetcher::instance().requestTiles(requesterId, path,
                                                    toPositions(bucketsMs));
}

void Backend::requestWaveformWindows(const QString &requesterId,
                                     const QString &token,
                                     const QVariantList &startsMs,
                                     qint64 spanMs, int columns) {
  if (requesterId.isEmpty() || token.isEmpty() || startsMs.isEmpty() ||
      spanMs <= 0)
    return;
  const QString path = AudioPeakWindowService::instance().pathForToken(token);
  if (path.isEmpty())
    return;
  wirePrefetcher(this);
  TimelinePreviewPrefetcher::instance().requestWindows(
      requesterId, path, toPositions(startsMs), spanMs, columns);
}

void Backend::cancelTimelinePreviewRequest(const QString &requesterId) {
  if (requesterId.isEmpty())
    return;
  TimelinePreviewPrefetcher::instance().cancel(requesterId);
}

bool Backend::timelineTileReady(const QString &token, qint64 bucketMs) const {
  if (token.isEmpty())
    return false;
  TimelineThumbnailService &service = TimelineThumbnailService::instance();
  const QString path = service.pathForToken(token);
  if (path.isEmpty())
    return false;
  return service.hasTile(path, bucketMs);
}

bool Backend::waveformWindowReady(const QString &token, qint64 startMs,
                                  qint64 spanMs, int columns) const {
  if (token.isEmpty() || spanMs <= 0)
    return false;
  AudioPeakWindowService &service = AudioPeakWindowService::instance();
  const QString path = service.pathForToken(token);
  if (path.isEmpty())
    return false;
  return service.hasWindow(path, startMs, spanMs, columns);
}

// Called by the monitor whenever its picture area changes size. Decode size
// follows the panel: a source larger than the panel is decoded down to it (those
// pixels could not be seen), and a source smaller than the panel is decoded at
// its own resolution rather than being enlarged by the decoder.
void Backend::setPreviewSurfaceSize(int width, int height) {
  PreviewDecodePolicy::setSurfaceSize(QSize(width, height));
}

// Held for the whole gesture: QML raises this when a drag starts and lowers it
// when the drop lands. Background decode classes are refused while it is up, and
// the filmstrip stops asking for tiles at all.
void Backend::beginTimelineInteraction() {
  DecodeWorkGovernor::instance().beginInteraction();
  // Single-shot safety net. A native Windows drag cancelled with Escape delivers
  // neither a drop nor an exit, and a hold that never lowers would leave the
  // timeline without thumbnails until the next launch.
  if (!m_timelineInteractionTimer.isSingleShot()) {
    m_timelineInteractionTimer.setSingleShot(true);
    m_timelineInteractionTimer.setInterval(kTimelineInteractionLapseMs);
    connect(&m_timelineInteractionTimer, &QTimer::timeout, this, [this]() {
      if (m_timelineInteractions <= 0)
        return;
      // One release per hold this object took, so the governor's own count comes
      // back to zero rather than staying positive for the rest of the session.
      const int held = m_timelineInteractions;
      m_timelineInteractions = 0;
      for (int i = 0; i < held; ++i)
        DecodeWorkGovernor::instance().endInteraction();
      emit timelineInteractionChanged();
    });
  }
  m_timelineInteractionTimer.start();
  if (++m_timelineInteractions == 1)
    emit timelineInteractionChanged();
}

// Called from move handlers. Keeps the hold alive without pairing another begin,
// so a gesture that never reports its end still recovers by itself.
void Backend::touchTimelineInteraction() {
  DecodeWorkGovernor::instance().touchInteraction();
  if (m_timelineInteractions > 0)
    m_timelineInteractionTimer.start();
}

void Backend::endTimelineInteraction() {
  // Unpaired ends are normal: QML lowers the hold from the drop handler and from
  // the exit handler, and a drop fires both. Only a hold this object actually
  // took is released, or the governor's count would go negative and it would stop
  // holding anything back at all.
  if (m_timelineInteractions <= 0)
    return;
  DecodeWorkGovernor::instance().endInteraction();
  if (--m_timelineInteractions == 0) {
    m_timelineInteractionTimer.stop();
    emit timelineInteractionChanged();
  }
}

QVariantMap Backend::previewDecodeStatistics() const {
  QVariantMap stats = m_scrubFrames.statistics();
  stats[QStringLiteral("available")] = ScrubFrameService::available();
  stats[QStringLiteral("playbackDecoding")] = m_previewDecoder.running();
  const QSize surface = PreviewDecodePolicy::surfaceSize();
  stats[QStringLiteral("previewSurface")] =
      QStringLiteral("%1x%2").arg(surface.width()).arg(surface.height());
  const QVariantMap tiles = TimelineThumbnailService::instance().statistics();
  for (auto it = tiles.cbegin(); it != tiles.cend(); ++it)
    stats.insert(it.key(), it.value());
  const QVariantMap waves = AudioPeakWindowService::instance().statistics();
  for (auto it = waves.cbegin(); it != waves.cend(); ++it)
    stats.insert(it.key(), it.value());
  // The three schedulers that decide whether a decode happens at all. Without
  // them a stall looks identical to a slow file.
  const QVariantMap governor = DecodeWorkGovernor::instance().statistics();
  for (auto it = governor.cbegin(); it != governor.cend(); ++it)
    stats.insert(it.key(), it.value());
  const QVariantMap failures = PreviewFailureRegistry::instance().statistics();
  for (auto it = failures.cbegin(); it != failures.cend(); ++it)
    stats.insert(it.key(), it.value());
  const QVariantMap cost = DecodeCostModel::instance().statistics();
  for (auto it = cost.cbegin(); it != cost.cend(); ++it)
    stats.insert(it.key(), it.value());
  // What the timeline has asked for and how much of it is still owed.
  // prefetchPending far above zero while prefetchDecoded is flat means the worker
  // is parked - a gesture that never ended, or a source it cannot read.
  const QVariantMap prefetch = TimelinePreviewPrefetcher::instance().statistics();
  for (auto it = prefetch.cbegin(); it != prefetch.cend(); ++it)
    stats.insert(it.key(), it.value());
  // What the GUI thread actually experienced, as opposed to what the schedulers
  // intended. guiWorstStallScope is the one field worth reading first after a
  // freeze: it names the call the window was inside when it stopped answering.
  const QVariantMap gui = GuiThreadWatchdog::instance().statistics();
  for (auto it = gui.cbegin(); it != gui.cend(); ++it)
    stats.insert(it.key(), it.value());
  const QVariantMap dispatch = GuiDispatch::statistics();
  for (auto it = dispatch.cbegin(); it != dispatch.cend(); ++it)
    stats.insert(it.key(), it.value());
  // What the startup warm-up managed to pay for before QML asked. An empty list
  // here next to a multi-second startup stall means the warm-up lost the race and
  // the GUI thread paid it anyway.
  stats[QStringLiteral("startupWarmup")] = StartupWarmup::report();
  // Nested rather than merged: the coalescer's field names are generic on
  // purpose, so more of them can be added for other signals without colliding.
  stats[QStringLiteral("colorSettingsNotify")] =
      m_colorSettingsNotify.statistics();
  // A high collapsed:emissions ratio here is the drop-freeze fix working: one
  // panel rebuild per event-loop turn instead of one per nested binding level.
  stats[QStringLiteral("selectionDetailNotify")] =
      m_selectionDetailNotify.statistics();
  // Every guarded QML model, largest first, plus anything that hit its cap. This
  // is the answer to "which Repeater exploded": gdb showed the freeze inside
  // QQuickRepeater::clear() but not which Repeater, and one number per call site
  // is what turns that into a file name.
  const QVariantMap models = ModelGuard::instance().statistics();
  for (auto it = models.cbegin(); it != models.cend(); ++it)
    stats.insert(it.key(), it.value());
  // How many clips the timeline holds against how many rows the view was asked
  // for. timelineClips far above timelineClipRows is the collapse doing its job;
  // the two being equal on a subtitle track means the viewport was set without a
  // scale and the view is carrying one delegate per segment again.
  const QVariantMap clipRows = m_timelineClipModel.statistics();
  for (auto it = clipRows.cbegin(); it != clipRows.cend(); ++it)
    stats.insert(it.key(), it.value());
  // Whether the out-of-process reporter is actually watching. A reporter that
  // failed to start is worth knowing about before a crash, not after.
  const QVariantMap reporter = CrashReporterHost::statistics();
  for (auto it = reporter.cbegin(); it != reporter.cend(); ++it)
    stats.insert(it.key(), it.value());
  // ItemTreeCensus is deliberately absent. It walks QQuickItems, so it lives in
  // cutpro_scene where Qt6::Quick is allowed; including it here would give
  // cutpro_backend a Quick dependency and undo the reason for the split. QML
  // reads it from the Diagnostics singleton instead, which merges both.
  return stats;
}

void Backend::stopPreviewDecode() {
  // Logged whether or not a session is running: a teardown that arrives while
  // nothing is playing is harmless, and one that arrives 800 ms into playback is
  // the bug. Which QML handler called it is the answer either way.
  if (PlaybackTrace::enabled())
    PlaybackTrace::instance().record(
        "session.stop",
        QStringLiteral("decoder %1, picture at %2 ms, published %3 ms")
            .arg(m_previewDecoder.running() ? QStringLiteral("running")
                                            : QStringLiteral("idle"))
            .arg(m_paintedSourceMs)
            .arg(m_previewDecoder.presentedSourceMs()));

  m_acceptScrubFrames = false;
  m_scrubFrames.cancel();
  m_minimumScrubRevision = m_scrubFrames.revision() + 1;
  if (m_pausingPreview && m_paintedSourceMs >= 0 && m_previewDecoder.pause()) {
    // A stalled GUI may not have accepted the decoder's newest picture yet.
    // Restart that exceptional case from the accepted frame instead of skipping
    // the pictures that were decoded while its notifications were queued.
    if (m_previewDecoder.presentedSourceMs() == m_paintedSourceMs)
      return;
  }
  m_previewDecoder.stop();
}

bool Backend::previewFrameMatchesPlayhead() const {
  const QVariantMap &clip = m_videoPreviewHelper.activeClipMap();
  const QVariantMap &media = m_videoPreviewHelper.activeMediaMap();
  return !previewFrameImage().isNull() &&
      m_acceptedPreviewClip.value("id") == clip.value("id") &&
      m_acceptedPreviewClip.value("sourceInMs") == clip.value("sourceInMs") &&
      m_acceptedPreviewClip.value("startMs") == clip.value("startMs") &&
      m_acceptedPreviewClip.value("effects").toMap().value("speed", 100) ==
          clip.value("effects").toMap().value("speed", 100) &&
      m_acceptedPreviewPath == normalizePath(media.value("path").toString()) &&
      qAbs(m_acceptedPreviewTimelineMs - m_playheadMs) <= 1;
}

double Backend::previewDisplayPlayheadMs() {
  if (!m_playing)
    return m_playheadMs;
  return qBound(0.0, m_previewPlaybackClock.displayPosition(
      m_previewMonotonic.nsecsElapsed() / 1000000.0, m_playheadMs,
      m_videoPreviewHelper.uiTickInterval()), double(durationMs()));
}

void Backend::advancePreviewPlayhead() {
  if (!m_playing)
    return;
  const QVariantMap &clip = m_videoPreviewHelper.activeClipMap();
  const QString kind = m_videoPreviewHelper.activeMediaMap().value("kind").toString();
  PreviewPlaybackClock::Sample sample;
  sample.clipStartMs = clip.value("startMs").toLongLong();
  sample.sourceInMs = clip.value("sourceInMs").toLongLong();
  sample.durationMs = clip.value("durationMs").toLongLong();
  sample.speed = clip.value("effects").toMap().value("speed", 100).toDouble() / 100;
  sample.video = kind == QStringLiteral("video");
  sample.timedSource = sample.video || kind == QStringLiteral("audio");
  sample.decoding = m_previewDecoder.running();
  sample.failed = !previewError().isEmpty();
  sample.shownMs = m_paintedSourceMs;
  sample.audioMs = m_previewDecoder.audioClockSourceMs();
  sample.frameAgeMs = previewPresentedAgeMs();
  const qint64 now = m_previewMonotonic.elapsed();
  const qint64 next = m_previewPlaybackClock.next(now, m_playheadMs, sample);
  m_advancingPreview = true;
  setPlayheadMs(qMin(durationMs(), next));
  m_advancingPreview = false;
  if (m_playheadMs >= durationMs()) {
    setPlaying(false);
    if (durationMs() > 0 && m_appSettings.value("loopPlayback", false).toBool()) {
      setPlayheadMs(0);
      const quint64 serial = m_previewTransportSerial;
      QTimer::singleShot(0, this, [this, serial]() {
        if (!m_playing && m_playheadMs == 0 && serial == m_previewTransportSerial)
          setPlaying(true);
      });
    }
  }
}

qint64 Backend::previewPresentedSourceMs() const {
  // Deliberately not cleared by stopPreviewDecode(): the pause path stops the
  // session and anchors onto the retained image's timestamp.
  // startPreviewDecode() clears it for the next session.
  //
  // This is the frame the GUI thread was handed, not the newest one the decode
  // thread published - see m_paintedSourceMs. The two differ by exactly the
  // queued call between them, which is the frame the pause used to jump onto.
  return m_paintedSourceMs;
}

qint64 Backend::previewAudioClockSourceMs() const {
  // A direct, lock-free read of the decoder's audio master clock (atomics plus
  // one mutex-guarded flag), safe to call from the GUI thread every UI tick.
  // Unlike previewPresentedSourceMs() above - the GUI thread's snapshot of the
  // frame it was handed - this is the device clock the frames themselves are
  // paced against, so a playhead reading it stays locked to the picture.
  return m_previewDecoder.audioClockSourceMs();
}

int Backend::previewPresentedFrameWidth() const { return m_paintedFrameWidth; }

qint64 Backend::previewPresentedAgeMs() const {
  if (m_paintedSourceMs < 0 || m_paintedWallMs <= 0)
    return -1;
  const qint64 age = m_previewMonotonic.elapsed() - m_paintedWallMs;
  return age < 0 ? 0 : age;
}

qint64 Backend::previewPresentedSourceMsAtOrBefore(qint64 sourceMs) const {
  if (m_paintedSourceMs < 0)
    return -1;
  if (m_paintedSourceMs <= sourceMs)
    return m_paintedSourceMs;
  // The newest picture is ahead of the clock, so it is the one that was still
  // waiting to be drawn. Fall back to its predecessor; if there is none, the
  // session has produced a single frame and that frame is what is on screen.
  return m_previousPaintedSourceMs >= 0 ? m_previousPaintedSourceMs
                                        : m_paintedSourceMs;
}

void Backend::tracePlaybackDrift(qint64 presentedTimelineMs) const {
  if (!PlaybackTrace::enabled() || presentedTimelineMs < 0)
    return;
  PlaybackTrace::instance().recordDrift(m_playheadMs, presentedTimelineMs);
}

void Backend::markGuiScope(const QString &label) const {
  GuiThreadWatchdog::markTurn(label);
}

bool Backend::applyAppSettings(const QVariantMap &settings) {
  const QVariantMap clean = AppSettings::normalized(settings);
  QString error;
  if (!AppSettings::save(clean, &error)) {
    setError(error);
    return false;
  }
  if (clean == m_appSettings)
    return true;
  m_appSettings = clean;
  configureAutoSave();
  emit appSettingsChanged();
  return true;
}

bool Backend::resetAppSettings() {
  return applyAppSettings(AppSettings::defaults());
}

void Backend::removeGeneratedSpeechFromProject() {
  QSet<QString> mediaIds;
  for (const QVariant &value : m_media) {
    const QVariantMap media = value.toMap();
    if (media.value(QStringLiteral("generatedBy")).toString() ==
        QStringLiteral("text_to_speech"))
      mediaIds.insert(media.value(QStringLiteral("id")).toString());
  }
  // Clips are matched on their own marker as well as on the media, so a project
  // whose generated media was already removed by hand does not keep orphans.
  auto isGeneratedSpeech = [&mediaIds](const QVariantMap &clip) {
    return clip.value(QStringLiteral("generatedBy")).toString() ==
               QStringLiteral("text_to_speech") ||
           mediaIds.contains(clip.value(QStringLiteral("mediaId")).toString());
  };
  bool hasClips = false;
  for (const QVariant &value : m_clips) {
    if (isGeneratedSpeech(value.toMap())) {
      hasClips = true;
      break;
    }
  }
  if (mediaIds.isEmpty() && !hasClips)
    return;

  // Undoable, and taken before the first removal so the state it captures is the
  // timeline the user is looking at.
  rememberState();
  bool selectionRemoved = false;
  for (int index = m_clips.size() - 1; index >= 0; --index) {
    const QVariantMap clip = m_clips.at(index).toMap();
    if (!isGeneratedSpeech(clip))
      continue;
    if (!m_selectedClipId.isEmpty() &&
        clip.value(QStringLiteral("id")).toString() == m_selectedClipId)
      selectionRemoved = true;
    m_clips.removeAt(index);
  }
  for (int index = m_media.size() - 1; index >= 0; --index) {
    if (mediaIds.contains(
            m_media.at(index).toMap().value(QStringLiteral("id")).toString()))
      m_media.removeAt(index);
  }
  if (selectionRemoved)
    m_selectedClipId.clear();
  pruneEmptyTracks();
  markDirty();
  emit mediaChanged();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  if (selectionRemoved)
    emit selectionChanged();
}

bool Backend::clearMediaCache() {
  if (m_mediaCacheClearing)
    return false;
  // Refused rather than raced. The worker writes into the folder this is about to
  // delete, and the import behind it is holding paths inside it.
  if (m_ttsImportActive || m_textToSpeechEngine.inProgress()) {
    setError(QStringLiteral(
        "Generated subtitle voices are still being created. Wait for that to "
        "finish before clearing the cache."));
    return false;
  }

  // The project's references go first, on this thread: the voice files are about to
  // stop existing and the clips that point at them have to go with them.
  removeGeneratedSpeechFromProject();

  m_mediaCacheClearing = true;
  m_mediaCacheMeasured = false;
  m_mediaCacheSizeText.clear();
  m_generatedSpeechFiles = 0;
  emit mediaCacheChanged();
  // removeRecursively() over 24k files takes long enough to freeze the window, and
  // this used to run on the GUI thread.
  QtConcurrent::run([]() {
    QString error;
    const bool ok = AppSettings::clearPreviewCache(&error);
    return std::pair<bool, QString>(ok, error);
  }).then(this, [this](std::pair<bool, QString> result) {
    m_mediaCacheClearing = false;
    if (!result.first)
      setError(result.second);
    emit mediaCacheChanged();
    // Re-measured rather than assumed to be zero: a file another process still has
    // open survives the delete, and the page should say so.
    measureMediaCache();
  });
  return true;
}

bool Backend::setMediaColorSetting(const QString &mediaId, const QString &key,
                                   const QVariant &value) {
  const int index = mediaIndex(mediaId);
  if (index < 0)
    return false;
  QVariantMap media = m_media.at(index).toMap();
  QVariantMap settings = media.value("color").toMap();
  QVariant normalized = value;
  if (key == "inputLut") {
    const QString path = value.toString().trimmed();
    if (path.isEmpty() || path.compare("None", Qt::CaseInsensitive) == 0)
      normalized = QString();
    else {
      const QString lutPath = normalizePath(path);
      if (!QFileInfo::exists(lutPath)) {
        setError(QStringLiteral("The selected LUT file does not exist."));
        return false;
      }
      normalized = lutPath;
    }
  }
  if (!ColorSettings::setMediaValue(&settings, key, normalized))
    return false;
  rememberState();
  media["color"] = settings;
  m_media[index] = media;
  markDirty();
  emit mediaChanged();
  m_colorSettingsNotify.schedule();
  return true;
}
