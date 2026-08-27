#include "app/core_app/backend.h"

#include "app/settings/app_settings.h"
#include "app/lumetri/color_settings.h"
#include "app/preview/audio_peak_window_service.h"
#include "app/preview/decode_cost_model.h"
#include "app/preview/decode_work_governor.h"
#include "app/preview/gui_dispatch.h"
#include "app/preview/gui_thread_watchdog.h"
#include "app/preview/preview_decode_policy.h"
#include "app/preview/preview_failure_registry.h"
#include "app/preview/startup_warmup.h"
#include "app/preview/timeline_thumbnail_service.h"

#include <QDir>
#include <QFileInfo>
#include <QSize>

#include <cmath>

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
} // namespace

QVariantMap Backend::defaultAppSettings() const {
  return AppSettings::defaults();
}

QString Backend::mediaCachePath() const { return AppSettings::cacheRoot(); }

QString Backend::mediaCacheSize() const {
  return formatBytes(AppSettings::cacheSizeBytes());
}

QStringList Backend::downloadedWhisperModels() const {
  QString cacheRoot = qEnvironmentVariable("XDG_CACHE_HOME").trimmed();
  if (cacheRoot.isEmpty())
    cacheRoot = QDir(QDir::homePath()).filePath(QStringLiteral(".cache"));
  const QDir whisperCache(QDir(cacheRoot).filePath(QStringLiteral("whisper")));
  static const QStringList supportedModels{
      QStringLiteral("tiny"), QStringLiteral("base"),
      QStringLiteral("small"), QStringLiteral("medium"),
      QStringLiteral("large-v3"), QStringLiteral("turbo")};
  QStringList downloaded;
  for (const QString &model : supportedModels) {
    const QFileInfo file(whisperCache.filePath(model + QStringLiteral(".pt")));
    // A real Whisper checkpoint is much larger than this. The size guard keeps
    // an interrupted/empty download marked with the cloud icon.
    if (file.isFile() && file.size() > 1024 * 1024)
      downloaded.append(model);
  }
  return downloaded;
}

QString Backend::previewFrameUrl() const {
  if (previewFrameImage().isNull())
    return {};
  return QStringLiteral("image://ffmpeg-preview/frame?revision=%1")
      .arg(m_previewFrameRevision);
}

QImage Backend::previewFrameImage() const {
  // The scrub service and the playback decoder both produce frames. Whichever
  // published last owns the monitor, so a still from an abandoned drag cannot
  // overwrite the frame playback just produced.
  if (m_previewFrameFromScrub) {
    const QImage scrubbed = m_scrubFrames.frame();
    if (!scrubbed.isNull())
      return scrubbed;
  }
  const QImage played = m_previewDecoder.frame();
  if (!played.isNull())
    return played;
  return m_scrubFrames.frame();
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
  // Playback owns the monitor from here; a scrub still that is still decoding
  // would otherwise land on top of the first played frame.
  m_scrubFrames.cancel();
  m_previewFrameFromScrub = false;
  return m_previewDecoder.start(normalizePath(path), mediaKind,
                                sourcePositionMs, durationMs, sourceWidth,
                                sourceHeight, frameRate, audioEnabled, volume,
                                normalizePath(audioPath));
}

bool Backend::requestPreviewFrame(const QString &path,
                                  qint64 sourcePositionMs, int sourceWidth,
                                  int sourceHeight) {
  CUTPRO_GUI_SCOPE("Backend::requestPreviewFrame");
  const QString clean = normalizePath(path);
  if (clean.isEmpty())
    return false;
  if (ScrubFrameService::available()) {
    // Playback and scrubbing cannot share the monitor: stop the streaming
    // decoder, then serve the position from the warm session.
    m_previewDecoder.stop();
    m_previewFrameFromScrub = true;
    return m_scrubFrames.request(clean, sourcePositionMs,
                                 previewFrameBound(sourceWidth, sourceHeight));
  }
  m_previewFrameFromScrub = false;
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
  return stats;
}

void Backend::stopPreviewDecode() {
  m_scrubFrames.cancel();
  m_previewDecoder.stop();
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

bool Backend::clearMediaCache() {
  QString error;
  if (!AppSettings::clearPreviewCache(&error)) {
    setError(error);
    return false;
  }
  emit mediaCacheChanged();
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
