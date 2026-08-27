#pragma once

#include "app/caption_style.h"
#include "app/effects/clip_effects.h"
#include "app/media/media_import_queue.h"
#include "app/preview/effect_preview_generator.h"
#include "app/preview/ffmpeg_preview_decoder.h"
#include "app/preview/scrub_frame_service.h"
#include "app/timeline/keyframe_engine.h"
#include "app/subtitles/text_to_speech_engine.h"
#include "app/timeline/timeline_editor.h"
#include "app/timeline/timeline_placement_job.h"
#include "app/timeline/timeline_clip_model.h"
#include "app/preview/video_preview_helper.h"
#include "app/subtitles/transcript_translator.h"
#include "app/subtitles/transcription_plan.h"
#include "app/core_app/project_database.h"
#include "app/core_app/signal_coalescer.h"

#include <QObject>
#include <QImage>
#include <QHash>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QFutureWatcher>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Native Qt application service exposed to QML. Project data uses Qt value
// types so it remains observable and can be serialized without another bridge.
class Backend : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString coreVersion READ coreVersion CONSTANT)
  Q_PROPERTY(QString projectId READ projectId NOTIFY projectChanged)
  Q_PROPERTY(QString projectName READ projectName WRITE setProjectName NOTIFY
                 projectChanged)
  Q_PROPERTY(QString projectLocation READ projectLocation NOTIFY projectChanged)
  Q_PROPERTY(QString projectFile READ projectFile NOTIFY projectChanged)
  Q_PROPERTY(QString sequenceId READ sequenceId NOTIFY sequenceChanged)
  Q_PROPERTY(QString sequenceName READ sequenceName WRITE setSequenceName NOTIFY
                 sequenceChanged)
  Q_PROPERTY(QString activeWorkspace READ activeWorkspace WRITE
                 setActiveWorkspace NOTIFY activeWorkspaceChanged)
  Q_PROPERTY(QString layoutPreset READ layoutPreset WRITE setLayoutPreset NOTIFY
                 layoutPresetChanged)
  Q_PROPERTY(QString captionFontFamily READ captionFontFamily WRITE
                 setCaptionFontFamily NOTIFY captionStyleChanged)
  Q_PROPERTY(QStringList downloadedCaptionFonts READ downloadedCaptionFonts
                 NOTIFY captionFontsChanged)
  Q_PROPERTY(int captionFontSize READ captionFontSize WRITE setCaptionFontSize
                 NOTIFY captionStyleChanged)
  Q_PROPERTY(QString captionTextColor READ captionTextColor WRITE
                 setCaptionTextColor NOTIFY captionStyleChanged)
  Q_PROPERTY(bool captionBold READ captionBold WRITE setCaptionBold NOTIFY
                 captionStyleChanged)
  Q_PROPERTY(bool captionItalic READ captionItalic WRITE setCaptionItalic NOTIFY
                 captionStyleChanged)
  Q_PROPERTY(bool captionBackgroundVisible READ captionBackgroundVisible WRITE
                 setCaptionBackgroundVisible NOTIFY captionStyleChanged)
  Q_PROPERTY(QString captionBackgroundColor READ captionBackgroundColor WRITE
                 setCaptionBackgroundColor NOTIFY captionStyleChanged)
  Q_PROPERTY(QString captionPosition READ captionPosition WRITE
                 setCaptionPosition NOTIFY captionStyleChanged)
  Q_PROPERTY(QString captionAlignment READ captionAlignment WRITE
                 setCaptionAlignment NOTIFY captionStyleChanged)
  Q_PROPERTY(
      double captionPositionX READ captionPositionX NOTIFY captionStyleChanged)
  Q_PROPERTY(
      double captionPositionY READ captionPositionY NOTIFY captionStyleChanged)
  Q_PROPERTY(bool captionBlurEnabled READ captionBlurEnabled WRITE
                 setCaptionBlurEnabled NOTIFY captionStyleChanged)
  Q_PROPERTY(bool captionBlurTrackingEnabled READ captionBlurTrackingEnabled
                 WRITE setCaptionBlurTrackingEnabled NOTIFY captionStyleChanged)
  Q_PROPERTY(double captionBlurRegionX READ captionBlurRegionX NOTIFY
                 captionStyleChanged)
  Q_PROPERTY(double captionBlurRegionY READ captionBlurRegionY NOTIFY
                 captionStyleChanged)
  Q_PROPERTY(double captionBlurRegionWidth READ captionBlurRegionWidth NOTIFY
                 captionStyleChanged)
  Q_PROPERTY(double captionBlurRegionHeight READ captionBlurRegionHeight NOTIFY
                 captionStyleChanged)
  Q_PROPERTY(int captionBlurStrength READ captionBlurStrength WRITE
                 setCaptionBlurStrength NOTIFY captionStyleChanged)
  Q_PROPERTY(int captionBlurPadding READ captionBlurPadding WRITE
                 setCaptionBlurPadding NOTIFY captionStyleChanged)
  Q_PROPERTY(QVariantList media READ media NOTIFY mediaChanged)
  Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
  Q_PROPERTY(TimelineClipModel *timelineClipModel READ timelineClipModel CONSTANT)
  Q_PROPERTY(bool hasSubtitleClips READ hasSubtitleClips NOTIFY clipsChanged)
  Q_PROPERTY(int videoTrackCount READ videoTrackCount NOTIFY tracksChanged)
  Q_PROPERTY(int audioTrackCount READ audioTrackCount NOTIFY tracksChanged)
  Q_PROPERTY(QStringList mutedTracks READ mutedTracks NOTIFY tracksChanged)
  Q_PROPERTY(QVariantList trackStates READ trackStates NOTIFY tracksChanged)
  Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)
  Q_PROPERTY(bool snappingEnabled READ snappingEnabled WRITE setSnappingEnabled
                 NOTIFY snappingChanged)
  Q_PROPERTY(int mediaCount READ mediaCount NOTIFY mediaChanged)
  Q_PROPERTY(bool mediaImportInProgress READ mediaImportInProgress NOTIFY mediaImportChanged)
  Q_PROPERTY(int mediaImportProgress READ mediaImportProgress NOTIFY mediaImportChanged)
  Q_PROPERTY(bool timelinePlacementInProgress READ timelinePlacementInProgress
                 NOTIFY timelinePlacementChanged)
  Q_PROPERTY(double timelinePlacementProgress READ timelinePlacementProgress
                 NOTIFY timelinePlacementChanged)
  Q_PROPERTY(QString timelinePlacementStatus READ timelinePlacementStatus
                 NOTIFY timelinePlacementChanged)
  Q_PROPERTY(bool hasSequence READ hasSequence NOTIFY sequenceChanged)
  Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY mediaChanged)
  Q_PROPERTY(bool canExport READ canExport NOTIFY timelineChanged)
  Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY timelineChanged)
  Q_PROPERTY(qint64 playheadMs READ playheadMs WRITE setPlayheadMs NOTIFY
                 playheadChanged)
  Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  Q_PROPERTY(
      bool exportInProgress READ exportInProgress NOTIFY exportStateChanged)
  Q_PROPERTY(
      double exportProgress READ exportProgress NOTIFY exportStateChanged)
  Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportStateChanged)
  Q_PROPERTY(QVariantList transcript READ transcript NOTIFY transcriptChanged)
  Q_PROPERTY(bool transcriptionInProgress READ transcriptionInProgress NOTIFY
                 transcriptChanged)
  Q_PROPERTY(QString transcriptionStatus READ transcriptionStatus NOTIFY
                 transcriptChanged)
  Q_PROPERTY(double transcriptionProgress READ transcriptionProgress NOTIFY
                 transcriptChanged)
  Q_PROPERTY(QString transcriptLanguage READ transcriptLanguage NOTIFY
                 transcriptChanged)
  Q_PROPERTY(QStringList transcribedMediaIds READ transcribedMediaIds NOTIFY
                 transcriptChanged)
  Q_PROPERTY(QStringList downloadedWhisperModels READ downloadedWhisperModels
                 NOTIFY whisperModelsChanged)
  Q_PROPERTY(bool translationInProgress READ translationInProgress NOTIFY
                 transcriptChanged)
  Q_PROPERTY(
      QString translationStatus READ translationStatus NOTIFY transcriptChanged)
  Q_PROPERTY(bool translationTestInProgress READ translationTestInProgress
                 NOTIFY transcriptChanged)
  Q_PROPERTY(bool demucsInProgress READ demucsInProgress NOTIFY demucsChanged)
  Q_PROPERTY(double demucsProgress READ demucsProgress NOTIFY demucsChanged)
  Q_PROPERTY(QString demucsStatus READ demucsStatus NOTIFY demucsChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
  Q_PROPERTY(
      QVariantMap colorSettings READ colorSettings NOTIFY colorSettingsChanged)
  Q_PROPERTY(QVariantMap activeColorClip READ activeColorClip NOTIFY
                 colorSettingsChanged)
  Q_PROPERTY(QVariantMap activeColorMedia READ activeColorMedia NOTIFY
                 colorSettingsChanged)
  Q_PROPERTY(QString selectedClipId READ selectedClipId WRITE setSelectedClipId
                 NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap selectedClip READ selectedClip NOTIFY
                 selectionDetailChanged)
  Q_PROPERTY(QVariantList effectDefinitions READ effectDefinitions CONSTANT)
  Q_PROPERTY(KeyframeEngine *keyframeEngine READ keyframeEngine CONSTANT)
  Q_PROPERTY(TextToSpeechEngine *textToSpeechEngine READ textToSpeechEngine
                 CONSTANT)
  Q_PROPERTY(VideoPreviewHelper *videoPreviewHelper READ videoPreviewHelper
                 CONSTANT)
  Q_PROPERTY(QString customBlurEditClipId READ customBlurEditClipId NOTIFY
                 customBlurEditChanged)
  Q_PROPERTY(QString customBlurEditInstanceId READ customBlurEditInstanceId
                 NOTIFY customBlurEditChanged)
  Q_PROPERTY(QVariantMap appSettings READ appSettings NOTIFY appSettingsChanged)
  Q_PROPERTY(QString mediaCachePath READ mediaCachePath CONSTANT)
  Q_PROPERTY(QString mediaCacheSize READ mediaCacheSize NOTIFY mediaCacheChanged)
  Q_PROPERTY(QString previewFrameUrl READ previewFrameUrl NOTIFY
                 previewFrameChanged)
  Q_PROPERTY(bool previewDecoding READ previewDecoding NOTIFY
                 previewStateChanged)
  Q_PROPERTY(QString previewError READ previewError NOTIFY previewErrorChanged)
  // True from the moment a timeline drag, trim or drop gesture starts until it
  // finishes. The filmstrip binds its cell count to this: during a gesture no
  // thumbnail is even requested, so the window keeps repainting instead of going
  // "Not Responding" while a long source is decoded behind it.
  Q_PROPERTY(bool timelineInteractionActive READ timelineInteractionActive NOTIFY
                 timelineInteractionChanged)

public:
  explicit Backend(QObject *parent = nullptr);
  ~Backend() override;
  QString coreVersion() const;
  QString projectId() const { return m_projectId; }
  QString projectName() const { return m_projectName; }
  QString projectLocation() const { return m_projectLocation; }
  QString projectFile() const { return m_projectFile; }
  QString sequenceId() const { return m_sequenceId; }
  QString sequenceName() const { return m_sequenceName; }
  QString activeWorkspace() const { return m_activeWorkspace; }
  QString layoutPreset() const { return m_layoutPreset; }
  QString captionFontFamily() const { return m_captionStyle.fontFamily; }
  QStringList downloadedCaptionFonts() const { return m_downloadedCaptionFonts; }
  int captionFontSize() const { return m_captionStyle.fontSize; }
  QString captionTextColor() const { return m_captionStyle.textColor; }
  bool captionBold() const { return m_captionStyle.bold; }
  bool captionItalic() const { return m_captionStyle.italic; }
  bool captionBackgroundVisible() const {
    return m_captionStyle.backgroundVisible;
  }
  QString captionBackgroundColor() const {
    return m_captionStyle.backgroundColor;
  }
  QString captionPosition() const { return m_captionStyle.position; }
  QString captionAlignment() const { return m_captionStyle.alignment; }
  double captionPositionX() const { return m_captionStyle.positionX; }
  double captionPositionY() const { return m_captionStyle.positionY; }
  bool captionBlurEnabled() const { return m_captionStyle.blurEnabled; }
  bool captionBlurTrackingEnabled() const {
    return m_captionStyle.blurTrackingEnabled;
  }
  double captionBlurRegionX() const { return m_captionStyle.blurRegionX; }
  double captionBlurRegionY() const { return m_captionStyle.blurRegionY; }
  double captionBlurRegionWidth() const {
    return m_captionStyle.blurRegionWidth;
  }
  double captionBlurRegionHeight() const {
    return m_captionStyle.blurRegionHeight;
  }
  int captionBlurStrength() const { return m_captionStyle.blurStrength; }
  int captionBlurPadding() const { return m_captionStyle.blurPadding; }
  QVariantList media() const { return m_media; }
  QVariantList clips() const { return m_clips; }
  TimelineClipModel *timelineClipModel() { return &m_timelineClipModel; }
  bool hasSubtitleClips() const;
  int videoTrackCount() const { return m_videoTrackCount; }
  int audioTrackCount() const { return m_audioTrackCount; }
  QStringList mutedTracks() const { return m_mutedTracks; }
  QVariantList trackStates() const;
  QVariantList markers() const { return m_markers; }
  bool snappingEnabled() const { return m_snappingEnabled; }
  int mediaCount() const { return m_media.size(); }
  bool mediaImportInProgress() const { return m_mediaImportQueue.active(); }
  int mediaImportProgress() const { return m_mediaImportQueue.percent(); }
  bool timelinePlacementInProgress() const {
    return m_timelinePlacementJob.inProgress();
  }
  double timelinePlacementProgress() const {
    return m_timelinePlacementJob.progress();
  }
  QString timelinePlacementStatus() const {
    return m_timelinePlacementJob.status();
  }
  bool hasSequence() const { return !m_sequenceId.isEmpty(); }
  bool hasMedia() const { return !m_media.isEmpty(); }
  bool canExport() const;
  qint64 durationMs() const;
  qint64 playheadMs() const { return m_playheadMs; }
  bool playing() const { return m_playing; }
  bool dirty() const { return m_dirty; }
  bool canUndo() const { return !m_undo.isEmpty(); }
  bool canRedo() const { return !m_redo.isEmpty(); }
  bool exportInProgress() const {
    return m_exportProcess.state() != QProcess::NotRunning;
  }
  double exportProgress() const { return m_exportProgress; }
  QString exportStatus() const { return m_exportStatus; }
  QVariantList transcript() const { return m_transcript; }
  bool transcriptionInProgress() const {
    return m_transcriptionProcess.state() != QProcess::NotRunning;
  }
  QString transcriptionStatus() const { return m_transcriptionStatus; }
  double transcriptionProgress() const { return m_transcriptionProgress; }
  QString transcriptLanguage() const { return m_transcriptLanguage; }
  QStringList transcribedMediaIds() const { return m_sourceTranscripts.keys(); }
  QStringList downloadedWhisperModels() const;
  bool translationInProgress() const {
    return m_translator.inProgress() && !m_translator.testInProgress();
  }
  bool translationTestInProgress() const {
    return m_translator.testInProgress();
  }
  QString translationStatus() const { return m_translator.status(); }
  bool demucsInProgress() const {
    return m_demucsProcess.state() != QProcess::NotRunning;
  }
  double demucsProgress() const { return m_demucsProgress; }
  QString demucsStatus() const { return m_demucsStatus; }
  QString lastError() const { return m_lastError; }
  QVariantMap colorSettings() const { return m_colorSettings; }
  QVariantMap activeColorClip() const;
  QVariantMap activeColorMedia() const;
  QString selectedClipId() const { return m_selectedClipId; }
  QVariantMap selectedClip() const;
  QVariantList effectDefinitions() const;
  KeyframeEngine *keyframeEngine() { return &m_keyframeEngine; }
  const KeyframeEngine *keyframeEngine() const { return &m_keyframeEngine; }
  TextToSpeechEngine *textToSpeechEngine() { return &m_textToSpeechEngine; }
  const TextToSpeechEngine *textToSpeechEngine() const {
    return &m_textToSpeechEngine;
  }
  VideoPreviewHelper *videoPreviewHelper() { return &m_videoPreviewHelper; }
  const VideoPreviewHelper *videoPreviewHelper() const {
    return &m_videoPreviewHelper;
  }
  QString customBlurEditClipId() const { return m_customBlurEditClipId; }
  QString customBlurEditInstanceId() const {
    return m_customBlurEditInstanceId;
  }
  QVariantMap appSettings() const { return m_appSettings; }
  QString mediaCachePath() const;
  QString mediaCacheSize() const;
  QString previewFrameUrl() const;
  bool previewDecoding() const {
    return m_previewDecoder.running() || m_scrubFrames.busy();
  }
  QString previewError() const;
  QImage previewFrameImage() const;

  void setProjectName(const QString &name);
  void setSequenceName(const QString &name);
  void setActiveWorkspace(const QString &workspace);
  void setLayoutPreset(const QString &preset);
  void setCaptionFontFamily(const QString &family);
  void setCaptionFontSize(int size);
  void setCaptionTextColor(const QString &color);
  void setCaptionBold(bool bold);
  void setCaptionItalic(bool italic);
  void setCaptionBackgroundVisible(bool visible);
  void setCaptionBackgroundColor(const QString &color);
  void setCaptionPosition(const QString &position);
  void setCaptionAlignment(const QString &alignment);
  void setCaptionBlurEnabled(bool enabled);
  void setCaptionBlurTrackingEnabled(bool enabled);
  Q_INVOKABLE void setCaptionBlurRegionNormalized(double x, double y,
                                                  double width, double height);
  void setCaptionBlurStrength(int strength);
  void setCaptionBlurPadding(int padding);
  Q_INVOKABLE void setCaptionPositionNormalized(double x, double y);
  void setPlayheadMs(qint64 value);
  void setPlaying(bool playing);
  void setSelectedClipId(const QString &clipId);

  Q_INVOKABLE bool
  newProject(const QString &name = QStringLiteral("Untitled"),
             const QString &location = QString(), bool createSequence = true,
             const QString &sequenceName = QStringLiteral("Sequence 01"));
  Q_INVOKABLE int importMedia(const QStringList &paths,
                              bool copyIntoProject = false);
  Q_INVOKABLE void importMediaAsync(const QStringList &paths,
                                    bool copyIntoProject = false);
  // Stops an import in progress. The folder scan and any copy in flight are
  // abandoned; media already added to the bin stays.
  Q_INVOKABLE void cancelMediaImport();
  Q_INVOKABLE bool removeMedia(const QString &mediaId);
  Q_INVOKABLE bool removeMediaSelection(const QStringList &mediaIds);
  Q_INVOKABLE bool renameMedia(const QString &mediaId, const QString &name);
  Q_INVOKABLE bool openMediaExternally(const QString &mediaId);
  Q_INVOKABLE bool revealMediaInFileManager(const QString &mediaId);
  Q_INVOKABLE bool copyMediaPath(const QString &mediaId);
  Q_INVOKABLE bool
  createSequence(const QString &name = QStringLiteral("Sequence 01"));
  Q_INVOKABLE QString addClip(const QString &mediaId, qint64 startMs = -1,
                              const QString &track = QString());
  Q_INVOKABLE QStringList addMediaToTimeline(
      const QString &mediaId, qint64 startMs = -1,
      const QString &track = QString());
  Q_INVOKABLE bool addMediaSelectionToTimeline(const QStringList &mediaIds);
  Q_INVOKABLE bool beginTimelinePlacement(const QStringList &mediaIds,
                                          qint64 startMs,
                                          const QString &track);
  Q_INVOKABLE void cancelTimelinePlacement();
  Q_INVOKABLE bool moveClip(const QString &clipId, qint64 startMs,
                            const QString &track = QString());
  Q_INVOKABLE bool moveClips(const QStringList &clipIds, qint64 deltaMs,
                             int trackDelta = 0);
  Q_INVOKABLE bool splitClip(const QString &clipId, qint64 positionMs);
  Q_INVOKABLE bool trimClipStart(const QString &clipId, qint64 startMs);
  Q_INVOKABLE bool trimClipEnd(const QString &clipId, qint64 endMs);
  Q_INVOKABLE bool deleteClipLeft(const QString &clipId, qint64 positionMs);
  Q_INVOKABLE bool deleteClipRight(const QString &clipId, qint64 positionMs);
  Q_INVOKABLE bool removeClip(const QString &clipId);
  Q_INVOKABLE bool removeClips(const QStringList &clipIds);
  Q_INVOKABLE QString addTrack(const QString &kind);
  Q_INVOKABLE bool removeLastTrack(const QString &kind);
  Q_INVOKABLE bool setTrackMuted(const QString &track, bool muted);
  Q_INVOKABLE bool setTrackState(const QString &track, const QString &state,
                                 bool enabled);
  Q_INVOKABLE QVariantMap trackState(const QString &track) const;
  Q_INVOKABLE bool trackLocked(const QString &track) const;
  Q_INVOKABLE bool trackVisible(const QString &track) const;
  Q_INVOKABLE bool trackSolo(const QString &track) const;
  Q_INVOKABLE bool trackSyncLocked(const QString &track) const;
  Q_INVOKABLE bool trackTargeted(const QString &track) const;
  Q_INVOKABLE qint64 snapTime(
      qint64 requestedMs, const QStringList &excludedClipIds = QStringList(),
      qint64 thresholdMs = 120) const;
  Q_INVOKABLE bool rippleDeleteClips(const QStringList &clipIds);
  Q_INVOKABLE bool rippleTrimClipEnd(const QString &clipId, qint64 endMs);
  Q_INVOKABLE bool closeGap(const QString &track, qint64 startMs, qint64 endMs);
  Q_INVOKABLE QString addMarker(qint64 positionMs,
                                const QString &name = QString(),
                                const QString &color = QString());
  Q_INVOKABLE bool updateMarker(const QString &markerId, qint64 positionMs,
                                const QString &name, const QString &color);
  Q_INVOKABLE bool removeMarker(const QString &markerId);
  Q_INVOKABLE void setSnappingEnabled(bool enabled);
  Q_INVOKABLE bool saveProject(const QString &path = QString());
  Q_INVOKABLE bool loadProject(const QString &path);
  Q_INVOKABLE QVariantMap probeMedia(const QString &path,
                                     bool generateDetailedPreviews = true);
  Q_INVOKABLE bool startExport(const QString &outputPath,
                               const QString &preset = QStringLiteral("high"));
  Q_INVOKABLE bool startExportWithSettings(const QString &outputPath,
                                           const QVariantMap &settings);
  Q_INVOKABLE bool downloadCaptionFont(const QString &family);
  Q_INVOKABLE QString suggestedExportPath() const;
  Q_INVOKABLE void cancelExport();
  Q_INVOKABLE bool
  transcribeMedia(const QString &mediaId,
                  const QString &model = QStringLiteral("tiny"),
                  const QString &language = QStringLiteral("auto"));
  Q_INVOKABLE void cancelTranscription();
  Q_INVOKABLE void cancelDemucs();
  Q_INVOKABLE bool importSubtitles(const QString &path);
  Q_INVOKABLE bool exportTranscriptSrt(const QString &path);
  // TTML, the format YouTube accepts as a caption upload and hands back on
  // download. SRT stays the default everywhere else; this is the round trip.
  Q_INVOKABLE bool exportTranscriptTtml(const QString &path);
  Q_INVOKABLE bool updateTranscriptSegment(int index, const QString &text);
  Q_INVOKABLE bool addTranscriptToTimeline();
  Q_INVOKABLE bool removeTranscriptFromTimeline();
  Q_INVOKABLE bool translateTranscript(const QString &targetLanguage,
                                       const QVariantMap &settings = {});
  Q_INVOKABLE bool testTranslationProvider(const QVariantMap &settings = {});
  Q_INVOKABLE void cancelTranslation();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void clearError();
  Q_INVOKABLE bool setColorSetting(const QString &key, const QVariant &value);
  Q_INVOKABLE bool setClipColorSetting(const QString &clipId,
                                       const QString &key,
                                       const QVariant &value);
  Q_INVOKABLE bool resetClipColorSettings(const QString &clipId);
  Q_INVOKABLE bool setClipEffectSetting(const QString &clipId,
                                        const QString &key,
                                        const QVariant &value);
  Q_INVOKABLE bool resetClipEffectSettings(const QString &clipId);
  Q_INVOKABLE bool generateTimedTextToSpeech(const QVariantList &segments,
                                             const QString &language,
                                             const QString &gender);
  Q_INVOKABLE QString addClipEffect(const QString &clipId,
                                    const QString &effectId);
  Q_INVOKABLE bool removeClipEffect(const QString &clipId,
                                    const QString &instanceId);
  Q_INVOKABLE bool moveClipEffect(const QString &clipId,
                                  const QString &instanceId, int offset);
  Q_INVOKABLE bool setClipEffectEnabled(const QString &clipId,
                                        const QString &instanceId,
                                        bool enabled);
  Q_INVOKABLE bool setClipEffectParameter(const QString &clipId,
                                          const QString &instanceId,
                                          const QString &parameterId,
                                          const QVariant &value);
  Q_INVOKABLE bool resetClipEffectInstance(const QString &clipId,
                                           const QString &instanceId);
  Q_INVOKABLE bool beginCustomBlurMaskEdit(const QString &clipId,
                                           const QString &instanceId);
  Q_INVOKABLE void endCustomBlurMaskEdit();
  Q_INVOKABLE bool setCustomBlurMask(const QString &clipId,
                                     const QString &instanceId, double x,
                                     double y, double width, double height);
  Q_INVOKABLE void requestEffectControls(const QString &clipId);
  Q_INVOKABLE void requestEffectsBrowser(const QString &clipId = QString());
  Q_INVOKABLE QString requestEffectPreview(const QString &clipId,
                                            const QString &effectId,
                                            bool animated = false);
  Q_INVOKABLE QVariantMap defaultAppSettings() const;
  Q_INVOKABLE bool applyAppSettings(const QVariantMap &settings);
  Q_INVOKABLE bool resetAppSettings();
  Q_INVOKABLE bool clearMediaCache();
  Q_INVOKABLE bool setMediaColorSetting(const QString &mediaId,
                                        const QString &key,
                                        const QVariant &value);
  Q_INVOKABLE bool startPreviewDecode(
      const QString &path, const QString &mediaKind, qint64 sourcePositionMs,
      qint64 durationMs, int sourceWidth, int sourceHeight, double frameRate,
      bool audioEnabled, double volume, const QString &audioPath = QString());
  Q_INVOKABLE bool requestPreviewFrame(const QString &path,
                                       qint64 sourcePositionMs,
                                       int sourceWidth, int sourceHeight);
  // Opens the container for a source before it is scrubbed, so the first frame
  // costs a seek instead of a header parse on a multi-gigabyte file.
  Q_INVOKABLE void prewarmPreviewSource(const QString &path, int sourceWidth = 0,
                                        int sourceHeight = 0);
  // Frees everything the preview pipeline holds for one source.
  Q_INVOKABLE void releasePreviewSource(const QString &path);
  // Counters from the scrub service, the frame cache and the session pool, for
  // the debug overlay.
  Q_INVOKABLE QVariantMap previewDecodeStatistics() const;
  Q_INVOKABLE void stopPreviewDecode();
  // Labels the current event-loop turn so a stall report names the QML handler
  // that caused it instead of saying "no marked scope". Call it as the first
  // statement of a handler that builds something; it clears itself when the turn
  // ends, so there is nothing to pair it with.
  Q_INVOKABLE void markGuiScope(const QString &label) const;
  // Short handle for a media path, used to build
  // "image://timeline-tile/<token>/<ms>" URLs. A path cannot be put in an
  // image:// URL directly on Windows, and the timeline needs one per visible
  // thumbnail slot.
  Q_INVOKABLE QString timelineTileToken(const QString &path);
  // False in builds without direct FFmpeg linkage, where the timeline falls back
  // to the pre-rendered filmstrip sheet.
  Q_INVOKABLE bool timelineTilesAvailable() const;
  // Short handle for a media path, used to build
  // "image://wave-window/<token>/<startMs>/<spanMs>/<columns>" URLs. Same
  // registry as timelineTileToken(), so both resolve to one source.
  Q_INVOKABLE QString waveformWindowToken(const QString &path);
  // False in builds without direct FFmpeg linkage, where the timeline keeps
  // stretching the whole-file waveform sheet.
  Q_INVOKABLE bool waveformWindowsAvailable() const;
  // Size the monitor draws preview frames at, in QML logical units. The decoder
  // decodes at the source's own resolution up to this size, so the picture is
  // never upscaled from a smaller decode - the reason a preview looked softer
  // than the file. Any smaller deliverable is chosen at export instead.
  Q_INVOKABLE void setPreviewSurfaceSize(int width, int height);
  // Interaction hold. While a drag, scrub or trim is in progress, filmstrip and
  // waveform decoding is refused instead of queued, so the GUI thread is not
  // competing with background decoders for the cores it needs to keep painting -
  // the reason dropping an 8 hour clip could show "Not Responding".
  //
  // The hold lapses on its own shortly after the last touch, so a missed end
  // call cannot leave thumbnails switched off.
  Q_INVOKABLE void beginTimelineInteraction();
  Q_INVOKABLE void touchTimelineInteraction();
  Q_INVOKABLE void endTimelineInteraction();
  bool timelineInteractionActive() const { return m_timelineInteractions > 0; }

signals:
  void projectChanged();
  void sequenceChanged();
  void mediaChanged();
  void mediaImportChanged();
  void timelinePlacementChanged();
  void timelinePlacementFinished(bool success, const QStringList &clipIds);
  void clipsChanged();
  void tracksChanged();
  void markersChanged();
  void snappingChanged();
  void timelineChanged();
  void playheadChanged();
  void playingChanged();
  void dirtyChanged();
  void historyChanged();
  void activeWorkspaceChanged();
  void layoutPresetChanged();
  void captionStyleChanged();
  void captionFontsChanged();
  void exportStateChanged();
  void demucsChanged();
  void demucsFinished(bool success, const QString &clipId);
  void errorChanged();
  void exportFinished(bool success, const QString &outputPath);
  void transcriptionFinished(bool success, const QString &mediaId);
  void transcriptChanged();
  void whisperModelsChanged();
  void colorSettingsChanged();
  // Identity only: "a different clip is selected now". Cheap by construction -
  // the one property that declares it is a QString - so it stays synchronous,
  // because a selection highlight is the acknowledgement of a click.
  void selectionChanged();
  // Contents of the selected clip. Everything expensive hangs off this one:
  // the effect controls panel and the effects browser both bind a Repeater's
  // model to Backend.selectedClip, so emitting it destroys and re-instantiates
  // their whole delegate tree inside whatever call emitted it. Deferred one
  // event-loop turn through m_selectionDetailNotify; never emit it directly.
  void selectionDetailChanged();
  void effectControlsRequested();
  void customBlurEditChanged();
  void effectsBrowserRequested();
  void effectPreviewReady(const QString &clipId, const QString &effectId,
                          bool animated, const QString &url);
  void appSettingsChanged();
  void mediaCacheChanged();
  void previewFrameChanged();
  void previewStateChanged();
  void previewErrorChanged();
  void timelineInteractionChanged();
  void autoSaveCompleted(const QString &path);

private:
  QString normalizePath(const QString &path) const;
  QString thumbnailForMedia(const QString &path, const QString &kind,
                            qint64 durationMs) const;
  QStringList expandImportPaths(const QStringList &paths) const;
  QVariantMap mediaById(const QString &id) const;
  int mediaIndex(const QString &id) const;
  int clipIndex(const QString &id) const;
  QStringList expandedLinkedClipIds(const QStringList &clipIds) const;
  QByteArray serializeState() const;
  bool restoreState(const QByteArray &json, bool fromHistory = false);
  void rememberState();
  void markDirty(bool dirty = true);
  void setError(const QString &message);
  void emitAllStateChanged();
  void updateExportProgress();
  void rebuildSequenceTranscript();
  // Windowed transcription. The worker streams one JSON object per line while it
  // runs, so a long source publishes segments as it goes instead of after hours
  // of silence - and a cancel keeps whatever has already landed.
  void consumeTranscriptionOutput();
  void handleTranscriptionEvent(const QVariantMap &event);
  void updateTranscriptionProgress();
  // Job scratch directory: one small WAV lives there at a time, and the whole
  // directory goes away when the process ends for any reason.
  QString startTranscriptionJobDir(const QString &mediaId);
  void clearTranscriptionJobDir();
  void ensureTrackExists(const QString &track);
  void pruneEmptyTracks();
  void beginTimedSpeechImport(const QVariantList &outputs);
  void importNextTimedSpeechOutput();
  void configureAutoSave();
  void performAutoSave();
  QString projectDatabasePath() const;
  bool persistProjectDatabase();
  void scheduleProjectDatabaseSave();
  void startDeferredMediaPreview(const QVariantMap &media);
  // True when the background preview job still has a poster, filmstrip or
  // waveform left to produce for this media entry.
  static bool needsDeferredPreview(const QVariantMap &item);
  // Ceiling on queued background thumbnail jobs for large media.
  static constexpr int kMaxPendingLargePreviews = 16;
  void finishDeferredMediaPreview();
  void recordAction(const QString &type, const QVariantMap &payload = {});

  QString m_projectId;
  QString m_projectName = QStringLiteral("Untitled");
  QString m_projectLocation;
  QString m_projectFile;
  QString m_projectDatabaseFile;
  QString m_sequenceId;
  QString m_sequenceName = QStringLiteral("Sequence 01");
  QString m_activeWorkspace = QStringLiteral("Edit");
  QString m_layoutPreset = QStringLiteral("ESSENTIALS");
  QVariantMap m_appSettings;
  QString m_selectedClipId;
  QString m_customBlurEditClipId;
  QString m_customBlurEditInstanceId;
  CaptionStyle m_captionStyle;
  QStringList m_downloadedCaptionFonts;
  QNetworkAccessManager m_fontNetwork;
  QVariantMap m_colorSettings;
  QVariantList m_media;
  void handleTimelinePlacementStep(const QVariantMap &item);
  bool startDemucsForClip(const QString &clipId);
  // Appends a batch of freshly probed media to the bin in one step. Emitting
  // per file rebuilt the whole model for every import.
  void appendImportedMedia(const QVariantList &items);
  void configureMediaImportQueue();
  QSet<QString> mediaDuplicateKeys() const;
  MediaImportQueue m_mediaImportQueue;
  bool m_mediaImportRemembered = false;
  QFutureWatcher<QVariantMap> m_largeMediaPreviewWatcher;
  QString m_largeMediaPreviewMediaId;
  QVariantList m_pendingLargeMediaPreviews;
  TimelinePlacementJob m_timelinePlacementJob;
  bool m_timelinePlacementActive = false;
  QStringList m_timelinePlacementAddedIds;
  QVariantList m_clips;
  TimelineClipModel m_timelineClipModel;
  int m_videoTrackCount = 1;
  int m_audioTrackCount = 1;
  QStringList m_mutedTracks;
  QVariantMap m_trackStates;
  QVariantList m_markers;
  bool m_snappingEnabled = true;
  qint64 m_playheadMs = 0;
  bool m_playing = false;
  bool m_dirty = false;
  QVector<QByteArray> m_undo;
  QVector<QByteArray> m_redo;
  QString m_lastError;
  ProjectDatabase m_projectDatabase;
  QProcess m_exportProcess;
  QString m_exportOutputPath;
  QString m_exportConcatFile;
  QByteArray m_exportStandardError;
  double m_exportProgress = 0.0;
  QString m_exportStatus;
  QProcess m_transcriptionProcess;
  QProcess m_demucsProcess;
  QString m_demucsClipId;
  QString m_demucsOutputDir;
  QByteArray m_demucsOutput;
  double m_demucsProgress = 0.0;
  int m_demucsProgressPass = 0;
  double m_demucsLastRawProgress = 0.0;
  int m_demucsExpectedPasses = 1;
  QString m_demucsStatus;
  QVariantList m_transcript;
  QHash<QString, QVariantList> m_sourceTranscripts;
  QHash<QString, QString> m_sourceTranscriptLanguages;
  QString m_transcriptionMediaId;
  QString m_transcriptionStatus;
  double m_transcriptionProgress = 0.0;
  QByteArray m_transcriptionStderr;
  bool m_transcriptionCancelRequested = false;
  // Windowed-run state. m_transcriptionSegments accumulates source-time segments
  // as each window lands; it is what the media's transcript becomes, and what
  // survives a cancel.
  TranscriptionPlan m_transcriptionPlan;
  QVariantList m_transcriptionSegments;
  QByteArray m_transcriptionStdout;
  // Last complete JSON line the worker printed. The single-pass reply is one
  // object on stdout, and stdout is now drained while the process runs, so it has
  // to be kept here rather than read back at exit.
  QByteArray m_transcriptionLastLine;
  QString m_transcriptionJobDir;
  bool m_transcriptionStreamed = false;
  int m_transcriptionWindowIndex = 0;
  int m_transcriptionWindowCount = 0;
  // Whisper's own tqdm percentage restarts at zero every window, so it is only
  // meaningful as a fraction of the current window.
  double m_transcriptionWindowFraction = 0.0;
  // How far each media's transcript reaches, so Transcribe can continue a
  // cancelled eight hour run instead of starting over.
  QHash<QString, qint64> m_transcriptCoverageMs;
  QString m_transcriptLanguage;
  TranscriptTranslator m_translator;
  TextToSpeechEngine m_textToSpeechEngine;
  EffectPreviewGenerator m_effectPreviewGenerator;
  KeyframeEngine m_keyframeEngine;
  FfmpegPreviewDecoder m_previewDecoder;
  // Scrub frames come from here instead of from a decoder restart per position:
  // one worker, newest request wins, warm container and frame cache behind it.
  ScrubFrameService m_scrubFrames;
  VideoPreviewHelper m_videoPreviewHelper;
  quint64 m_previewFrameRevision = 0;
  // Nesting depth of timeline gestures. A drag over the tracks raises this and
  // the drop lowers it; the filmstrip watches it so no thumbnail decode is even
  // requested while the pointer is moving.
  int m_timelineInteractions = 0;
  // Lowers the hold by itself if a gesture ends without saying so - a native
  // Windows drag that is cancelled by Escape never delivers a drop.
  QTimer m_timelineInteractionTimer;
  // Which of the two producers owns the frame the monitor is showing, so a
  // playback frame is not replaced by a stale scrub still or the reverse.
  bool m_previewFrameFromScrub = false;
  // colorSettingsChanged rebuilds the Lumetri panel's whole control tree in the
  // emitting call. On a drop that was 413-462 ms of GUI thread inside the drop
  // handler, so the notification is deferred one event-loop turn: same getters,
  // same eventual UI, just not inside the gesture. Declared after the state it
  // notifies about, so the emit is retracted before that state is destroyed.
  SignalCoalescer m_colorSettingsNotify{
      "Backend::colorSettingsChanged",
      [this]() { emit colorSettingsChanged(); }};
  // The other half of the selection notification. selectionChanged carries the
  // id and stays synchronous; this carries the map, and the map is what makes
  // panels exist.
  //
  // Measured: dropping a clip onto V1 held the GUI thread inside
  //   Backend::addMediaToTimeline > .../setSelectedClipId > .../selectionChanged
  // for 443 ms, then 904, 2238, 4505, 9041, 18098, 36225, 72493 ms - it never
  // finished. Every sample was six or seven levels of
  //   QQmlNotifier::emitNotify -> QQmlBinding::update -> QQmlVMEMetaObject::metaCall
  // ending in QQuickRepeater::setModel -> clear() -> QQmlDelegateModel::cancel
  // -> synchronous re-incubation of every delegate. Each level was one binding
  // on Backend.selectedClip rewriting a model that another binding then read.
  // Collapsing this to one notification per turn takes the cascade out of the
  // gesture and runs it once instead of once per nested level.
  SignalCoalescer m_selectionDetailNotify{
      "Backend::selectionDetailChanged",
      [this]() { emit selectionDetailChanged(); }};
  QTimer m_autoSaveTimer;
  QTimer m_projectDatabaseSaveTimer;
  QTimer m_ttsImportTimer;
  QVariantList m_pendingTtsOutputs;
  int m_pendingTtsIndex = 0;
  int m_pendingTtsAdded = 0;
  bool m_ttsImportActive = false;
};
