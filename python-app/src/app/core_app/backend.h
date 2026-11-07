#pragma once

#include "app/caption_style.h"
#include "app/effects/clip_effects.h"
#include "app/media/media_import_queue.h"
#include "app/media/noise_removal.h"
#include "app/preview/effect_preview_generator.h"
#include "app/preview/ffmpeg_preview_decoder.h"
#include "app/preview/scrub_frame_service.h"
#include "app/timeline/keyframe_engine.h"
#include "app/lumetri/lumetri_preview.h"
#include "app/subtitles/text_to_speech_engine.h"
#include "app/subtitles/speech_chain.h"
#include "app/subtitles/timed_speech_placer.h"
#include "app/subtitles/window_audio_prefetcher.h"
#include "app/timeline/timeline_editor.h"
#include "app/timeline/timeline_placement_job.h"
#include "app/timeline/timeline_clip_model.h"
#include "app/preview/video_preview_helper.h"
#include "app/preview/preview_playback_clock.h"
#include "app/subtitles/recap_generator.h"
#include "app/subtitles/transcript_translator.h"
#include "app/subtitles/transcription_plan.h"
#include "app/subtitles/transcription_batch.h"
#include "app/subtitles/translation_model_probe.h"
#include "app/core_app/caption_speech_timing.h"
#include "app/core_app/project_database.h"
#include "app/core_app/project_save_service.h"
#include "app/core_app/signal_coalescer.h"

#include <QObject>
#include <QElapsedTimer>
#include "app/export/export_progress.h"
#include "app/export/export_time_estimate.h"
#include <QImage>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QMutex>
#include <QPair>
#include <QProcess>
#include <QFutureWatcher>
#include <memory>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <atomic>

#include "core/module_api.h"

