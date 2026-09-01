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
#include <QElapsedTimer>
#include <QImage>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPair>
#include <QProcess>
#include <QFutureWatcher>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "core/module_api.h"

// Native Qt application service exposed to QML. Project data uses Qt value
// types so it remains observable and can be serialized without another bridge.
class CUTPRO_BACKEND_API Backend : public QObject {
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
  // Everything except the subtitle cues. A imported subtitle track is tens of
  // thousands of clips against a handful of real ones, and almost every QML
  // scan over the timeline skips cues anyway - iterating this instead keeps
  // those scans the size of the edit rather than the size of the transcript.
  Q_PROPERTY(QVariantList mediaClips READ mediaClips NOTIFY clipsChanged)
  // The media a human voice could be transcribed from: real video and audio,
  // minus every generated-speech file. A timed voiceover adds one bin entry per
  // distinct cue, so this is a handful of entries against thousands - and the
  // Text panel used to work that out in QML by scanning the whole bin once per
  // bin entry.
  Q_PROPERTY(QVariantList transcribableMedia READ transcribableMedia NOTIFY
                 mediaChanged)
  // What the project bin shows: everything except entries a generator marked as
  // its own working files. Same reason as above - the bin has no interest in one
  // entry per spoken cue, and it should not have to walk them to find that out.
  Q_PROPERTY(QVariantList visibleMedia READ visibleMedia NOTIFY mediaChanged)
  Q_PROPERTY(TimelineClipModel *timelineClipModel READ timelineClipModel CONSTANT)
  Q_PROPERTY(bool hasSubtitleClips READ hasSubtitleClips NOTIFY clipsChanged)
  // Effect-track items only, so the monitor and the timeline can walk a handful
  // of entries per playhead move instead of the whole clip list.
  Q_PROPERTY(QVariantList timelineEffects READ timelineEffects NOTIFY
                 clipsChanged)
  Q_PROPERTY(int videoTrackCount READ videoTrackCount NOTIFY tracksChanged)
  Q_PROPERTY(int audioTrackCount READ audioTrackCount NOTIFY tracksChanged)
  Q_PROPERTY(QStringList mutedTracks READ mutedTracks NOTIFY tracksChanged)
  Q_PROPERTY(QVariantList trackStates READ trackStates NOTIFY tracksChanged)
  Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)
  Q_PROPERTY(bool snappingEnabled READ snappingEnabled WRITE setSnappingEnabled
                 NOTIFY snappingChanged)
  // True while an effect is being dragged out of the Effects panel. The effect
  // lane only exists on the timeline while it holds bars or while there is
  // something to drop on it, and the panel doing the dragging is not the panel
  // that has to show the lane - so the flag lives here, where both can see it.
  Q_PROPERTY(bool effectDragActive READ effectDragActive WRITE
                 setEffectDragActive NOTIFY effectDragActiveChanged)
  // True while the program monitor is showing the picture over the whole screen.
  // The monitor is the panel that re-hosts its own stage, but the window is what
  // has to go fullscreen and Escape is handled once for the whole window - so
  // like effectDragActive this lives where both ends can see it, which is also
  // what keeps the button and the key from disagreeing about the state.
  Q_PROPERTY(bool videoFullScreen READ videoFullScreen WRITE setVideoFullScreen
                 NOTIFY videoFullScreenChanged)
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
  // Bumped every time the prefetcher lands another thumbnail or waveform window.
  // The timeline's filmstrips and waveforms re-check what they hold whenever this
  // changes, which is how a clip's strip visibly grows in after a drop instead of
  // appearing whole or not at all. Monotonic; the value itself means nothing.
  Q_PROPERTY(int timelinePreviewRevision READ timelinePreviewRevision NOTIFY
                 timelinePreviewRevisionChanged)

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
  QVariantList mediaClips() const;
  QVariantList transcribableMedia() const;
  QVariantList visibleMedia() const;
  // O(1) by id, answered from the same cache the list above is built from.
  Q_INVOKABLE bool isTranscribableMedia(const QString &mediaId) const;
  // O(1) by id. QML used to hand-roll this as a linear scan in half a dozen
  // places, each of which walked the subtitle cues to find a video clip.
  Q_INVOKABLE QVariantMap clipById(const QString &id) const;
  // O(1) by id, same as clipById and for the same reason: half a dozen QML
  // functions looked a clip's source up by walking the whole bin, which a
  // generated voice track fills with one entry per spoken cue.
  Q_INVOKABLE QVariantMap mediaById(const QString &id) const;
  TimelineClipModel *timelineClipModel() { return &m_timelineClipModel; }
  bool hasSubtitleClips() const;
  QVariantList timelineEffects() const;
  int videoTrackCount() const { return m_videoTrackCount; }
  int audioTrackCount() const { return m_audioTrackCount; }
  QStringList mutedTracks() const { return m_mutedTracks; }
  QVariantList trackStates() const;
  QVariantList markers() const { return m_markers; }
  bool snappingEnabled() const { return m_snappingEnabled; }
  bool effectDragActive() const { return m_effectDragActive; }
  void setEffectDragActive(bool active);
  bool videoFullScreen() const { return m_videoFullScreen; }
  void setVideoFullScreen(bool active);
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
  // Places an image (or video) as a logo/graphic overlay on a fresh V track
  // above everything, flagged overlay:true with centered transform defaults so
  // it composites over the base picture and can be dragged/resized in the
  // monitor. Accepts an existing media id or a filesystem path/URL to import.
  Q_INVOKABLE QString addImageOverlay(const QString &pathOrMediaId,
                                      qint64 startMs = -1);
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
  // "Extract Audio": the embedded sound of each video clip becomes its own
  // A-track clip. The new clip is independent - CapCut's detached audio, not
  // Premiere's linked pair - so moving or deleting it leaves the video alone.
  // The video clip is marked separateAudio, which is what the export builder,
  // the program monitor and the waveform gate read to stop taking audio from
  // it, so nothing is heard twice. Returns the ids of the new audio clips.
  Q_INVOKABLE QStringList extractClipAudio(const QStringList &clipIds);
  // The other direction. Accepts either half: the extracted A-track clip is
  // removed, its audio settings go back onto the video clip, and the video
  // carries its own sound again.
  Q_INVOKABLE bool restoreClipAudio(const QStringList &clipIds);
  Q_INVOKABLE QString addTrack(const QString &kind, bool sticky = true);
  Q_INVOKABLE bool removeLastTrack(const QString &kind);
  // Placement rules for the drop handlers, answered against the live track
  // counts. QML owns the geometry (which row the pointer is over); this owns
  // what may go there, so the rule cannot drift between the two drag paths.
  Q_INVOKABLE QString trackForRow(int row) const;
  Q_INVOKABLE QString compatibleTrackFor(const QString &kind,
                                         const QString &requestedTrack) const;
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
  // Whole-clip snapping for a live drag: both edges compete, and the caller
  // gets back what was hit ("snapped", "startMs", "guideMs", "edge", "target")
  // rather than a bare time, because a guide line cannot be drawn from a time
  // that does not say whether it moved or which edge it belongs to.
  Q_INVOKABLE QVariantMap snapClipDrag(
      qint64 startMs, qint64 clipDurationMs,
      const QStringList &excludedClipIds = QStringList(),
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
  // An effect as its own timeline item on the F1 lane: the span of the item is
  // the stretch of the sequence the effect applies to, so trimming the bar is
  // how the user decides when it is on. Returns the new clip's id.
  Q_INVOKABLE QString addTimelineEffect(const QString &effectId,
                                        qint64 startMs = -1,
                                        qint64 durationMs = 0);
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
  // Applies to the session that is already playing. 0 is mute; the stream keeps
  // running, so un-muting is instant and does not need a restart.
  Q_INVOKABLE void setPreviewVolume(double volume);
  // exact = false asks for the cheap frame: the seek lands on the keyframe at or
  // before the position, which is what a moving playhead can afford. Pass true
  // once the playhead settles to decode forward to the requested frame itself.
  Q_INVOKABLE bool requestPreviewFrame(const QString &path,
                                       qint64 sourcePositionMs,
                                       int sourceWidth, int sourceHeight,
                                       bool exact = false);
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
  // Source position of the frame the monitor is actually showing during
  // playback, or -1 before the first one arrives. The monitor's playhead follows
  // this instead of a wall clock started when Play was pressed: the decoder
  // needs to open the container, seek and decode before any picture exists, and
  // counting that time as elapsed playback put the playhead - and with it the
  // time display, the subtitle overlay and the still drawn on pause - ahead of
  // the image.
  Q_INVOKABLE qint64 previewPresentedSourceMs() const;

  // How long ago that picture was handed to QML, in milliseconds, or -1 when
  // there is none. The monitor's UI tick runs every 50 ms and the source
  // publishes a frame every 33-40, so a tick that notices a new picture noticed
  // it up to a frame or two late. Anchoring the playback clock at "now" threw
  // that delay away on every single frame, which is why the playhead sat one or
  // two frames behind the image and the pause handler had to jump forward to
  // catch up. Back-dating the anchor by this age is what removes the gap
  // instead of correcting it after the fact.
  Q_INVOKABLE qint64 previewPresentedAgeMs() const;

  // The newest picture whose source position the playback clock has already
  // reached, or -1. A frame can be published in the couple of milliseconds
  // between the pause click and the decoder being stopped: it was queued, never
  // painted, and anchoring the pause onto it steps the image forward at the
  // click. That is the "the frame is ahead when I pause" report. Asking for the
  // frame at or before the clock keeps the pause on the picture the eye had.
  Q_INVOKABLE qint64 previewPresentedSourceMsAtOrBefore(qint64 sourceMs) const;

  // Pixel width of the picture on screen, or 0 when there is none. The still
  // rendered on pause is the same frame decoded at the source's own resolution,
  // which is a sharpness upgrade the monitor only asks for when the playback
  // frame carries fewer pixels than the panel draws. Otherwise the swap changes
  // nothing except the moment it lands - 100-400 ms after the freeze - and that
  // reads as the frozen frame twitching.
  Q_INVOKABLE int previewPresentedFrameWidth() const;

  // Report the timeline position of the picture on screen, so the playback trace
  // can print the playhead-versus-image gap as a number. The mapping from a
  // source position to a timeline position needs the active clip's startMs and
  // sourceInMs, which only the monitor knows, so the monitor pushes it here
  // rather than the trace pulling it. A no-op unless CUTPRO_PLAYBACK_TRACE is
  // set.
  Q_INVOKABLE void tracePlaybackDrift(qint64 presentedTimelineMs) const;
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
  // Declare what a clip layer wants decoded, instead of asking the image
  // providers for it directly.
  //
  // `requesterId` is one opaque key per clip layer; a second call with the same
  // key replaces the first, so a pan or a zoom abandons the positions that
  // scrolled off screen rather than queueing behind them. `bucketsMs` are the
  // exact values the QML builds its tile URLs from, in the order they should be
  // decoded - left to right across the visible slice.
  //
  // A single background thread serves every wish, round-robin between clips, one
  // decode at a time. Nothing here blocks: the call is a hash insert.
  Q_INVOKABLE void requestTimelineTiles(const QString &requesterId,
                                        const QString &token,
                                        const QVariantList &bucketsMs);
  Q_INVOKABLE void requestWaveformWindows(const QString &requesterId,
                                          const QString &token,
                                          const QVariantList &startsMs,
                                          qint64 spanMs, int columns);
  // Called when a clip layer goes away, so its wish does not hold a slice of the
  // worker's attention for a delegate that no longer exists.
  Q_INVOKABLE void cancelTimelinePreviewRequest(const QString &requesterId);
  // Whether that exact position is in memory *now*. The timeline only points an
  // Image at a tile or a window this returns true for, which is what guarantees
  // no provider thread ever has to decode one: a request that would have to open
  // a file is never made. Memory-only and mutex-cheap, because it is asked per
  // visible slot on every revision.
  Q_INVOKABLE bool timelineTileReady(const QString &token,
                                     qint64 bucketMs) const;
  Q_INVOKABLE bool waveformWindowReady(const QString &token, qint64 startMs,
                                       qint64 spanMs, int columns) const;
  int timelinePreviewRevision() const;
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
  void effectDragActiveChanged();
  void videoFullScreenChanged();
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
  void timelinePreviewRevisionChanged();
  void autoSaveCompleted(const QString &path);

private:
  QString normalizePath(const QString &path) const;
  QString thumbnailForMedia(const QString &path, const QString &kind,
                            qint64 durationMs) const;
  QStringList expandImportPaths(const QStringList &paths) const;
  int mediaIndex(const QString &id) const;
  int clipIndex(const QString &id) const;
  QStringList expandedLinkedClipIds(const QStringList &clipIds) const;
  // The lowest A track with nothing across [startMs, endMs), or one past the last
  // existing lane when they are all busy. Extracted audio has to land where it
  // cannot overwrite anything: it keeps the video clip's own times, so any lane
  // already occupied there is not a candidate.
  QString freeAudioTrack(qint64 startMs, qint64 endMs) const;
  // Every clip carrying the same sound as the one at this index: the extracted
  // A-track clip of a video clip, the video clip an extracted clip came out of,
  // and the members of a legacy link group. Used to keep an audio setting on one
  // half from disagreeing with the other; it is not a selection or delete group.
  QVector<int> audioPeerIndexes(int index) const;
  // A link group left behind by an older build's Extract Audio. Those pairs are
  // no longer treated as linked - CapCut's detached audio is independent - so the
  // commands that act on a whole group have to leave them alone, or a project
  // saved before the change would still delete the video with the audio.
  bool isDetachedAudioGroup(const QString &group) const;
  // The whole project as plain values. This is the cheap form: the clip, media
  // and transcript lists go in by reference, so building it costs a handful of
  // small map inserts no matter how long the timeline is. Everything else -
  // undo, the SQLite mirror, the .cutpro file - is derived from this.
  QVariantMap stateVariant() const;
  // The whole project as one object. Kept separate from serializeState() so the
  // SQLite mirror can take it directly: that path used to serialise to JSON text
  // and immediately parse the text back into a QVariantMap, three full passes
  // over every clip to produce something the object already was.
  QJsonObject stateObject() const;
  // Compact by default. The indented writer emits a newline and an indent per
  // value, and a subtitle track puts tens of thousands of values in here; only
  // the file the user may open in an editor is worth that cost.
  QByteArray serializeState(bool pretty = false) const;
  bool restoreState(const QByteArray &json, bool fromHistory = false);
  // The single restore implementation. restoreState() is the file/JSON door into
  // it; undo and redo come in here directly, because their snapshots never had
  // to become text in the first place.
  bool restoreStateVariant(const QVariantMap &state, bool fromHistory = false);
  void rememberState();
  // Undo depth is bounded by cost as well as by count. Snapshots are value maps
  // now, not JSON text, so an unchanged list costs one shared reference and a
  // changed one costs a fresh spine - the estimate below prices a state by the
  // spines it can own rather than by megabytes of text it no longer builds.
  static constexpr int kMaxUndoStates = 100;
  static constexpr qint64 kMaxUndoBytes = 96 * 1024 * 1024;
  static qint64 approximateStateBytes(const QVariantMap &state);
  // How often the action log may carry a full state snapshot. Nothing reads that
  // column, so this is a floor on how much a row costs, not a recovery window.
  static constexpr qint64 kActionSnapshotIntervalMs = 30000;
  void markDirty(bool dirty = true);
  void setError(const QString &message);
  void emitAllStateChanged();
  void updateExportProgress();
  void rebuildSequenceTranscript();
  // Asks for one rebuild once the timeline stops changing. The rebuild walks
  // every clip, so running it inside a burst of edits costs the whole walk per
  // edit and throws all but the last result away; the timed-speech import is a
  // burst thousands of turns long. The timer restarts on every request, so a
  // burst pays for one rebuild at its end and a lone edit pays for one a moment
  // later.
  void scheduleSequenceTranscriptRebuild();
  QTimer m_transcriptRebuildTimer;
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
  // `releaseUserTracks` drops the hand-added track floors before recomputing, so
  // the stack collapses to the tracks that actually hold clips. Pass it from any
  // edit that vacates a track - a delete or a move - because that is the moment
  // the user expects an emptied lane to disappear. Additive edits and project
  // loads keep the floors, which is what makes V+ hold a lane long enough to be
  // useful and lets a saved sequence reopen with its empty tracks intact.
  void pruneEmptyTracks(bool releaseUserTracks = false);
  void beginTimedSpeechImport(const QVariantList &outputs);
  void importNextTimedSpeechOutput();
  // Existing occupancy of A1..A64, read once, in one pass over the timeline.
  void buildSpeechLaneIndex();
  // The first lane that is free for [startMs, endMs), A64 when none is.
  QString reserveSpeechLane(qint64 startMs, qint64 endMs);
  void configureAutoSave();
  void performAutoSave();
  QString projectDatabasePath() const;
  bool persistProjectDatabase();
  void scheduleProjectDatabaseSave();
  void startDeferredMediaPreview(const QVariantMap &media);
  // True when the background preview job still has a poster, filmstrip or
  // waveform left to produce for this media entry.
  static bool needsDeferredPreview(const QVariantMap &item);
  // Starts that job for a source already in the bin, if it is still missing
  // anything. Called when a clip is placed, which is where the artefacts are
  // actually drawn.
  void ensureMediaPreviews(const QString &mediaId);
  // Ceiling on queued background thumbnail jobs for large media.
  static constexpr int kMaxPendingLargePreviews = 16;
  void finishDeferredMediaPreview();
  // The snapshot is the cheap value form, and it is only turned into JSON text
  // on the rare row that actually carries one - the throttle below discards it
  // otherwise, and serialising a discarded snapshot was the whole problem.
  void recordAction(const QString &type, const QVariantMap &payload = {},
                    const QVariantMap &stateSnapshot = QVariantMap());
  qint64 m_lastActionSnapshotMs = 0;

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
  // Facts derived from m_media, pinned to the exact list version they were built
  // from - the same trick m_clipCachePin plays for the clips, and for the same
  // reason. mediaIndex() was a linear scan that converted a QVariantMap per
  // element, and rebuildSequenceTranscript() called it once per clip: importing
  // a twenty-thousand-cue voiceover made that product grow on both sides at
  // once, which is what froze the window for sixteen seconds at a time.
  mutable QVariantList m_mediaCachePin;
  mutable QHash<QString, int> m_cachedMediaIndex;
  mutable QVariantList m_cachedTranscribableMedia;
  mutable QVariantList m_cachedVisibleMedia;
  mutable QSet<QString> m_cachedTranscribableIds;
  mutable bool m_mediaCacheReady = false;
  void ensureMediaCaches() const;
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
  // Facts derived from m_clips that QML reads through bindings. Recomputing
  // them on every read is what made a twenty-thousand-cue subtitle track slow:
  // one Backend.durationMs binding walked the whole list, and there are several
  // such bindings. The cache is pinned to the exact list version it was built
  // from - m_clipCachePin shares m_clips' buffer, so any mutation is forced to
  // detach and the two data pointers stop matching. That is what makes this
  // safe without touching the forty-odd places that mutate m_clips: none of
  // them has to remember to invalidate anything.
  mutable QVariantList m_clipCachePin;
  mutable QHash<QString, int> m_cachedClipIndex;
  mutable QVariantList m_cachedTimelineEffects;
  mutable QVariantList m_cachedMediaClips;
  mutable QVector<int> m_cachedVideoClips;
  mutable qint64 m_cachedDurationMs = 0;
  mutable bool m_cachedHasSubtitleClips = false;
  mutable bool m_cachedHasRenderableClips = false;
  mutable bool m_clipCacheReady = false;
  void ensureClipCaches() const;
  TimelineClipModel m_timelineClipModel;
  int m_videoTrackCount = 1;
  int m_audioTrackCount = 1;
  // Tracks the user asked for by hand (V+ / A-), as opposed to tracks that
  // exist because a clip is sitting on them. pruneEmptyTracks() recomputes the
  // counts from clip occupancy, which used to delete an empty track the moment
  // any other edit ran - so a lane added to drop a clip onto was gone before
  // the drop, and the drop then landed on the only remaining track.
  int m_minVideoTracks = 1;
  int m_minAudioTracks = 0;
  QStringList m_mutedTracks;
  QVariantMap m_trackStates;
  QVariantList m_markers;
  bool m_snappingEnabled = true;
  // Purely view flags: not part of the project, so they never mark it dirty and
  // never enter the undo history.
  bool m_effectDragActive = false;
  bool m_videoFullScreen = false;
  qint64 m_playheadMs = 0;
  bool m_playing = false;
  bool m_dirty = false;
  // Value snapshots, not JSON text. Serialising the whole project on every
  // undoable edit cost ~945 ms with a 19831-cue subtitle track on the timeline;
  // these share the lists they were taken from, so an edit that touches one clip
  // copies one list spine and nothing else.
  QVector<QVariantMap> m_undo;
  QVector<QVariantMap> m_redo;
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
  // The picture QML was actually handed, recorded on the GUI thread when the
  // frame reaches the image provider rather than on the decode thread when it is
  // published. Those are not the same instant: the decode thread publishes and
  // the GUI thread picks the frame up one queued call later, so the decoder's own
  // presentedSourceMs() can already name a frame that nothing has drawn yet.
  qint64 m_paintedSourceMs = -1;
  // The one before it, which is the picture still on screen when the newest
  // frame was published but not yet painted.
  qint64 m_previousPaintedSourceMs = -1;
  // Wall clock, same epoch as QML's Date.now(), of the moment m_paintedSourceMs
  // was handed over.
  qint64 m_paintedWallMs = 0;
  // Pixel width of the picture QML was handed, for the pause path's decision
  // about whether a full-resolution still would look any different.
  int m_paintedFrameWidth = 0;
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
  // One audio lane's occupancy, prepared once per import.
  //
  // Placing a generated cue used to rescan every clip on the timeline for every
  // candidate lane: with 19831 cues over a 19831-clip subtitle track that is
  // hundreds of millions of QVariantMap conversions, and it ran on the GUI thread
  // in batches of two. The question is monotonic instead - cues are imported in
  // ascending start order - so each lane keeps its existing clips sorted and a
  // cursor that only ever moves forward past the ones already behind the playhead
  // of the import.
  struct SpeechLane {
    QVector<QPair<qint64, qint64>> intervals;  // existing clips, sorted by start
    int cursor = 0;
    qint64 appendedEndMs = -1;  // end of the last cue this import placed here
    bool blocked = false;       // a locked track this import must not touch
  };
  QVector<SpeechLane> m_ttsLanes;
  // Path -> media id for this import. Identical cue text is synthesized once and
  // therefore lands in one file, so the second clip that needs it reuses the media
  // entry instead of adding a seventeen-key duplicate of it.
  QHash<QString, QString> m_ttsMediaByPath;
  int m_pendingTtsIndex = 0;
  int m_pendingTtsAdded = 0;
  bool m_ttsImportActive = false;
  // How often the half-built timeline is published, as opposed to how often the
  // import runs.
  //
  // Every clipsChanged during this import re-derives everything downstream of the
  // clip list: the preview helper re-indexes every clip and every bin entry, the
  // timeline model re-projects its viewport, the transcript is rebuilt. That is
  // the right work to do for an edit, and the wrong work to do thousands of times
  // for one import - it is what the watchdog caught the window sitting inside for
  // sixteen seconds. Placement still runs on every turn against a millisecond
  // budget; only the notification is rationed, so the clip count climbs a few
  // times a second instead of hundreds of times a second and the frames in
  // between belong to the window.
  QElapsedTimer m_ttsPublishClock;
  int m_ttsPublishedMediaCount = 0;
  int m_ttsPublishedVideoTracks = 0;
  int m_ttsPublishedAudioTracks = 0;
};