class QFileSystemWatcher;
// Forward-declared rather than included: the capture thread lives in
// cutpro_preview and only this pointer crosses the boundary.
class VoiceOverRecorder;
class WebcamRecorder;
class QMediaPlayer;
class QAudioOutput;

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
  // The folder the home screen lists projects from and creates them in. Backed
  // by the workspaceLocation app setting; the setter writes it through so the
  // choice survives a relaunch.
  Q_PROPERTY(QString workspaceLocation READ workspaceLocation WRITE
                 setWorkspaceLocation NOTIFY workspaceLocationChanged)
  Q_PROPERTY(QString sequenceId READ sequenceId NOTIFY sequenceChanged)
  Q_PROPERTY(QString sequenceName READ sequenceName WRITE setSequenceName NOTIFY
                 sequenceChanged)
  Q_PROPERTY(QString activeWorkspace READ activeWorkspace WRITE
                 setActiveWorkspace NOTIFY activeWorkspaceChanged)
  // License activation (offline, node-locked). `licensed` gates the whole UI;
  // `machineId` is the string the user sends to the vendor to obtain a key.
  Q_PROPERTY(bool licensed READ licensed NOTIFY licensedChanged)
  Q_PROPERTY(QString machineId READ machineId CONSTANT)
  Q_PROPERTY(QString layoutPreset READ layoutPreset WRITE setLayoutPreset NOTIFY
                 layoutPresetChanged)
  Q_PROPERTY(QString captionFontFamily READ captionFontFamily WRITE
                 setCaptionFontFamily NOTIFY captionStyleChanged)
  Q_PROPERTY(QStringList downloadedCaptionFonts READ downloadedCaptionFonts
                 NOTIFY captionFontsChanged)
  // Family names of fonts the user imported from their own files.
  Q_PROPERTY(QStringList customCaptionFonts READ customCaptionFonts NOTIFY
                 captionFontsChanged)
  // The ordered picker model the caption Font dropdown binds to: one map per
  // entry, {family, available, downloadable, category}. Re-read on
  // captionFontsChanged so imports/downloads/removals show up live.
  Q_PROPERTY(QVariantList captionFontModel READ captionFontModel NOTIFY
                 captionFontsChanged)
  // The full Google Fonts library the settings page searches over: one map per
  // family, {family, category}. Empty until fetchGoogleFontsCatalog() pulls it.
  Q_PROPERTY(QVariantList googleFontsCatalog READ googleFontsCatalog NOTIFY
                 googleFontsCatalogChanged)
  Q_PROPERTY(bool googleFontsLoading READ googleFontsLoading NOTIFY
                 googleFontsCatalogChanged)
  Q_PROPERTY(QString googleFontsError READ googleFontsError NOTIFY
                 googleFontsCatalogChanged)
  // Families the browser has fetched a throwaway copy of just to render a
  // preview sample - registered but not installed, so a Download still persists.
  Q_PROPERTY(QStringList previewedCaptionFonts READ previewedCaptionFonts NOTIFY
                 previewedCaptionFontsChanged)
  Q_PROPERTY(int captionFontSize READ captionFontSize WRITE setCaptionFontSize
                 NOTIFY captionStyleChanged)
  Q_PROPERTY(double captionBoxWidth READ captionBoxWidth WRITE setCaptionBoxWidth
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
  // Letter style and caption animation, plus the two catalogs the Text Effects
  // gallery is built from. The catalogs are CONSTANT: they are compiled-in
  // tables, so a binding on one never has to be re-evaluated.
  Q_PROPERTY(QString captionTextEffect READ captionTextEffect WRITE
                 setCaptionTextEffect NOTIFY captionStyleChanged)
  Q_PROPERTY(QString captionAnimation READ captionAnimation WRITE
                 setCaptionAnimation NOTIFY captionStyleChanged)
  Q_PROPERTY(double captionAnimationSpeed READ captionAnimationSpeed WRITE
                 setCaptionAnimationSpeed NOTIFY captionStyleChanged)
  Q_PROPERTY(QVariantList textEffectStyles READ textEffectStyles CONSTANT)
  Q_PROPERTY(QVariantList textEffectAnimations READ textEffectAnimations CONSTANT)
  // When each word of the cue under the playhead was spoken, read off the clip's
  // audio. Only for a cue that carries no timing of its own - an imported .srt, a
  // translated line, a caption typed by hand - so karaoke follows the speaker
  // there too. Empty while the analysis has not landed, and empty for a
  // transcribed cue, which already knows.
  Q_PROPERTY(QString activeCaptionWordTimes READ activeCaptionWordTimes NOTIFY
                 activeCaptionWordTimesChanged)
  Q_PROPERTY(QVariantList media READ media NOTIFY mediaChanged)
  Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
  // How many clips there are, for the bindings that only wanted to know whether
  // the timeline is empty. Reading `clips` from QML converts every element into
  // a JS object, so `Backend.clips.length > 0` on an imported transcript built
  // twenty thousand wrappers to answer a question about zero - and it did that
  // once per binding, on every clipsChanged.
  Q_PROPERTY(int clipCount READ clipCount NOTIFY clipsChanged)
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
  // How many text lanes the timeline shows (S1..Sn). Derived from the highest S
  // track any subtitle clip sits on - text stacks onto extra lanes like CapCut.
  Q_PROPERTY(int subtitleTrackCount READ subtitleTrackCount NOTIFY tracksChanged)
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
  Q_PROPERTY(double exportFramesPerSecond READ exportFramesPerSecond NOTIFY exportStateChanged)
  Q_PROPERTY(double exportSpeed READ exportSpeed NOTIFY exportStateChanged)
  Q_PROPERTY(double exportBytesPerSecond READ exportBytesPerSecond NOTIFY exportStateChanged)
  Q_PROPERTY(qint64 exportRemainingSeconds READ exportRemainingSeconds NOTIFY exportStateChanged)
  Q_PROPERTY(qint64 exportElapsedSeconds READ exportElapsedSeconds NOTIFY exportStateChanged)
  Q_PROPERTY(QString exportEncoder READ exportEncoder NOTIFY exportStateChanged)
  Q_PROPERTY(QString exportEncoderFallback READ exportEncoderFallback NOTIFY exportStateChanged)
  Q_PROPERTY(bool exportFinalizing READ exportFinalizing NOTIFY exportStateChanged)
  Q_PROPERTY(bool exportRemainingIncludesFinalization READ exportRemainingIncludesFinalization NOTIFY exportStateChanged)
  Q_PROPERTY(QVariantList transcript READ transcript NOTIFY transcriptChanged)
  // Same reason as clipCount: a transcript is one entry per cue, so the bindings
  // that only wanted to know whether there is a transcript at all were paying for
  // the whole list.
  Q_PROPERTY(int transcriptCount READ transcriptCount NOTIFY transcriptChanged)
  Q_PROPERTY(bool transcriptionInProgress READ transcriptionInProgress NOTIFY
                 transcriptChanged)
  Q_PROPERTY(QVariantList transcriptionBatchJobs READ transcriptionBatchJobs
                 NOTIFY transcriptionBatchJobsChanged)
  Q_PROPERTY(QString transcriptionStatus READ transcriptionStatus NOTIFY
                 transcriptChanged)
  Q_PROPERTY(double transcriptionProgress READ transcriptionProgress NOTIFY
                 transcriptChanged)
  Q_PROPERTY(bool transcriptionIndeterminate READ transcriptionIndeterminate
                 NOTIFY transcriptChanged)
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
  Q_PROPERTY(QVariantList translationPreview READ translationPreview NOTIFY transcriptChanged)
  Q_PROPERTY(int translationCompletedSegments READ translationCompletedSegments NOTIFY transcriptChanged)
  Q_PROPERTY(int translationTotalSegments READ translationTotalSegments NOTIFY transcriptChanged)
  Q_PROPERTY(bool translationTestInProgress READ translationTestInProgress
                 NOTIFY transcriptChanged)
  // The model-catalogue probe. Its own pair because it runs in its own process
  // and has to be able to answer while a translation is still going.
  Q_PROPERTY(bool translationModelsInProgress READ translationModelsInProgress
                 NOTIFY translationModelsChanged)
  Q_PROPERTY(QString translationModelsStatus READ translationModelsStatus NOTIFY
                 translationModelsChanged)
  // Video recap: a Gemini narration script derived from the current transcript.
  // Its own signal because it runs alongside, and independently of, translation.
  Q_PROPERTY(bool recapInProgress READ recapInProgress NOTIFY recapChanged)
  Q_PROPERTY(QString recapStatus READ recapStatus NOTIFY recapChanged)
  Q_PROPERTY(QString recapText READ recapText NOTIFY recapChanged)
  // Scene Edit Detection runs a bundled-ffmpeg scene-score pass over one clip's
  // trimmed range; its own signal so the dialog can show progress independently.
  Q_PROPERTY(bool sceneDetectInProgress READ sceneDetectInProgress NOTIFY
                 sceneDetectChanged)
  Q_PROPERTY(QString sceneDetectStatus READ sceneDetectStatus NOTIFY
                 sceneDetectChanged)
  // 0..1 across the scanned span, from ffmpeg's own -progress stream. Without
  // this a multi-hour clip showed one unchanging label for the whole pass.
  Q_PROPERTY(double sceneDetectProgress READ sceneDetectProgress NOTIFY
                 sceneDetectChanged)
  Q_PROPERTY(bool demucsInProgress READ demucsInProgress NOTIFY demucsChanged)
  Q_PROPERTY(double demucsProgress READ demucsProgress NOTIFY demucsChanged)
  Q_PROPERTY(QString demucsStatus READ demucsStatus NOTIFY demucsChanged)
  // System dependency check driving the navbar "setup" cloud icon. The list
  // holds one entry per requirement (Python, edge-tts, ffmpeg, and the optional
  // offline engines); requirementsSatisfied is true once every *required* one is
  // present, which is what hides the icon.
  Q_PROPERTY(QVariantList systemRequirements READ systemRequirements NOTIFY
                 systemRequirementsChanged)
  Q_PROPERTY(bool requirementsSatisfied READ requirementsSatisfied NOTIFY
                 systemRequirementsChanged)
  Q_PROPERTY(bool requirementsChecking READ requirementsChecking NOTIFY
                 systemRequirementsChanged)
  // Install-in-progress state for the optional engines (Whisper/Demucs). id of
  // the requirement being pip-installed, empty when idle, plus the last line of
  // pip output so the Requirements page can show live progress.
  Q_PROPERTY(QString installingRequirement READ installingRequirement NOTIFY
                 requirementInstallChanged)
  Q_PROPERTY(QString requirementInstallStatus READ requirementInstallStatus
                 NOTIFY requirementInstallChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
  Q_PROPERTY(
      QVariantMap colorSettings READ colorSettings NOTIFY colorSettingsChanged)
  Q_PROPERTY(QVariantMap activeColorClip READ activeColorClip NOTIFY
                 colorSettingsChanged)
  Q_PROPERTY(QVariantMap activeColorMedia READ activeColorMedia NOTIFY
                 colorSettingsChanged)
  Q_PROPERTY(QString selectedClipId READ selectedClipId WRITE setSelectedClipId
                 NOTIFY selectionChanged)
  Q_PROPERTY(QStringList effectEditSelection READ effectEditSelection NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap selectedClip READ selectedClip NOTIFY
                 selectionDetailChanged)
  Q_PROPERTY(QVariantList effectDefinitions READ effectDefinitions CONSTANT)
  // The two preset shelves, shaped like effect definitions so the browser can
  // draw all four of its shelves with one delegate. CONSTANT because both are
  // compiled in: nothing at runtime adds to them.
  Q_PROPERTY(QVariantList effectPresets READ effectPresets CONSTANT)
  Q_PROPERTY(QVariantList lumetriPresets READ lumetriPresets CONSTANT)
  Q_PROPERTY(KeyframeEngine *keyframeEngine READ keyframeEngine CONSTANT)
  Q_PROPERTY(TextToSpeechEngine *textToSpeechEngine READ textToSpeechEngine
                 CONSTANT)
  // True while clips are being placed onto the timeline in bulk (generated
  // subtitle voices, today). The timeline publishes itself a few times a second
  // during that, so panels that answer a timeline change with real work - the
  // monitor decoding a frame - use this to sit still and do it once at the end.
  // It is cleared before the import's final emits, so that last refresh is not
  // suppressed.
  Q_PROPERTY(bool timelineImportActive READ timelineImportActive NOTIFY
                 timelineImportActiveChanged)
  Q_PROPERTY(VideoPreviewHelper *videoPreviewHelper READ videoPreviewHelper
                 CONSTANT)
  Q_PROPERTY(QString customBlurEditClipId READ customBlurEditClipId NOTIFY
                 customBlurEditChanged)
  Q_PROPERTY(QString customBlurEditInstanceId READ customBlurEditInstanceId
                 NOTIFY customBlurEditChanged)
  Q_PROPERTY(QVariantMap appSettings READ appSettings NOTIFY appSettingsChanged)
  // Interface state: which tab a panel was left on, the Whisper model chosen, a
  // translation target, a voice. Not part of the edit - undo does not reopen a
  // panel - but part of what the user set up, so it is saved with the project and
  // mirrored into the session database as a per-machine default.
  Q_PROPERTY(QVariantMap uiState READ uiState NOTIFY uiStateChanged)
  Q_PROPERTY(QString mediaCachePath READ mediaCachePath CONSTANT)
  // Measured off the GUI thread, so the first read of either of these reports
  // progress rather than an answer. Generated subtitle voices are counted in the
  // total, and there are tens of thousands of those files.
  Q_PROPERTY(QString mediaCacheSize READ mediaCacheSize NOTIFY mediaCacheChanged)
  Q_PROPERTY(QString mediaCacheDetail READ mediaCacheDetail NOTIFY
                 mediaCacheChanged)
  Q_PROPERTY(bool mediaCacheBusy READ mediaCacheBusy NOTIFY mediaCacheChanged)
  Q_PROPERTY(QString previewFrameUrl READ previewFrameUrl NOTIFY
                 previewFrameChanged)
  // Bumped for every new decoded/scrubbed frame and on grade changes - the same
  // counter previewFrameUrl carries. The Scopes panel binds its image URL to this
  // so a new frame re-fetches the scope, without reusing the monitor's own URL.
  Q_PROPERTY(qulonglong previewFrameRevision READ previewFrameRevision NOTIFY
                 previewFrameChanged)
  Q_PROPERTY(bool previewDecoding READ previewDecoding NOTIFY
                 previewStateChanged)
  Q_PROPERTY(QString previewError READ previewError NOTIFY previewErrorChanged)
  // The monitor's Lumetri grade, as shader state. All four change together and
  // only when the grade itself changes, so one notification covers them.
  Q_PROPERTY(QVariantMap lumetriUniforms READ lumetriUniforms NOTIFY
                 lumetriGradeChanged)
  Q_PROPERTY(QString lumetriCurveUrl READ lumetriCurveUrl NOTIFY
                 lumetriGradeChanged)
  Q_PROPERTY(QString lumetriCubeUrl READ lumetriCubeUrl NOTIFY
                 lumetriGradeChanged)
  Q_PROPERTY(bool gpuGrading READ gpuGrading NOTIFY lumetriGradeChanged)
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
  QString workspaceLocation() const;
  void setWorkspaceLocation(const QString &path);
  QString sequenceId() const { return m_sequenceId; }
  QString sequenceName() const { return m_sequenceName; }
  QString activeWorkspace() const { return m_activeWorkspace; }
  bool licensed() const { return m_licensed; }
  QString machineId() const;
  QString layoutPreset() const { return m_layoutPreset; }
  QString captionFontFamily() const { return m_captionStyle.fontFamily; }
  QStringList downloadedCaptionFonts() const { return m_downloadedCaptionFonts; }
  QStringList customCaptionFonts() const { return m_customCaptionFonts; }
  QVariantList captionFontModel() const;
  QVariantList googleFontsCatalog() const { return m_googleFontsCatalog; }
  bool googleFontsLoading() const { return m_googleFontsLoading; }
  QString googleFontsError() const { return m_googleFontsError; }
  QStringList previewedCaptionFonts() const { return m_previewedCaptionFonts; }
  int captionFontSize() const { return m_captionStyle.fontSize; }
  double captionBoxWidth() const { return m_captionStyle.boxWidth; }
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
  QString captionTextEffect() const { return m_captionStyle.textEffect; }
  QString captionAnimation() const { return m_captionStyle.animation; }
  double captionAnimationSpeed() const { return m_captionStyle.animationSpeed; }
  QVariantList textEffectStyles() const;
  QVariantList textEffectAnimations() const;
  // The letter recipe for an id, so the gallery card and the monitor overlay
  // read their numbers from the same table the export renderer does.
  Q_INVOKABLE QVariantMap textEffectStyleFor(const QString &id) const;
  // Per-glyph state at `elapsedMs` into a cue - the whole animation, solved in
  // C++ and drawn verbatim by QML. Passing the ids in lets a gallery card
  // preview an animation the project has not selected yet; the project's own
  // animation and speed are the defaults. `wordTimes` is the cue's spoken word
  // timing, which only the karaoke animations read; a card that has no cue
  // behind it leaves it out and gets the estimate.
  Q_INVOKABLE QVariantMap solveCaptionAnimation(const QString &text,
                                                double elapsedMs,
                                                double cueDurationMs,
                                                const QString &animationId = {},
                                                double speed = 0.0,
                                                const QString &wordTimes = {}) const;
  // The words and glyphs an animation splits `text` into, with no time in it.
  // A renderer builds its items from this once and then reads the numbers above
  // per frame by index, which is the difference between updating a caption and
  // rebuilding every letter of it thirty times a second.
  //
  // Passing the font asks for each word's width in it as well. A renderer needs
  // those to break a line and then align it, and it cannot measure them itself:
  // QML can only measure a string by writing into a TextMetrics and reading it
  // back, which inside the binding that lays the words out is a binding loop.
  Q_INVOKABLE QVariantMap captionGlyphLayout(const QString &text,
                                             const QString &animationId = {},
                                             const QString &fontFamily = {},
                                             int pixelSize = 0,
                                             bool bold = false,
                                             bool italic = false) const;
  QString activeCaptionWordTimes() const { return m_activeCaptionWordTimes; }
  QVariantList media() const { return m_media; }
  QVariantList clips() const { return m_clips; }
  int clipCount() const { return int(m_clips.size()); }
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
  int subtitleTrackCount() const;
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
    return m_exportActive || m_exportProcess.state() != QProcess::NotRunning;
  }
  double exportProgress() const { return m_exportProgress; }
  QString exportStatus() const { return m_exportStatus; }
  double exportFramesPerSecond() const { return m_exportFinalizing ? 0 : m_exportEstimate.speed() * m_exportSettings.value("frameRate", 30).toDouble(); }
  double exportSpeed() const { return m_exportEstimate.speed(); }
  double exportBytesPerSecond() const { return m_exportMetrics.bytesPerSecond; }
  qint64 exportRemainingSeconds() const { return m_exportRemainingSeconds; }
  qint64 exportElapsedSeconds() const { return (m_exportActive && m_exportElapsed.isValid() ? m_exportElapsed.elapsed() : m_exportElapsedMs) / 1000; }
  QString exportEncoder() const { return m_exportEncoder; }
  QString exportEncoderFallback() const { return m_exportEncoderFallback; }
  bool exportFinalizing() const { return m_exportFinalizing; }
  bool exportRemainingIncludesFinalization() const { return m_exportChunks.isEmpty() || m_exportEstimate.includesFinalization(); }
  QVariantList transcript() const { return m_transcript; }
  int transcriptCount() const { return int(m_transcript.size()); }
  bool transcriptionInProgress() const {
    return m_transcriptionProcess.state() != QProcess::NotRunning || m_transcriptionBatch.inProgress();
  }
  QVariantList transcriptionBatchJobs() const { return m_transcriptionBatch.jobs(); }
  QString transcriptionStatus() const { return m_transcriptionStatus; }
  double transcriptionProgress() const { return m_transcriptionProgress; }
  // A run with no measurable progress yet: in progress, no windows planned, and
  // no percentage in hand. The dashboard shows a busy sweep for this instead of a
  // frozen 0% bar - the case for one-pass Gemini, which streams no progress.
  bool transcriptionIndeterminate() const {
    return transcriptionInProgress() && m_transcriptionWindowCount <= 0 &&
           m_transcriptionProgress <= 0.0;
  }
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
  QVariantList translationPreview() const { return m_translator.preview(); }
  int translationCompletedSegments() const { return m_translator.completedSegments(); }
  int translationTotalSegments() const { return m_translator.totalSegments(); }
  bool translationModelsInProgress() const {
    return m_translationModelProbe.inProgress();
  }
  QString translationModelsStatus() const {
    return m_translationModelProbe.status();
  }
  bool recapInProgress() const { return m_recap.inProgress(); }
  QString recapStatus() const { return m_recap.status(); }
  QString recapText() const { return m_recap.recap(); }
  bool sceneDetectInProgress() const {
    return m_sceneBatchActive || m_sceneProcess.state() != QProcess::NotRunning;
  }
  QString sceneDetectStatus() const { return m_sceneStatus; }
  double sceneDetectProgress() const { return m_sceneProgress; }
  bool demucsInProgress() const {
    return m_demucsProcess.state() != QProcess::NotRunning;
  }
  double demucsProgress() const { return m_demucsProgress; }
  QString demucsStatus() const { return m_demucsStatus; }
  QVariantList systemRequirements() const { return m_systemRequirements; }
  bool requirementsChecking() const { return m_requirementsChecking; }
  bool requirementsSatisfied() const;
  // Probes the machine for the Python runtime, edge-tts, ffmpeg and the optional
  // offline engines, then refreshes systemRequirements. Runs asynchronously; the
  // navbar calls it on startup and from the dependency panel's Recheck button.
  Q_INVOKABLE void checkSystemRequirements();
  QString installingRequirement() const { return m_installingId; }
  QString requirementInstallStatus() const { return m_installStatus; }
  // Pip-installs an optional engine (id "whisper" or "demucs") into the system
  // Python, streaming progress into requirementInstallStatus and rechecking when
  // it finishes. A no-op while an install is already running.
  Q_INVOKABLE void installRequirement(const QString &id);
  QString lastError() const { return m_lastError; }
  QVariantMap colorSettings() const { return m_colorSettings; }
  QVariantMap activeColorClip() const;
  QVariantMap activeColorMedia() const;
  QString selectedClipId() const { return m_selectedClipId; }
  QVariantMap selectedClip() const;
  QVariantList effectDefinitions() const;
  QVariantList effectPresets() const;
  QVariantList lumetriPresets() const;
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
  bool timelineImportActive() const { return m_ttsImportActive; }
  QString customBlurEditClipId() const { return m_customBlurEditClipId; }
  QString customBlurEditInstanceId() const {
    return m_customBlurEditInstanceId;
  }
  QVariantMap appSettings() const { return m_appSettings; }
  QVariantMap uiState() const { return m_uiState; }
  QString mediaCachePath() const;
  QString mediaCacheSize() const;
  QString mediaCacheDetail() const;
  bool mediaCacheBusy() const { return m_mediaCacheClearing; }
  QString previewFrameUrl() const;
  qulonglong previewFrameRevision() const { return m_previewFrameRevision; }
  bool previewDecoding() const {
    return m_previewDecoder.running() || m_scrubFrames.busy();
  }
  QString previewError() const;
  QImage previewFrameImage() const;
  // What the monitor is actually shown: previewFrameImage() with the clip's
  // Lumetri chain applied on the CPU. The image provider calls this; nothing else
  // should, because the graded copy is cached against the frame revision and the
  // grade generation and would otherwise be recomputed per caller.
  //
  // Only reached when gpuGrading() is false. With a working scene graph the same
  // chain runs in the fragment stage instead - see lumetriUniforms().
  QImage gradedPreviewFrameImage() const;

  // A graded, downscaled RGB copy of the on-screen frame, for the Lumetri Scopes.
  // Unlike gradedPreviewFrameImage() this works on both backends: it applies the
  // cached CPU correction (m_previewGrade) instead of relying on the GPU shader
  // chain, and never touches the monitor's own graded-frame cache. maxEdge caps
  // the long edge before grading - the scopes only need a sample, so a full-res
  // grade per frame would be wasted work on the GUI thread.
  QImage scopeSourceImage(int maxEdge) const;

  // The Lumetri chain as fragment-shader uniforms, keyed by the uniform-block
  // member names in shaders/lumetri_*.frag. QML binds a ShaderEffect property per
  // entry, so a slider drag becomes a uniform write and not a re-grade.
  QVariantMap lumetriUniforms() const { return m_lumetriUniforms; }
  // The two lookup tables behind the same chain, as image provider URLs, empty
  // when the chain does not use them. Both carry the grade generation so a new
  // table is a new URL and QML cannot keep the old texture.
  QString lumetriCurveUrl() const;
  QString lumetriCubeUrl() const;
  // 256x1 RGBA8: R/G/B are the three curve tables, A is eq's tone table.
  QImage lumetriCurveTexture() const;
  // The input .cube unrolled to a (n*n)x(n) strip of blue slices, RGBA8. Null
  // when the clip has no input LUT.
  QImage lumetriCubeTexture() const;
  // False when the scene graph cannot run a ShaderEffect at all (the software
  // backend), which is the one case the CPU chain is still needed for. Set once
  // from main.cpp after the window exists.
  bool gpuGrading() const { return m_gpuGrading; }
  void setGpuGrading(bool enabled);

  void setProjectName(const QString &name);
  void setSequenceName(const QString &name);
  void setActiveWorkspace(const QString &workspace);
  void setLayoutPreset(const QString &preset);
  void setCaptionFontFamily(const QString &family);
  void setCaptionFontSize(int size);
  void setCaptionBoxWidth(double fraction);
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
  void setCaptionTextEffect(const QString &id);
  void setCaptionAnimation(const QString &id);
  void setCaptionAnimationSpeed(double speed);
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
  // Append several media items to the timeline in one undoable edit batch
  // (one rememberState()/one clipsChanged) instead of looping addClip() in QML.
  // Returns the ids of the clips actually created (rejected items are skipped).
  Q_INVOKABLE QStringList addClips(const QStringList &mediaIds);
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
  // Explicit selection commands keep single-clip engine callers unchanged.
  Q_INVOKABLE QStringList editableClipIds(const QStringList &ids,
                                         const QString &mediaType = QString()) const;
  Q_INVOKABLE void setEffectEditSelection(const QStringList &ids);
  QStringList effectEditSelection() const { return m_effectEditSelection; }
  Q_INVOKABLE QStringList effectEditTargets(const QString &primary) const;
  Q_INVOKABLE QVariantMap applyClipSelectionCommand(
      const QStringList &ids, const QString &command, const QVariantMap &args,
      const QString &primary = QString());
  Q_INVOKABLE QVariantMap applyEffectSelectionCommand(
      const QString &primary, const QString &command, const QVariantMap &args);
  Q_INVOKABLE bool editSelectedClipEffectSetting(const QString &clipId,
      const QString &key, const QVariant &value) {
    return applyEffectSelectionCommand(clipId, "setting", {{"key", key}, {"value", value}}).value("changed").toBool();
  }
  Q_INVOKABLE bool resetSelectedClipEffectSettings(const QString &clipId) {
    return applyEffectSelectionCommand(clipId, "reset", {}).value("changed").toBool();
  }
  Q_INVOKABLE bool editSelectedClipEffectParameter(const QString &clipId,
      const QString &instanceId, const QString &key, const QVariant &value) {
    return applyEffectSelectionCommand(clipId, "parameter", {{"instanceId", instanceId}, {"key", key}, {"value", value}}).value("changed").toBool();
  }
  Q_INVOKABLE bool editSelectedClipEffectEnabled(const QString &clipId,
      const QString &instanceId, bool enabled) {
    return applyEffectSelectionCommand(clipId, "enableEffect", {{"instanceId", instanceId}, {"enabled", enabled}}).value("changed").toBool();
  }
  Q_INVOKABLE bool moveSelectedClipEffect(const QString &clipId,
      const QString &instanceId, int offset) {
    return applyEffectSelectionCommand(clipId, "moveEffect", {{"instanceId", instanceId}, {"offset", offset}}).value("changed").toBool();
  }
  Q_INVOKABLE bool removeSelectedClipEffect(const QString &clipId,
      const QString &instanceId) {
    return applyEffectSelectionCommand(clipId, "removeEffect", {{"instanceId", instanceId}}).value("changed").toBool();
  }
  Q_INVOKABLE bool resetSelectedClipEffectInstance(const QString &clipId,
      const QString &instanceId) {
    return applyEffectSelectionCommand(clipId, "resetInstance", {{"instanceId", instanceId}}).value("changed").toBool();
  }
  Q_INVOKABLE bool editSelectedClipColorSetting(const QString &clipId,
      const QString &key, const QVariant &value) {
    return applyEffectSelectionCommand(clipId, "color", {{"key", key}, {"value", value}}).value("changed").toBool();
  }
  Q_INVOKABLE bool editSelectedCustomBlurMask(const QString &clipId,
      const QString &instanceId, double x, double y, double width, double height) {
    return applyEffectSelectionCommand(clipId, "mask", {{"instanceId", instanceId},
        {"x", x}, {"y", y}, {"width", width}, {"height", height}}).value("changed").toBool();
  }
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
  // Where the hole under the pointer starts and ends, or {found: false} if that
  // spot is on a clip or past the last one. The menu needs the bounds to say how
  // long the gap is before the user commits to closing it.
  Q_INVOKABLE QVariantMap gapAt(const QString &track, qint64 positionMs) const;
  // Every gap on one track, in one undo step. Returns how many were closed.
  Q_INVOKABLE int closeAllGaps(const QString &track);
  // Detect the silent stretches inside one clip and ripple them out in a single
  // undo step, keeping any linked A/V partner in sync. `thresholdDb` is the
  // level below which a stretch counts as silent (e.g. -32), `minSilenceMs` the
  // shortest gap worth cutting, and `paddingMs` a breath left at each edge so
  // speech is not clipped. Returns the number of stretches removed, 0 if none
  // were found, or a negative code: -1 no audio to analyse, -2 the clip's used
  // span is too long to scan on the spot.
  //
  // This is the one-shot path, kept for callers that want analyse-and-apply in
  // a single blocking call. The reviewable path is analyzeSilence() followed by
  // applySilenceRemoval(), which is what the dialog drives.
  Q_INVOKABLE int removeSilence(const QString &clipId, double thresholdDb = -32.0,
                                qint64 minSilenceMs = 500,
                                qint64 paddingMs = 100);

  // --- Silence analysis, reviewable ---------------------------------------
  // Scanning a clip's audio decodes it tile by tile, which is far too slow for
  // the GUI thread on anything longer than a few minutes, so the scan runs on a
  // worker and reports progress. The result is a candidate list the user can see
  // and adjust before anything is cut - this used to be a one-click destructive
  // action whose only recourse was Ctrl+Z.
  Q_PROPERTY(bool silenceScanInProgress READ silenceScanInProgress NOTIFY
                 silenceScanChanged)
  Q_PROPERTY(double silenceScanProgress READ silenceScanProgress NOTIFY
                 silenceScanChanged)
  Q_PROPERTY(QString silenceScanStatus READ silenceScanStatus NOTIFY
                 silenceScanChanged)
  // One entry per candidate: {startMs, endMs, durationMs} in timeline time.
  Q_PROPERTY(QVariantList silenceCandidates READ silenceCandidates NOTIFY
                 silenceCandidatesChanged)
  Q_PROPERTY(QString silenceCandidateClipId READ silenceCandidateClipId NOTIFY
                 silenceCandidatesChanged)

  bool silenceScanInProgress() const { return m_silenceScanning || m_silenceBatchActive; }
  double silenceScanProgress() const { return m_silenceProgress; }
  QString silenceScanStatus() const { return m_silenceStatus; }
  QVariantList silenceCandidates() const { return m_silenceCandidates; }
  QString silenceCandidateClipId() const { return m_silenceClipId; }

  // Start a scan. `useAverage` measures each grid cell as the mean of the peak
  // columns covering it instead of their maximum, which is what you want for
  // breaths and room tone: a single click or keyboard tap is enough to hold a
  // whole cell above a peak threshold. (It is a mean of peaks, not true RMS -
  // the decoder hands back an envelope, not samples.)
  Q_INVOKABLE bool analyzeSilence(const QString &clipId, double thresholdDb,
                                  qint64 minSilenceMs, qint64 paddingMs,
                                  bool useAverage);
  Q_INVOKABLE void cancelSilenceScan();
  Q_INVOKABLE bool analyzeClipsSilence(const QStringList &ids, double thresholdDb,
                                      qint64 minSilenceMs, qint64 paddingMs,
                                      bool useAverage = true);
  Q_INVOKABLE int applyClipsSilenceRemoval();
  // Ripple out the candidates found by the last scan. Returns the number of
  // stretches removed, or a negative code as removeSilence does.
  Q_INVOKABLE int applySilenceRemoval(const QString &clipId);
  Q_INVOKABLE void clearSilenceCandidates();
  // Total timeline time the current candidates would remove.
  Q_INVOKABLE qint64 silenceCandidateTotalMs() const;
  // The whole grade in one write: a Lumetri preset is a map of the same keys
  // setClipColorSetting takes, and applying it key by key from QML would cost
  // one undo entry and one full clip rewrite per key.
  Q_INVOKABLE bool applyClipColorPreset(const QString &clipId,
                                        const QVariantMap &settings);
  Q_INVOKABLE QString addMarker(qint64 positionMs,
                                const QString &name = QString(),
                                const QString &color = QString());
  Q_INVOKABLE bool updateMarker(const QString &markerId, qint64 positionMs,
                                const QString &name, const QString &color);
  Q_INVOKABLE bool removeMarker(const QString &markerId);
  Q_INVOKABLE void setSnappingEnabled(bool enabled);
  Q_INVOKABLE bool saveProject(const QString &path = QString());
  Q_INVOKABLE bool loadProject(const QString &path);

  // ---- Workspace / home screen -------------------------------------------
  // The projects visible on the home screen: every *.cutpro.json directly in
  // the workspace folder, newest first. Each entry is
  // {name, path, modified (ISO), modifiedLabel, sizeBytes}. Cheap enough to
  // call on every home-screen refresh - it stats a directory, it does not open
  // any project.
  Q_INVOKABLE QVariantList listWorkspaceProjects();
  // Create a named project in the workspace and save it straight away, so it
  // has a real .cutpro.json from the first frame and auto-save has a target.
  // Returns the project's file path, or an empty string on failure (name
  // clash, unwritable folder). Switches the app into the Edit workspace.
  Q_INVOKABLE QString createWorkspaceProject(const QString &name);
  // Open a workspace project by its .cutpro.json path and switch to Edit.
  Q_INVOKABLE bool openWorkspaceProject(const QString &path);
  // Delete a project and its siblings (.sqlite mirror, .autosave.cutpro.json).
  Q_INVOKABLE bool deleteWorkspaceProject(const QString &path);
  // Rename a project file (and its siblings) within the workspace. Returns the
  // new .cutpro.json path, or empty on failure.
  Q_INVOKABLE QString renameWorkspaceProject(const QString &path,
                                             const QString &newName);
  // Pick a new workspace folder from QML (writes the setting through).
  Q_INVOKABLE void chooseWorkspaceLocation(const QString &path);
  Q_INVOKABLE QVariantMap probeMedia(const QString &path,
                                     bool generateDetailedPreviews = true);
  Q_INVOKABLE bool startExport(const QString &outputPath,
                               const QString &preset = QStringLiteral("high"));
  Q_INVOKABLE bool startExportWithSettings(const QString &outputPath,
                                           const QVariantMap &settings);
  Q_INVOKABLE bool downloadCaptionFont(const QString &family);
  // Font library management (implemented in backend_fonts.cpp). Import copies the
  // chosen files into the app font folder and registers them, so they survive a
  // restart and show up everywhere a caption font is picked. URLs are the
  // file:/// strings QML's FileDialog/FolderDialog hand back.
  Q_INVOKABLE int importCaptionFontFiles(const QStringList &fileUrls);
  Q_INVOKABLE int importCaptionFontFolder(const QString &folderUrl);
  Q_INVOKABLE bool removeCustomCaptionFont(const QString &family);
  // Google Fonts browser: fetchGoogleFontsCatalog() pulls the whole library once
  // (key-free, from fonts.google.com/metadata/fonts) so the settings page can
  // search it; downloadGoogleFont() fetches one chosen family on demand and
  // registers it like any other caption font. removeDownloadedCaptionFont()
  // unregisters and deletes a downloaded cloud font again.
  Q_INVOKABLE void fetchGoogleFontsCatalog();
  Q_INVOKABLE bool downloadGoogleFont(const QString &family);
  // Fetch a throwaway copy of a family into the cache and register it so the
  // browser can render its shape, without installing or persisting it.
  Q_INVOKABLE void previewGoogleFont(const QString &family);
  Q_INVOKABLE bool removeDownloadedCaptionFont(const QString &family);
  // Full catalog for the Fonts settings page: {family, category, available,
  // removable, downloadable} for system, cloud and custom fonts.
  Q_INVOKABLE QVariantList captionFontCatalog() const;
  Q_INVOKABLE QString suggestedExportPath() const;
  // The sequence's own picture: resolution and frame rate taken from the
  // earliest visual clip, which is what "Match Source" means. Returns
  // {width, height, frameRate, durationMs, hasVideo}.
  Q_INVOKABLE QVariantMap sequenceSourceInfo() const;
  // Estimated video bitrate and file size for a given export settings map,
  // as {videoMbps, sizeMb}. Lives here rather than in a view so Quick Export
  // and the full Export page cannot quote different numbers for the same
  // render - two estimates that disagree are worse than one that is only
  // approximate.
  Q_INVOKABLE QVariantMap exportEstimate(const QVariantMap &settings) const;
  Q_INVOKABLE void cancelExport();
  Q_INVOKABLE bool
  transcribeMedia(const QString &mediaId,
                  const QString &model = QStringLiteral("tiny"),
                  const QString &language = QStringLiteral("auto"));
  Q_INVOKABLE bool transcribeMediaBatch(const QStringList &mediaIds,
                                      const QString &model, const QString &language);
  Q_INVOKABLE void cancelTranscription();
  Q_INVOKABLE void cancelDemucs();
  Q_INVOKABLE bool importSubtitles(const QString &path);
  Q_INVOKABLE bool exportTranscriptSrt(const QString &path);
  // TTML, the format YouTube accepts as a caption upload and hands back on
  // download. SRT stays the default everywhere else; this is the round trip.
  Q_INVOKABLE bool exportTranscriptTtml(const QString &path);
  Q_INVOKABLE bool updateTranscriptSegment(int index, const QString &text);
  // Drops one line for good: out of the transcript, out of the per-source list it
  // was derived from so a rebuild cannot bring it back, and out of the timeline if
  // it had a subtitle clip.
  Q_INVOKABLE bool deleteTranscriptSegment(int index);
  // Text-based editing: remove the *footage* behind a run of transcript lines,
  // not just the lines. Maps rows [startIndex, endIndex] to the timeline span
  // they cover, cuts that span out of the media clips under it, and ripples
  // everything after it left to close the gap - so deleting a sentence in the
  // transcript deletes that piece of video/audio and tightens the sequence.
  // The transcript view re-derives itself from the new clips. One undo step.
  // Returns the number of milliseconds removed, or 0 if nothing was cut.
  Q_INVOKABLE qint64 deleteTranscriptRange(int startIndex, int endIndex);
  Q_INVOKABLE bool addTranscriptToTimeline();
  Q_INVOKABLE bool removeTranscriptFromTimeline();
  // Drops a free-standing text overlay onto the subtitle track at startMs (the
  // playhead when -1). It is an ordinary subtitle clip with transcriptIndex -1,
  // so the monitor and the exporter already know how to draw and burn it in -
  // it just is not tied to a spoken line. Returns the new clip id, or empty.
  Q_INVOKABLE QString addTextClip(qint64 startMs = -1,
                                  const QString &text = QString());
  // Retitles a text overlay in place, by clip id. The timeline row, the monitor
  // and the export all read the same "text" field, so one write updates them
  // together.
  Q_INVOKABLE bool updateTextClip(const QString &clipId, const QString &text);
  // The monitor re-seeks the picture on every clipsChanged. A live text edit
  // republishes the timeline on each keystroke but never changes the frame under
  // the playhead, so the monitor asks this once per clipsChanged and skips the
  // decode when it answers true - which is what stops the picture flickering
  // while typing. It clears itself, so an ordinary edit still seeks.
  Q_INVOKABLE bool takePreviewSeekSuppression();
  Q_INVOKABLE bool translateTranscript(const QString &targetLanguage,
                                       const QVariantMap &settings = {});
  Q_INVOKABLE bool testTranslationProvider(const QVariantMap &settings = {});
  // Asks the configured endpoint for its model catalogue. Answers through
  // translationModelsChecked(); the settings map is the settings dialog's draft,
  // so the URL and keys being typed are the ones checked.
  Q_INVOKABLE bool checkTranslationModels(const QVariantMap &settings = {});
  Q_INVOKABLE void cancelTranslationModelCheck();
  Q_INVOKABLE void cancelTranslation();
  // Generate a narrator recap of the current transcript with Gemini. Result lands
  // in recapText; does not touch the transcript itself.
  Q_INVOKABLE bool generateRecap(const QVariantMap &settings = {});
  Q_INVOKABLE void cancelRecap();
  Q_INVOKABLE bool saveRecapText(const QString &path, const QString &text);
  // The current transcript as timestamped "MM:SS.d  text" lines, for the recap
  // dialog's "Load subtitle" preview. Defined in backend_recap.cpp.
  Q_INVOKABLE QString transcriptText() const;
  // Replace the entire subtitle with a recap: split the prose into sentence-sized
  // cues, spread them evenly across the transcript's full time span, and make them
  // the new (standalone) transcript and subtitle track. Defined in
  // backend_recap.cpp.
  Q_INVOKABLE bool replaceTranscriptWithRecap(const QString &recapText);
  // Remembers one piece of interface state - a tab, a combo box, a toggle - under
  // a name of the caller's choosing. Written to the project state and to the
  // session database, so it comes back both with the project and on the next
  // launch of an unsaved one.
  Q_INVOKABLE bool setUiState(const QString &key, const QVariant &value);
  Q_INVOKABLE QVariant uiStateValue(const QString &key,
                                    const QVariant &fallback = {}) const;
  // An entry in the action log for something only the interface knows happened:
  // a panel opened, a dialog accepted, a button pressed that changes no clip.
  // Edits log themselves through rememberState().
  Q_INVOKABLE void logUiAction(const QString &type,
                               const QVariantMap &payload = {});
  // The log back out, newest first: [{type, payload, createdAt}, ...].
  Q_INVOKABLE QVariantList recentActions(int limit = 200);
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

  // ---- Premiere-style clip operations (defined in backend_clip_ops.cpp) ----
  // Retime one clip. speedPercent 100 = no change. The used span is recomputed
  // from the untouched source span (durationMs = sourceDurationMs*100/speed), so
  // repeated speed changes never compound. maintainPitch and timeInterpolation
  // are stored on the clip's effects and read by the export builder. When
  // rippleTrailing is true, later clips on the same track slide by the change in
  // length so no gap or overlap is left behind.
  Q_INVOKABLE bool setClipSpeed(const QString &clipId, double speedPercent,
                                bool maintainPitch = true,
                                const QString &timeInterpolation =
                                    QStringLiteral("sampling"),
                                bool rippleTrailing = false);
  Q_INVOKABLE bool setClipEnabled(const QString &clipId, bool on);
  Q_INVOKABLE bool toggleClipsEnabled(const QStringList &clipIds);
  Q_INVOKABLE bool setClipFieldOptions(const QString &clipId,
                                       const QString &fieldMode,
                                       bool reverseFieldDominance);
  // Group: a soft selection link (moving/deleting one selects the group). Link:
  // the existing A/V link (linkGroupId) generalised to any selection.
  Q_INVOKABLE bool groupClips(const QStringList &clipIds);
  Q_INVOKABLE bool ungroupClips(const QStringList &clipIds);
  Q_INVOKABLE bool linkClips(const QStringList &clipIds);
  Q_INVOKABLE bool unlinkClips(const QStringList &clipIds);
  // Clipboard: copy/cut fill an in-memory buffer; paste clones the buffer at
  // atMs on the given track (empty track = each clip's own track). Paste
  // Attributes copies effects+lumetri+speed from the single copied clip onto the
  // targets.
  Q_INVOKABLE bool copyClips(const QStringList &clipIds);
  Q_INVOKABLE bool cutClips(const QStringList &clipIds);
  Q_INVOKABLE QStringList pasteClips(const QString &track = QString(),
                                     qint64 atMs = -1);
  Q_INVOKABLE bool pasteClipAttributes(const QStringList &targetClipIds);
  Q_INVOKABLE bool clipboardHasClips() const { return !m_clipClipboard.isEmpty(); }
  Q_INVOKABLE bool setClipLabel(const QStringList &clipIds,
                                const QString &colorToken);
  // Scene Edit Detection: score the clip's trimmed range for hard cuts with the
  // bundled ffmpeg and, on finish, split the clip and/or drop a marker at each
  // detected cut. threshold ~0.3-0.5 (higher = fewer cuts). Asynchronous;
  // completion arrives on sceneDetectFinished.
  //
  // `minShotMs` discards a cut that falls too soon after the one before it. A
  // flashy or shaky source otherwise yields thousands of detections, and each
  // one becomes a sliver clip - so the guard is what keeps the feature usable on
  // real footage rather than a way to tune taste.
  Q_INVOKABLE bool detectSceneEdits(const QString &clipId, double threshold,
                                    bool applyCuts, bool createMarkers,
                                    qint64 minShotMs = 1000);
  Q_INVOKABLE void cancelSceneDetect();
  Q_INVOKABLE bool detectClipsSceneEdits(const QStringList &ids, double threshold,
                                       bool applyCuts, bool createMarkers,
                                       qint64 minShotMs);

  Q_INVOKABLE bool generateTimedTextToSpeech(const QVariantList &segments,
                                             const QString &language,
                                             const QString &gender,
                                             // The cap the user chose. 1.0 - the
                                             // default - is a natural-pace dub that
                                             // spills onto A2 and below wherever a
                                             // line runs long; above it each line is
                                             // sped up by only what it needs so the
                                             // dub fits back onto one track.
                                             double maxSpeed = 1.0,
                                             // A uniform pace for every line. 1.0 is
                                             // off (maxSpeed decides); above 1.0 every
                                             // voice is re-timed to exactly this rate.
                                             double fixedSpeed = 1.0);
  // What the last voiceover did to the timeline: which voices play over another
  // voice, and which ones are longer than the room before the next line. Built
  // fresh from the clips each time it is asked for, so it is right after an undo,
  // a manual trim or a reopened project. See speech_report.h for the shape.
  Q_INVOKABLE QVariantMap generatedSpeechReport() const;
  // Superseded by chainGeneratedSpeech below and no longer called from the UI. It
  // slides a voice later only while that keeps it within `maxLateSeconds` of its own
  // subtitle; past that bound the voice goes back to its cue time and is stacked on
  // the next free lane, which is audible overlap - the very thing the chain exists to
  // stop. Kept only as a reference implementation of a bounded-lateness layout.
  // Returns the summary described in speech_fit.h.
  Q_INVOKABLE QVariantMap fitGeneratedSpeech(double maxLateSeconds);
  // Lays the generated voices out as one sequential chain: each one starts at its own
  // cue, or at the end of the voice before it when that one is still talking, so no two
  // voices ever overlap. `gapMs` is breathing room after each voice before its row
  // may be reused; it never moves a voice, every one keeps its cue. Rows are not
  // chosen - they emerge as the deepest stack of voices heard at once. Nothing is
  // re-encoded, so it is instant and undoable. Returns the summary in speech_chain.h.
  Q_INVOKABLE QVariantMap chainGeneratedSpeech(int gapMs = 0);
  // The speed limit a regenerate would need for the dub to end inside the picture,
  // plus how far past it the dub runs as it stands. Reads the timeline only; the
  // caller decides whether to regenerate at the answer. See speech_chain.h.
  Q_INVOKABLE QVariantMap speechSpeedForPicture() const;
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
  // A live effect slider that must stream to the backend for its preview (the
  // custom-blur strength handle re-renders the mask on the C++ side, so it can't
  // hold a QML-only draft the way the Effect Controls sliders do) fires
  // setClipEffectParameter/setClipEffectSetting on every mouse-move. Wrapping the
  // gesture in beginLiveParameterEdit()/endLiveParameterEdit() collapses the undo
  // snapshot to one per gesture instead of one full stateVariant() deep-copy per
  // tick; the projection is already coalesced by publishClipStackUpdate().
  Q_INVOKABLE void beginLiveParameterEdit();
  Q_INVOKABLE void endLiveParameterEdit();
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

  // Keyboard shortcuts. The canonical action table lives in
  // backend_shortcuts.cpp; user overrides ride in appSettings under the
  // "keyboardShortcuts" map, so they persist and honour the Settings
  // OK/Cancel flow. QML binds `sequence` with a read of Backend.appSettings so
  // the binding re-evaluates when a rebind lands (see backend_shortcuts.cpp).
  //
  // shortcut(): the resolved portable-text sequence for an action id (a valid
  //   user override, else the built-in default; "" for an unknown id).
  // shortcutActions(): one row per built-in action for the Shortcuts page,
  //   {id,label,category,sequence,defaultSequence,isCustom}.
  // shortcutConflict(): the label of another action already bound to `sequence`
  //   in a scope that would clash, or "" when the binding is free.
  // matchesShortcut(): true when a pressed key+modifiers equals the resolved
  //   binding for `id` - used by TimelinePanel's Keys handler so single-letter
  //   tool keys stay scoped to the focused timeline.
  // sequenceFromEvent(): canonical portable text for a captured key+modifiers,
  //   or "" for a bare modifier press - used by the Shortcuts page's capture
  //   control so the display, storage and QKeySequence all agree.
  Q_INVOKABLE QString shortcut(const QString &id) const;
  Q_INVOKABLE QVariantList shortcutActions() const;
  Q_INVOKABLE QString shortcutConflict(const QString &id,
                                       const QString &sequence) const;
  Q_INVOKABLE bool matchesShortcut(const QString &id, int key,
                                   int modifiers) const;
  Q_INVOKABLE QString sequenceFromEvent(int key, int modifiers) const;

  // Verify a user-entered license key against this machine and, on success,
  // persist it and flip `licensed`. Returns false (and sets error) otherwise.
  Q_INVOKABLE bool activateLicense(const QString &key);

  // Current license status for the Settings page: keys "licensed" (bool),
  // "perpetual" (bool), "expiryDate" (ISO string, empty if perpetual),
  // "expiryMsecs" (qint64 epoch ms when the license lapses, 0 if perpetual),
  // and "machineId" (string). The countdown is driven in QML off expiryMsecs.
  Q_INVOKABLE QVariantMap licenseInfo() const;
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

  // --- Voice-Over Record ---------------------------------------------------
  // Record the microphone onto an audio track while the sequence plays, so a
  // narration can be performed to picture.
  Q_PROPERTY(bool voiceOverRecording READ voiceOverRecording NOTIFY
                 voiceOverChanged)
  Q_PROPERTY(bool voiceOverBusy READ voiceOverBusy NOTIFY voiceOverChanged)
  Q_PROPERTY(QObject *voiceOverWebcam READ voiceOverWebcam CONSTANT)
  // Seconds left in the 3-2-1 lead-in, or 0 when not counting down.
  Q_PROPERTY(int voiceOverCountdown READ voiceOverCountdown NOTIFY
                 voiceOverChanged)
  Q_PROPERTY(QString voiceOverTrack READ voiceOverTrack NOTIFY voiceOverChanged)
  Q_PROPERTY(QString voiceOverStatus READ voiceOverStatus NOTIFY
                 voiceOverChanged)
  // True while the microphone is open. In hands-free mode the session stays
  // open between lines, so this is not the same as "writing a clip right now" -
  // voiceOverCapturing is.
  Q_PROPERTY(bool voiceOverSessionActive READ voiceOverSessionActive NOTIFY
                 voiceOverChanged)
  // True only while speech is being written to a segment.
  Q_PROPERTY(bool voiceOverCapturing READ voiceOverCapturing NOTIFY
                 voiceOverTickChanged)
  // Where the current segment began on the timeline, and how long it is so far.
  // The timeline draws a growing bar from these rather than resizing a real
  // clip every tick, which would re-project the whole sequence per frame.
  Q_PROPERTY(qint64 voiceOverSegmentStartMs READ voiceOverSegmentStartMs NOTIFY
                 voiceOverTickChanged)
  Q_PROPERTY(qint64 voiceOverSegmentMs READ voiceOverSegmentMs NOTIFY
                 voiceOverTickChanged)
  // How many clips this session has already placed.
  Q_PROPERTY(int voiceOverTakeCount READ voiceOverTakeCount NOTIFY
                 voiceOverChanged)

  bool voiceOverRecording() const { return m_voiceOverRecording; }
  bool voiceOverBusy() const { return m_voiceOverRecording || m_voiceOverPending || m_voiceOverCountdown > 0; }
  QObject *voiceOverWebcam();
  int voiceOverCountdown() const { return m_voiceOverCountdown; }
  QString voiceOverTrack() const { return m_voiceOverTrack; }
  QString voiceOverStatus() const { return m_voiceOverStatus; }
  bool voiceOverSessionActive() const { return m_voiceOverRecording; }
  bool voiceOverCapturing() const;
  qint64 voiceOverSegmentStartMs() const;
  qint64 voiceOverSegmentMs() const;
  int voiceOverTakeCount() const { return m_voiceOverTakeCount; }

  // The available microphones, as {id, description, isDefault} maps. Enumerated
  // on demand rather than at startup: the first touch of QMediaDevices loads the
  // Qt Multimedia backend plugin, which is a documented multi-second stall.
  Q_INVOKABLE QVariantList voiceOverInputDevices();
  // Begin a take on `track` (empty picks a free audio lane) at `startMs`
  // (negative means the playhead). Counts down first when configured, then
  // records until stopVoiceOver().
  Q_INVOKABLE bool startVoiceOver(const QString &track = QString(),
                                  qint64 startMs = -1, bool webcam = false);
  // Stop and commit: the take is imported and placed as one undo step.
  Q_INVOKABLE void stopVoiceOver();
  // Stop and throw the take away, file included.
  Q_INVOKABLE void cancelVoiceOver();
  // Capture peak of each channel, 0..1 linear, read-and-clear - the same shape
  // as audioMeterLevels(). This measures the microphone, not the output: the
  // playback meter is post-fader and reads zero whenever monitoring is muted,
  // which is exactly when a record meter has to work.
  Q_INVOKABLE QVariantList voiceOverMeterLevels();
  Q_INVOKABLE qint64 voiceOverElapsedMs() const;
  // Open the input device early so the first take starts promptly.
  Q_INVOKABLE void warmVoiceOverInput();
  // Open the microphone purely to drive the level meter, so a level can be set
  // before performing rather than discovered afterwards. Superseded
  // automatically when a real take starts.
  Q_INVOKABLE void startVoiceOverMonitor();
  Q_INVOKABLE void stopVoiceOverMonitor();

  // --- Speech noise removal -----------------------------------------------
  Q_PROPERTY(bool noiseRemovalBusy READ noiseRemovalBusy NOTIFY noiseRemovalChanged)
  Q_PROPERTY(double noiseRemovalProgress READ noiseRemovalProgress NOTIFY noiseRemovalChanged)
  Q_PROPERTY(QString noiseRemovalStatus READ noiseRemovalStatus NOTIFY noiseRemovalChanged)
  Q_PROPERTY(QString noiseRemovalPreview READ noiseRemovalPreview NOTIFY noiseRemovalChanged)
  bool noiseRemovalBusy() const { return m_noiseApplying || m_noiseRemoval.busy(); }
  double noiseRemovalProgress() const;
  QString noiseRemovalStatus() const { return m_noiseStatus; }
  QString noiseRemovalPreview() const { return m_noisePreview; }
  Q_INVOKABLE bool previewNoiseRemoval(const QString &clipId, int strength, bool original);
  Q_INVOKABLE bool applyClipsNoiseRemoval(const QStringList &ids, int strength);
  Q_INVOKABLE bool restoreClipsNoiseRemoval(const QStringList &ids);
  Q_INVOKABLE void cancelNoiseRemoval();
  Q_INVOKABLE void stopNoiseRemovalPreview();
  Q_INVOKABLE QString clipReplacementAudioPath(const QVariantMap &clip) const;

  // --- Voice changer ------------------------------------------------------
  // Audible everywhere by rendering, not by filtering. Every audio effect in
  // this app is export-only (AudioEffectPipeline::filters has exactly one
  // caller, the export builder), so a voice built as an effect could not be
  // previewed - which is the whole point of choosing one. Instead a preset is
  // rendered to a .wav and the clip is pointed at it, the way vocal separation
  // already substitutes its processed stem. Preview, playback and export then
  // all agree, with no change to the preview pipeline.
  Q_PROPERTY(bool voiceFxBusy READ voiceFxBusy NOTIFY voiceFxChanged)
  Q_PROPERTY(QString voiceFxStatus READ voiceFxStatus NOTIFY voiceFxChanged)
  // The preset whose sample is currently rendering or playing, so the list can
  // show which row is busy.
  Q_PROPERTY(QString voiceFxPreviewId READ voiceFxPreviewId NOTIFY
                 voiceFxChanged)

  bool voiceFxBusy() const { return m_voiceFxBusy || !m_voiceFxQueue.isEmpty() || m_voiceQueuePumping || m_voiceBatchActive; }
  QString voiceFxStatus() const { return m_voiceFxStatus; }
  QString voiceFxPreviewId() const { return m_voiceFxPreviewId; }

  // {id, name, description} per voice, for the chooser.
  Q_INVOKABLE QVariantList voicePresets() const;
  // Render a few seconds of the clip through a preset and play it back, so a
  // voice can be heard before it is committed to.
  Q_INVOKABLE bool previewVoice(const QString &clipId, const QString &presetId);
  Q_INVOKABLE void stopVoicePreview();
  // Render the clip's whole used span and point the clip at the result.
  Q_INVOKABLE bool applyVoice(const QString &clipId, const QString &presetId);
  Q_INVOKABLE bool applyClipsVoice(const QStringList &ids, const QString &presetId);
  // Point the clip back at the media it had before a voice was applied.
  Q_INVOKABLE bool revertVoice(const QString &clipId);
  Q_INVOKABLE QString clipVoiceId(const QString &clipId) const;
  // The clip the last voice-over take became, so the recorder panel can
  // audition a voice against something the user actually said rather than
  // against nothing.
  Q_PROPERTY(QString voiceOverLastClipId READ voiceOverLastClipId NOTIFY
                 voiceOverChanged)
  QString voiceOverLastClipId() const { return m_voiceOverLastClipId; }
  // Real audio-meter levels: [left, right] peak (0..1) of the program output
  // since the last call, read straight off the PCM being played. The meters poll
  // this on their refresh timer; reading clears it so silence falls to zero.
  Q_INVOKABLE QVariantList audioMeterLevels();
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
  Q_INVOKABLE bool previewFrameMatchesPlayhead() const;
  // Source position of the frame the monitor is actually showing during
  // playback, or -1 before the first one arrives. The monitor's playhead follows
  // this instead of a wall clock started when Play was pressed: the decoder
  // needs to open the container, seek and decode before any picture exists, and
  // counting that time as elapsed playback put the playhead - and with it the
  // time display, the subtitle overlay and the still drawn on pause - ahead of
  // the image.
  Q_INVOKABLE qint64 previewPresentedSourceMs() const;

  // The audio device clock in source milliseconds during playback, or -1 when
  // the clip has no audio (or the sink failed). This is the master clock the
  // decoder slaves frame presentation to, so the monitor drives its playhead
  // from it: the playhead then advances monotonically in the frames' own time
  // base instead of free-running on a wall clock and being pulled back to each
  // new frame's pts - which is what made the playhead jitter backward/forward.
  Q_INVOKABLE qint64 previewAudioClockSourceMs() const;

  // How long ago that picture was handed to QML, in milliseconds, or -1 when
  // there is none. The monitor's UI tick runs every 50 ms and the source
  // publishes a frame every 33-40, so a tick that notices a new picture noticed
  // it up to a frame or two late. Anchoring the playback clock at "now" threw
  // that delay away on every single frame, which is why the playhead sat one or
  // two frames behind the image and the pause handler had to jump forward to
  // catch up. Back-dating the anchor by this age is what removes the gap
  // instead of correcting it after the fact.
  Q_INVOKABLE qint64 previewPresentedAgeMs() const;

  // Render-rate playhead position without notifying the transport's UI tick
  // subscribers. Uses the same clock; paused positions and seeks stay exact.
  Q_INVOKABLE double previewDisplayPlayheadMs();

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
  void workspaceLocationChanged();
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
  void timelineImportActiveChanged();
  void playheadChanged();
  void playingChanged();
  void dirtyChanged();
  void historyChanged();
  void activeWorkspaceChanged();
  void licensedChanged();
  void layoutPresetChanged();
  void captionStyleChanged();
  void captionFontsChanged();
  void googleFontsCatalogChanged();
  void previewedCaptionFontsChanged();
  void activeCaptionWordTimesChanged();
  void exportStateChanged();
  void demucsChanged();
  void systemRequirementsChanged();
  void requirementInstallChanged();
  void demucsFinished(bool success, const QString &clipId);
  void errorChanged();
  void exportFinished(bool success, const QString &outputPath);
  void transcriptionFinished(bool success, const QString &mediaId);
  void transcriptionBatchJobsChanged();
  void transcriptChanged();
  void whisperModelsChanged();
  void translationModelsChanged();
  void recapChanged();
  void sceneDetectChanged();
  // ok, the number of cuts acted on, and an error string when ok is false.
  void sceneDetectFinished(bool ok, int cuts, const QString &error);
  void silenceScanChanged();
  void silenceCandidatesChanged();
  // ok, how many silent stretches were found, and an error when ok is false.
  void silenceScanFinished(bool ok, int found, const QString &error);
  void voiceOverChanged();
  // Fired a few times a second while a session is open, for the growing bar and
  // the elapsed readout. Separate from voiceOverChanged so a high-frequency
  // tick does not re-evaluate every binding that only cares about state.
  void voiceOverTickChanged();
  // One clip placed. clipId is empty when the take could not be added.
  void voiceOverTakePlaced(const QString &clipId, qint64 durationMs);
  void voiceFxChanged();
  void noiseRemovalChanged();
  void noiseRemovalFinished(bool ok, const QString &error);
  void voiceFxFinished(bool ok, const QString &presetId, const QString &error);
  // ok, the clip the take became (empty when it failed or was cancelled), and
  // an error string when ok is false.
  void voiceOverFinished(bool ok, const QString &clipId, const QString &error);
  // ok plus the catalogue the endpoint published, the display name derived from
  // its host, and the URL actually reached (the scheme the user left out filled
  // in). The settings page writes all three back into its draft.
  void translationModelsChecked(bool ok, const QStringList &models,
                                const QString &label, const QString &baseUrl,
                                const QString &message);
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
  void uiStateChanged();
  void mediaCacheChanged();
  void previewFrameChanged();
  void previewStateChanged();
  void previewErrorChanged();
  // The monitor's grade changed: new uniforms, and possibly new LUT textures.
  void lumetriGradeChanged();
  void timelineInteractionChanged();
  void timelinePreviewRevisionChanged();
  void autoSaveCompleted(const QString &path);
  // A user-facing operation (transcription, translation, ...) failed. Carries a
  // short title and the actual reason, so the UI can pop a dialog with the
  // detail rather than letting it fail silently behind a muted status line.
  void operationFailed(const QString &title, const QString &reason);

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
  // A multi-edit action (scene-detect's N cuts/markers, "add selected media"'s N
  // appends) should be one undo step and one projection, not N of each. Between
  // beginEditBatch() and the matching endEditBatch() the undoable mutators skip
  // their own rememberState() and defer clipsChanged/markersChanged: the batch
  // takes a single snapshot up front and emits once (plus timelineChanged) when
  // it unwinds. Nestable; only the outermost pair does the work. The deferral is
  // keyed off m_editBatchDepth, so default (depth 0) behaviour is unchanged.
  void beginEditBatch(bool liveParameter = false);
  void notifyClipEdit(bool payloadOnly = false);
  void endEditBatch();
  // rememberState(), except that between beginLiveParameterEdit() and
  // endLiveParameterEdit() only the first call snapshots: a live slider gets one
  // undo step for the whole drag, not one per mouse-move. Lazy (snapshots on the
  // first actual mutation) so a press that never moves leaves no undo entry.
  void rememberStateForLiveEdit();
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
  // Writes the text of a sequence transcript back into the per-source lists it was
  // derived from. The sequence transcript is a view: rebuildSequenceTranscript()
  // regenerates it from m_sourceTranscripts on every clipsChanged, so a
  // translation or an edit that only reached m_transcript survived exactly until
  // the next timeline change - and "Add to Timeline" is a timeline change. Landing
  // it in the sources is also what makes it survive save, load and undo, because
  // stateVariant() serialises sourceTranscripts. Returns true if anything moved.
  bool writeTranscriptTextToSources(const QVariantList &segments);
  // Pushes the current transcript text onto subtitle clips already on the
  // timeline, matched by the transcriptIndex SubtitleTimeline stamped on them.
  bool applyTranscriptTextToSubtitleClips();
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
  // The longest line the worker may leave intact, from settings. A ceiling only:
  // the boundaries themselves are the pauses in the speech.
  qint64 maxSegmentMs() const;
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
  // Placement of a generated subtitle voice, either as the worker produces it or
  // from the manifest it leaves behind. Both go through the same budgeted drain.
  void prepareTimedSpeechBatch();
  void appendStreamedSpeech(const QVariantList &outputs);
  void closeTimedSpeechStream(bool success, const QString &error);
  void beginTimedSpeechImport(const QVariantList &outputs);
  void importNextTimedSpeechOutput();
  void publishTimedSpeechProgress();
  // Re-lays the generated voices already on the timeline into the sequential,
  // no-overlap chain during a still-running import, so the live preview reads the
  // way the finished dub will instead of the raw cue stack the drain leaves behind.
  void relaySpeechChainForPreview();
  // Cache size and voice-file count, on a pool. Both walks are tens of thousands of
  // stats and the properties they back are read by a settings page as it opens.
  void measureMediaCache() const;
  // Drops the generated subtitle voices from the project itself. Their files are
  // cache and Clear cache deletes them; the clips pointing at those files are not
  // cache, and leaving them behind would give the user a timeline of audio clips
  // whose sources no longer exist - saved into the project database like that.
  void removeGeneratedSpeechFromProject();
  void configureAutoSave();
  // Resolve the workspace folder to one that actually exists and is writable,
  // creating it if need be. If the configured/default location cannot be used
  // (a Documents folder redirected to a OneDrive path that was never synced is
  // the real-world case), it falls back through Videos, the home folder and
  // finally app data, and persists whichever it landed on so the "Saving to"
  // path the user sees is the one projects really go to. Returns empty only if
  // even app data is unwritable.
  QString ensureWorkspaceDir();
  // Watches the Whisper checkpoint folder so downloadedWhisperModels() stops
  // being a launch-time snapshot. A download writes its checkpoint in one rename
  // at the end, but the folder is also touched while the .part file grows, so
  // the re-read is debounced rather than run per notification.
  void configureWhisperCacheWatch();
  // Points the watcher at whichever checkpoint folders exist right now, falling
  // back to a folder's parent while the folder itself is absent.
  void updateWhisperCacheWatchPaths();
  void performAutoSave();
  QString projectDatabasePath() const;
  bool persistProjectDatabase();
  // Opens the database for the current project without writing the project out.
  // persistProjectDatabase() is the same thing plus a full save of every clip,
  // which is not what a combo box changing its value should cost.
  bool openProjectDatabase();
  void scheduleProjectDatabaseSave();
  // Hands the current project state to m_projectSaveService to be written on its
  // own thread. This is what the 250 ms debounce timer fires now: the full
  // DELETE-all + INSERT-all rewrite used to run here on the GUI thread.
  void dispatchAsyncProjectSave();
  // Interface state the session database is holding from an earlier launch. Read
  // once, on the way up, so an unsaved project still opens on the panel, model and
  // voice the user left it on.
  void restoreUiStateFromDatabase();
  void startDeferredMediaPreview(const QVariantMap &media);
  // One large-preview (poster/filmstrip/waveform) job for one source. Launches
  // on the global pool through a self-owned QFutureWatcher that is deleteLater'd
  // when it reports, so several sources are generated at once up to the pool
  // ceiling instead of strictly one at a time.
  void launchLargeMediaPreview(const QVariantMap &media);
  // Pool ceiling: filmstrip/waveform generation is ffmpeg decode (process- and
  // disk-bound), so a quarter of the cores clamped to [1,4] - the same split the
  // import queue uses - keeps a big backfill from thrashing the disk while still
  // beating the old serial-of-one.
  static int maxConcurrentLargePreviews();
  // True when the background preview job still has a poster, filmstrip or
  // waveform left to produce for this media entry.
  static bool needsDeferredPreview(const QVariantMap &item);
  // Starts that job for a source already in the bin, if it is still missing
  // anything. Called when a clip is placed, which is where the artefacts are
  // actually drawn.
  void ensureMediaPreviews(const QString &mediaId);
  // Ceiling on queued (not yet launched) background thumbnail jobs for large
  // media. The queue holds media ids, not full maps, so this can sit high
  // without the memory cost that forced the old 16-map cap: whatever is dropped
  // beyond it is just backfilled lazily when the source is next placed.
  static constexpr int kMaxPendingLargePreviews = 2048;
  void finishDeferredMediaPreview(const QVariantMap &result);
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

  // License state. m_licensed is the primary gate; m_licenseGuard is a
  // checksum shadow re-checked at a second site (backend_license.cpp) so a
  // single patch to the gate does not silently unlock the app.
  bool m_licensed = false;
  quint64 m_licenseGuard = 0;
  bool verifyStoredLicense();
  // Second, independent verification site (called before a high-value action).
  // Recomputes the license state from scratch and, if it disagrees with the
  // in-memory gate, terminates the process rather than proceeding.
  void enforceLicenseGuard();
  QVariantMap m_uiState;
  // Answers for the media cache page. Mutable because the getters are property
  // bindings and the first one to run is what starts the measurement.
  mutable bool m_mediaCacheMeasuring = false;
  mutable bool m_mediaCacheMeasured = false;
  mutable QString m_mediaCacheSizeText;
  mutable int m_generatedSpeechFiles = 0;
  bool m_mediaCacheClearing = false;
  QString m_selectedClipId;
  QString m_customBlurEditClipId;
  QString m_customBlurEditInstanceId;
  CaptionStyle m_captionStyle;
  QStringList m_downloadedCaptionFonts;
  // User-imported fonts: family names in import order, and the file each family
  // lives in / the QFontDatabase application id it was registered under, so a
  // removal can unregister it and delete the file.
  QStringList m_customCaptionFonts;
  QHash<QString, QString> m_customCaptionFontPaths;
  QHash<QString, int> m_captionFontFileIds;
  // Cloud/downloaded fonts (built-in three + anything grabbed from the Google
  // Fonts browser): the file each downloaded family lives in, so it can be
  // removed. The families themselves are tracked in m_downloadedCaptionFonts.
  QHash<QString, QString> m_downloadedFontPaths;
  // The whole Google Fonts library ({family, category}) once fetched, plus the
  // in-flight / error state the settings page shows while it loads.
  QVariantList m_googleFontsCatalog;
  bool m_googleFontsLoading = false;
  QString m_googleFontsError;
  // Families fetched only to preview: registered from a cache copy that is never
  // scanned at startup, so previewing never turns into an install.
  QStringList m_previewedCaptionFonts;
  QNetworkAccessManager m_fontNetwork;
  // Tier two of the caption word clock: the timing of a cue that came without
  // one, read off the audio under it. Deliberately not written into m_clips - a
  // derived value has no business dirtying the project or churning a twenty
  // thousand clip list every time the playhead crosses a cue.
  CaptionSpeechTiming m_captionSpeech{this};
  QString m_activeCaptionWordTimes;
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
  // Parses the scene-score pass's showinfo output and, per the remembered
  // options, splits the clip and/or drops markers at each cut. Wired lazily on
  // the first detectSceneEdits() call.
  void handleSceneDetectFinished(int exitCode, QProcess::ExitStatus status);
  void updateSceneDetectStatus();
  // Appends a batch of freshly probed media to the bin in one step. Emitting
  // per file rebuilt the whole model for every import.
  void appendImportedMedia(const QVariantList &items);
  void configureMediaImportQueue();
  // Font library helpers (backend_fonts.cpp). loadFontLibrary() runs once at
  // startup to register every downloaded and imported font file; registerFontFile
  // adds one file and returns the family it provides (empty on failure).
  void loadFontLibrary();
  QString registerFontFile(const QString &path);
  QSet<QString> mediaDuplicateKeys() const;
  MediaImportQueue m_mediaImportQueue;
  bool m_mediaImportRemembered = false;
  // Sources whose large-preview job is running right now, bounded by
  // maxConcurrentLargePreviews(). m_pendingLargeMediaPreviews is the ordered
  // backlog (ids), m_pendingLargeMediaPreviewSet its O(1) membership mirror.
  QSet<QString> m_largeMediaPreviewInFlight;
  QStringList m_pendingLargeMediaPreviews;
  QSet<QString> m_pendingLargeMediaPreviewSet;
  // Coalesces the mediaChanged()/timelineChanged() storm the deferred-preview
  // backfill would otherwise raise. Each preview job (poster, filmstrip,
  // waveform) finishes one source at a time and serially kicks the next, so a
  // thousand-clip bin streams a thousand completions. Emitting mediaChanged()
  // per completion made QML re-materialise the whole 1000-entry visibleMedia
  // list and re-sort it every time - a full-bin rebuild per thumbnail - which
  // pinned the GUI thread in back-to-back V4 GC during playback (the stutter).
  // Same reasoning and shape as the TTS publish throttle: emit at most once per
  // interval, with a trailing timer so the last fill is never dropped. The
  // media-bin append from a large import rides the same throttle (one
  // mediaChanged() per interval instead of one per 120 ms queue batch), hence
  // the parameter: a backfill completion also repaints placed clips and passes
  // withTimeline=true, an import append touches no clip and passes false. The
  // intent is sticky across a coalesced window (m_mediaPreviewTimelinePending),
  // so a window that saw any backfill still flushes timelineChanged().
  void publishMediaBinUpdate(bool withTimeline);
  QElapsedTimer m_mediaPreviewPublishClock;
  QTimer m_mediaPreviewPublishTimer;
  bool m_mediaPreviewPublishPending = false;
  bool m_mediaPreviewTimelinePending = false;
  // Same shape, for live effect-parameter edits. A blur-strength (or any
  // Effect Controls) slider is live:true and fires setClipEffectParameter /
  // setClipEffectSetting on every mouse-move. Each one emitted clipsChanged() +
  // timelineChanged() directly, and clipsChanged() queues a full projection:
  // TimelineClipModel::setClips (an O(clips) span rebuild) plus
  // VideoPreviewHelper::setTimeline -> rebuildIndexes (an O(clips)+O(media)
  // re-index) - none of which an effect value changes. On a thousand-clip
  // timeline that is one whole-timeline rebuild per drag tick, so dragging a
  // blur slider pins the GUI thread. Coalesce the notification: leading edge so
  // the preview reacts the moment the drag starts, trailing timer so the value
  // the user lets go on is never dropped.
  void publishClipStackUpdate();
  // Emits clipsChanged()/timelineChanged() tagged payload-only (see
  // m_clipsPayloadOnlyEdit): the clip list's structure is unchanged and only
  // some clip's inner fields moved, so the projection takes the windowed path.
  void emitClipsPayloadChanged();
  QElapsedTimer m_clipStackPublishClock;
  QTimer m_clipStackPublishTimer;
  bool m_clipStackPublishPending = false;
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
  mutable int m_cachedSubtitleTrackCount = 1;
  mutable bool m_cachedHasSubtitleClips = false;
  mutable bool m_cachedHasRenderableClips = false;
  mutable bool m_clipCacheReady = false;
  void ensureClipCaches() const;
  TimelineClipModel m_timelineClipModel;
  // Set when the clip list has moved and the two things that index it - the
  // preview's per-track arrays and the timeline's row model - have not caught up
  // yet. One edit emits clipsChanged several times (a drop that adds a clip and
  // then trims it; a transcript that inserts a cue at a time), and re-indexing
  // twenty thousand subtitle clips per emission is what the stall watchdog kept
  // catching. Both are rebuilt once, at the end of the event loop iteration.
  bool m_previewTimelineDirty = false;
  bool m_clipModelDirty = false;
  // Payload-only counterparts of the two flags above. A live effect/transform/
  // gain slider changes a clip's effects map but never its position, duration,
  // track or kind, so the span sort (TimelineClipModel::setClips) and the index
  // rebuild (VideoPreviewHelper::setTimeline -> rebuildIndexes) are both pure
  // waste - the structure they rebuild is identical. These route that edit to a
  // windowed refresh instead: the model re-emits dataChanged for the visible
  // rows and the preview re-resolves the active frame, no O(clips) rebuild.
  // A structural edit in the same event-loop turn upgrades to the full path.
  bool m_previewPayloadDirty = false;
  bool m_clipModelPayloadDirty = false;
  // Hint set by emitClipsPayloadChanged() immediately before it emits
  // clipsChanged(), read and cleared by that signal's projection handler, so the
  // handler can tell a payload-only edit from a structural one. Lives for the
  // duration of one synchronous emit only.
  bool m_clipsPayloadOnlyEdit = false;
  bool m_trackStatesDirty = false;
  bool m_projectionQueued = false;
  // Set for the one clipsChanged a live text edit emits; the monitor consumes it
  // and skips its picture re-seek. See takePreviewSeekSuppression().
  bool m_skipNextPreviewSeek = false;
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
  // Edit-batch state (see beginEditBatch): depth of nested batches, and which
  // deferred signals the unwind owes. Reset at the outermost begin.
  int m_editBatchDepth = 0;
  bool m_editBatchClipsDirty = false;
  bool m_editBatchMarkersDirty = false;
  bool m_editBatchPayloadOnly = false;
  QStringList m_effectEditSelection;
  bool m_liveParameterEditActive = false;
  bool m_liveParameterSnapshotTaken = false;
  QString m_lastError;
  ProjectDatabase m_projectDatabase;
  ProjectSaveService m_projectSaveService;
  QProcess m_exportProcess;
  QString m_exportOutputPath;
  QString m_exportConcatFile;
  // Holds the filter graph when it is too big to pass on the command line.
  QString m_exportFilterScriptFile;
  QByteArray m_exportStandardError;
  double m_exportProgress = 0.0;
  QString m_exportStatus;
  ExportProgress m_exportMetrics;
  bool m_exportActive = false;
  bool m_exportCancelled = false;
  bool m_exportFinalizing = false;
  qint64 m_exportDurationMs = 0;
  qint64 m_exportRemainingSeconds = -1;
  QVariantList m_exportChunks;
  int m_exportChunkIndex = 0;
  QString m_exportWorkDir;
  QVariantList m_exportMedia;
  QVariantList m_exportTrackStates;
  QStringList m_exportMutedTracks;
  QVariantMap m_exportColorSettings;
  QVariantMap m_exportSettings;
  QVariantList m_exportClips;
  QProcess *m_exportProbeProcess = nullptr;
  QString m_exportEncoder;
  QString m_exportEncoderFallback;
  QElapsedTimer m_exportElapsed;
  qint64 m_exportElapsedMs = 0;
  ExportTimeEstimate m_exportEstimate;
  double m_exportJoinSpeed = 0;
  void prepareExportEncoder();
  void finishExportPreflight(bool success, const QString &reason);
  void startPreparedExport();
  void appendExportLog();
  bool startExportProcess(QStringList arguments);
  void startNextExportChunk();
  void finishExportProcess(int code, QProcess::ExitStatus status);
  void failExport(const QString &error);
  void cleanupExportWork();
  QProcess m_transcriptionProcess;
  TranscriptionBatch m_transcriptionBatch;
  QProcess m_demucsProcess;
  QStringList m_demucsSelectionQueue;
  bool m_demucsSelectionPumping = false;
  bool m_demucsBatchSetting = false;
  void pumpDemucsSelectionQueue();
  QString m_demucsClipId;
  QString m_demucsOutputDir;
  QByteArray m_demucsOutput;
  double m_demucsProgress = 0.0;
  int m_demucsProgressPass = 0;
  double m_demucsLastRawProgress = 0.0;
  int m_demucsExpectedPasses = 1;
  QString m_demucsStatus;

  // System dependency check. m_systemRequirements is the list the navbar reads;
  // the probe runs Python out of process, so a guard keeps two from overlapping.
  QVariantList m_systemRequirements;
  bool m_requirementsChecking = false;
  void buildSystemRequirements(const QVariantMap &probe);
  // Fills m_systemRequirements with every row up front so the page is never a
  // blank panel while the out-of-process probe runs: the interpreter-free rows
  // (FFmpeg, offline Whisper) get their real state immediately and the pip-probed
  // engines show a transient "Checking..." until buildSystemRequirements lands.
  void buildRequirementPlaceholders();
  // Persist / restore the last requirement-probe result (defined in
  // backend_requirements_cache.cpp). The page shows this saved state at launch
  // instead of re-probing on every open; it is refreshed only by an explicit
  // Recheck or an in-app install. loadCachedRequirements() runs in the ctor;
  // persistRequirements() runs at the end of buildSystemRequirements().
  void loadCachedRequirements();
  void persistRequirements() const;
  // Offline Whisper "install" is a model-file download, not a pip run: it works
  // with no Python at all. Lives here so installRequirement("whisper") can hand
  // off to it. m_whisperDownload owns the in-flight reply so a second click is
  // ignored while one is running (guarded by m_installingId).
  void downloadWhisperModel();
  QNetworkAccessManager *m_whisperNetwork = nullptr;
  QNetworkReply *m_whisperDownload = nullptr;
  // Live install of an optional engine into the system Python.
  QProcess m_installProcess;
  QString m_installingId;
  QString m_installStatus;
  // Displayed progress is eased toward m_demucsRawTarget on a timer so the bar
  // counts up smoothly instead of sitting at 0% during model load and then
  // jumping to demucs' first coarse tqdm milestone.
  double m_demucsRawTarget = 0.0;
  QTimer m_demucsSmoothTimer;
  QElapsedTimer m_demucsWarmup;
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
  // Window audio extracted ahead of the worker. It exists to overlap ffmpeg with
  // inference; it cannot change a transcript because its command line is the one
  // the worker would have run itself.
  WindowAudioPrefetcher m_windowAudio;
  // Whisper's own tqdm percentage restarts at zero every window, so it is only
  // meaningful as a fraction of the current window.
  double m_transcriptionWindowFraction = 0.0;
  // How far each media's transcript reaches, so Transcribe can continue a
  // cancelled eight hour run instead of starting over.
  QHash<QString, qint64> m_transcriptCoverageMs;
  QString m_transcriptLanguage;
  TranscriptTranslator m_translator;
  RecapGenerator m_recap;
  // Scene Edit Detection state. The QProcess runs the bundled ffmpeg scene-score
  // pass; the finished handler (wired lazily on first use) reads these to know
  // which clip the cuts belong to and what to do with them.
  QProcess m_sceneProcess;
  QStringList m_sceneQueue;
  bool m_sceneBatchActive = false;
  double m_sceneBatchThreshold = 0.4;
  int m_sceneBatchActed = 0;
  QString m_sceneBatchError;
  void pumpSceneQueue();
  void finishSceneJob(bool ok, int acted, const QString &error);
  QString m_sceneStatus;
  QString m_sceneClipId;
  QVariantMap m_sceneClipSnapshot;
  qint64 m_sceneClipStartMs = 0;
  qint64 m_sceneClipDurationMs = 0;
  bool m_sceneApplyCuts = true;
  bool m_sceneCreateMarkers = false;
  qint64 m_sceneMinShotMs = 1000;
  bool m_sceneWired = false;
  bool m_sceneCancelRequested = false;
  // How much of the detection pass ffmpeg has reported through -progress, and
  // the cut count streamed from showinfo so far, so a long scan shows movement
  // instead of one frozen "Detecting scene cuts..." label.
  double m_sceneProgress = 0.0;
  int m_sceneCutsSoFar = 0;
  QByteArray m_sceneStderrTail;
  qint64 m_sceneScanSpanMs = 0;
  // Silence scan state. The scan itself runs on a QtConcurrent worker; the
  // generation counter lets a finished worker tell whether it is still the
  // current scan before it touches any of this.
  static QVariantList
  silenceIntervalsToVariant(const QVector<QPair<qint64, qint64>> &intervals);
  void finishSilenceScan(int generation, bool ok, bool cancelled,
                         const QString &error,
                         const QVector<QPair<qint64, qint64>> &found);
  std::shared_ptr<std::atomic_bool> m_silenceCancel = std::make_shared<std::atomic_bool>(false);
  int m_silenceScanGeneration = 0;
  bool m_silenceScanning = false;
  double m_silenceProgress = 0.0;
  QString m_silenceStatus;
  QString m_silenceClipId;
  QVariantList m_silenceCandidates;
  QHash<QString, QVariantMap> m_silenceSnapshots;
  QStringList m_silenceQueue;
  QVariantList m_silenceBatchCandidates;
  bool m_silenceBatchActive = false;
  bool m_silenceBatchApplying = false;
  double m_silenceBatchThreshold = -32.0;
  qint64 m_silenceBatchMinimum = 500;
  qint64 m_silenceBatchPadding = 100;
  bool m_silenceBatchAverage = true;
  QString m_silenceBatchError;
  void pumpSilenceQueue();

  // --- Voice-Over Record ---------------------------------------------------
  // Created on first use: constructing it starts a capture thread, and a user
  // who never records a voice-over should not pay for one.
  VoiceOverRecorder *m_voiceOver = nullptr;
  WebcamRecorder *m_voiceOverWebcam = nullptr;
  bool m_voiceOverPending = false;
  bool m_voiceOverStopping = false;
  bool m_voiceOverWithWebcam = false;
  bool m_voiceOverKeptAudio = false;
  bool m_voiceOverAudioEnded = false;
  struct WebcamTake { QString clipId; qint64 offsetMs; qint64 durationMs; };
  QVector<WebcamTake> m_voiceOverWebcamTakes;
  void commitWebcamTakes(const QString &path, qint64 durationMs);
  void finishVoiceOverSession(bool keptAnything);
  void teardownVoiceOverRecorder();
  QTimer *m_voiceOverCountdownTimer = nullptr;
  bool m_voiceOverRecording = false;
  int m_voiceOverCountdown = 0;
  QString m_voiceOverTrack;
  QString m_voiceOverStatus;
  // %1 is substituted with the segment number, so each line of a hands-free
  // session gets its own file.
  QString m_voiceOverPathPattern;
  qint64 m_voiceOverStartMs = 0;
  int m_voiceOverTakeCount = 0;
  bool m_voiceOverHandsFree = false;
  QTimer *m_voiceOverTickTimer = nullptr;

  NoiseRemoval m_noiseRemoval;
  bool m_noiseWired = false;
  bool m_noiseApplying = false;
  bool m_noiseCancelled = false;
  QString m_noiseStatus;
  QString m_noisePreview;
  QString m_noiseProjectId;
  QString m_noiseSequenceId;
  int m_noiseStrength = 100;
  int m_noiseJobIndex = 0;
  QVariantList m_noiseJobs;
  QMediaPlayer *m_noisePlayer = nullptr;
  QAudioOutput *m_noiseOutput = nullptr;
  void ensureNoiseRemoval();
  QStringList noiseRemovalTargets(const QStringList &ids) const;
  void startNextNoiseRemoval();
  void finishNoiseRemoval(bool ok, const QString &error);

  // Voice changer. One ffmpeg child at a time: a second render would fight the
  // first for the same cache path.
  QProcess m_voiceFxProcess;
  bool m_voiceFxWired = false;
  bool m_voiceFxBusy = false;
  bool m_voiceFxApplying = false;
  QString m_voiceFxStatus;
  QString m_voiceFxPreviewId;
  QString m_voiceFxClipId;
  QVariantMap m_voiceFxClipSnapshot;
  QString m_voiceFxPresetId;
  QString m_voiceFxOutputPath;
  QMediaPlayer *m_voicePreviewPlayer = nullptr;
  QAudioOutput *m_voicePreviewOutput = nullptr;
  void handleVoiceFxFinished(int exitCode, QProcess::ExitStatus status);
  bool startVoiceRender(const QString &clipId, const QString &presetId,
                        bool wholeClip);
  QString applyRenderedVoice(const QString &clipId, const QString &presetId,
                             const QString &path, qint64 durationMs);
  // Hands-free places a clip per line, and there is one ffmpeg child, so voice
  // renders have to queue rather than be dropped on the floor.
  void enqueueVoiceRender(const QString &clipId, const QString &presetId);
  void pumpVoiceQueue();
  QVector<QPair<QString, QString>> m_voiceFxQueue;
  bool m_voiceQueuePumping = false;
  bool m_voiceBatchActive = false;
  bool m_voiceBatchFailed = false;
  QString m_voiceOverLastClipId;
  bool m_voiceOverWasPlaying = false;
  void ensureVoiceOverRecorder();
  void beginVoiceOverCapture();
  QString commitVoiceOverTake(const QString &path, qint64 durationMs,
                              qint64 offsetMs);
  QString voiceOverPathPattern() const;
  void resetVoiceOverState();
  // In-memory clip clipboard for Copy/Cut/Paste. Each entry is a clip map with
  // startMs normalised to an offset from the earliest copied clip, so a paste
  // can rebase the whole set onto a new anchor time.
  QVariantList m_clipClipboard;
  // Wired on first use from backend_translation.cpp, so the catalogue probe adds
  // nothing to this constructor or to launch.
  TranslationModelProbe m_translationModelProbe;
  bool m_translationModelProbeWired = false;
  TextToSpeechEngine m_textToSpeechEngine;
  EffectPreviewGenerator m_effectPreviewGenerator;
  KeyframeEngine m_keyframeEngine;
  FfmpegPreviewDecoder m_previewDecoder;
  // Scrub frames come from here instead of from a decoder restart per position:
  // one worker, newest request wins, warm container and frame cache behind it.
  ScrubFrameService m_scrubFrames;
  VideoPreviewHelper m_videoPreviewHelper;
  quint64 m_previewFrameRevision = 0;
  // The image accepted on the GUI thread. Providers read this snapshot instead
  // of the decoder's changing buffer, so pixels cannot advance without a
  // previewFrameChanged notification, including during pause/resume.
  mutable QMutex m_previewFrameMutex;
  QImage m_previewFrame;
  QElapsedTimer m_previewMonotonic;
  QTimer m_previewPlayheadTimer;
  PreviewPlaybackClock m_previewPlaybackClock;
  QVariantMap m_previewSessionClip;
  QString m_previewSessionPath;
  QString m_previewSessionAudioPath;
  QVariantMap m_acceptedPreviewClip;
  QString m_acceptedPreviewPath;
  qint64 m_acceptedPreviewTimelineMs = -1;
  qint64 m_acceptedPreviewSourceMs = -1;
  QVariantMap m_requestedPreviewClip;
  QString m_requestedPreviewPath;
  qint64 m_requestedPreviewTimelineMs = -1;
  bool m_advancingPreview = false;
  bool m_pausingPreview = false;
  quint64 m_previewSeekSerial = 0;
  quint64 m_previewSessionSeekSerial = 0;
  double m_previewAudioVolume = 1.0;
  quint64 m_previewTransportSerial = 0;
  void advancePreviewPlayhead();
  // Timestamp of m_previewFrame, accepted with its pixels on the GUI thread.
  // The decoder can already have published a newer frame while its queued
  // notification is still waiting.
  qint64 m_paintedSourceMs = -1;
  // The one before it, which is the picture still on screen when the newest
  // frame was published but not yet painted.
  qint64 m_previousPaintedSourceMs = -1;
  // Wall clock, same epoch as QML's Date.now(), of the moment m_paintedSourceMs
  // was handed over.
  qint64 m_paintedWallMs = 0;
  // Pixel width of the picture QML was handed.
  int m_paintedFrameWidth = 0;
  // Nesting depth of timeline gestures. A drag over the tracks raises this and
  // the drop lowers it; the filmstrip watches it so no thumbnail decode is even
  // requested while the pointer is moving.
  int m_timelineInteractions = 0;
  // Lowers the hold by itself if a gesture ends without saying so - a native
  // Windows drag that is cancelled by Escape never delivers a drop.
  QTimer m_timelineInteractionTimer;
  bool m_acceptScrubFrames = false;
  quint64 m_minimumScrubRevision = 0;
  // Rebuilds m_previewGrade from the clip under the playhead, bumps the
  // generation, drops the cached graded frame and asks QML to re-read the URL.
  // GUI thread only; the provider thread only ever reads under the mutex.
  void refreshPreviewGrade();
  // Re-reads activeCaptionWordTimes for the cue under the playhead. Called when
  // the active cue changes and again when a background analysis lands, so the
  // highlight switches from the estimate to the measured timing in place.
  void refreshActiveCaptionWordTimes();
  // The clip whose audio the cue at this time is spoken over, and where in that
  // source the cue starts. An empty path means there is nothing to listen to.
  QString captionAudioFromClip(const QVariantMap &clip, qint64 cueStartMs,
                               qint64 *sourceStartMs) const;
  QString captionAudioSourceFor(qint64 cueStartMs, qint64 *sourceStartMs) const;
  QString captionAudioSourceForExport(qint64 cueStartMs,
                                      qint64 *sourceStartMs) const;
  // Flattens m_previewGrade into the uniform map the shaders read. Called from
  // refreshPreviewGrade only, so QML never sees a half-written grade.
  void rebuildLumetriUniforms();
  // The Lumetri chain the monitor is drawing, and the cached result of running
  // it. Both are read from whichever thread the image provider runs on, so both
  // live behind m_previewGradeMutex.
  LumetriPreview::Correction m_previewGrade;
  quint64 m_previewGradeGeneration = 0;
  // The same chain as shader state. GUI thread only - QML is the only reader.
  QVariantMap m_lumetriUniforms;
  bool m_gpuGrading = true;
  // What the grade was built from, so an unrelated colorSettingsChanged does not
  // invalidate a frame that would grade identically.
  QVariantMap m_previewGradeClipColor;
  QVariantMap m_previewGradeMediaColor;
  QVariantMap m_previewGradeProjectColor;
  bool m_previewGradeSeeded = false;
  mutable QMutex m_previewGradeMutex;
  mutable QImage m_previewGradedFrame;
  mutable quint64 m_previewGradedRevision = 0;
  mutable quint64 m_previewGradedGeneration = 0;
  // colorSettingsChanged rebuilds the Lumetri panel's whole control tree in the
  // emitting call. On a drop that was 413-462 ms of GUI thread inside the drop
  // handler, so the notification is deferred one event-loop turn: same getters,
  // same eventual UI, just not inside the gesture. Declared after the state it
  // notifies about, so the emit is retracted before that state is destroyed.
  SignalCoalescer m_colorSettingsNotify{
      "Backend::colorSettingsChanged",
      [this]() {
        // Every writer of a colour value already schedules this, so it is the one
        // place the monitor's grade has to be rebuilt from.
        refreshPreviewGrade();
        emit colorSettingsChanged();
      }};
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
  // Owned by this object, held by pointer so QFileSystemWatcher stays a forward
  // declaration in this header: it is created once, in configureWhisperCacheWatch().
  QFileSystemWatcher *m_whisperCacheWatcher = nullptr;
  QTimer m_whisperCacheRescanTimer;
  // What the picker was last told. A directory notification arrives for every
  // write into the folder, and re-reading it is a handful of stats, but emitting
  // whisperModelsChanged() rebuilds the combo box - so the signal goes out only
  // when the answer actually moved.
  QStringList m_whisperModelsSnapshot;
  QTimer m_projectDatabaseSaveTimer;
  QTimer m_ttsImportTimer;
  QVariantList m_pendingTtsOutputs;
  // Occupancy of A1..A64, prepared once per import.
  //
  // Placing a generated cue used to rescan every clip on the timeline for every
  // candidate lane: with 19831 cues over a 19831-clip subtitle track that is
  // hundreds of millions of QVariantMap conversions, and it ran on the GUI thread
  // in batches of two. See timed_speech_placer.h for why the lanes are interval
  // maps rather than the sorted-list-plus-cursor this used to be.
  cutpro::speech::SpeechLaneMap m_ttsLanes;
  // Path -> media id for this import. Identical cue text is synthesized once and
  // therefore lands in one file, so the second clip that needs it reuses the media
  // entry instead of adding a seventeen-key duplicate of it.
  QHash<QString, QString> m_ttsMediaByPath;
  int m_pendingTtsIndex = 0;
  int m_pendingTtsAdded = 0;
  bool m_ttsImportActive = false;
  // Set while the worker is still producing cues, so a drained queue means "waiting
  // for more" rather than "done". What is left when it clears is the tail: the cues
  // the worker sent faster than they could be placed.
  bool m_ttsStreamOpen = false;
  int m_ttsTailBase = 0;
  // A worker that failed partway through. What it did manage to say stays on the
  // timeline; this is reported once the queue behind it has drained.
  QString m_ttsStreamError;
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
  // The running voice count (m_pendingTtsAdded) at the last progress publish. When
  // it has not moved, the drain placed nothing since, so a relay + re-projection
  // would rebuild the same list - the publish is skipped instead.
  int m_ttsPublishedAdded = 0;
};
