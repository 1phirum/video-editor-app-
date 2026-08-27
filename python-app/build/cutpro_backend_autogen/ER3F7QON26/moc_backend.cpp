/****************************************************************************
** Meta object code from reading C++ file 'backend.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/app/core_app/backend.h"
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'backend.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN7BackendE_t {};
} // unnamed namespace

template <> constexpr inline auto Backend::qt_create_metaobjectdata<qt_meta_tag_ZN7BackendE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "Backend",
        "projectChanged",
        "",
        "sequenceChanged",
        "mediaChanged",
        "mediaImportChanged",
        "timelinePlacementChanged",
        "timelinePlacementFinished",
        "success",
        "clipIds",
        "clipsChanged",
        "tracksChanged",
        "markersChanged",
        "snappingChanged",
        "timelineChanged",
        "playheadChanged",
        "playingChanged",
        "dirtyChanged",
        "historyChanged",
        "activeWorkspaceChanged",
        "layoutPresetChanged",
        "captionStyleChanged",
        "captionFontsChanged",
        "exportStateChanged",
        "demucsChanged",
        "demucsFinished",
        "clipId",
        "errorChanged",
        "exportFinished",
        "outputPath",
        "transcriptionFinished",
        "mediaId",
        "transcriptChanged",
        "whisperModelsChanged",
        "colorSettingsChanged",
        "selectionChanged",
        "selectionDetailChanged",
        "effectControlsRequested",
        "customBlurEditChanged",
        "effectsBrowserRequested",
        "effectPreviewReady",
        "effectId",
        "animated",
        "url",
        "appSettingsChanged",
        "mediaCacheChanged",
        "previewFrameChanged",
        "previewStateChanged",
        "previewErrorChanged",
        "timelineInteractionChanged",
        "autoSaveCompleted",
        "path",
        "setCaptionBlurRegionNormalized",
        "x",
        "y",
        "width",
        "height",
        "setCaptionPositionNormalized",
        "newProject",
        "name",
        "location",
        "createSequence",
        "sequenceName",
        "importMedia",
        "paths",
        "copyIntoProject",
        "importMediaAsync",
        "cancelMediaImport",
        "removeMedia",
        "removeMediaSelection",
        "mediaIds",
        "renameMedia",
        "openMediaExternally",
        "revealMediaInFileManager",
        "copyMediaPath",
        "addClip",
        "startMs",
        "track",
        "addMediaToTimeline",
        "addMediaSelectionToTimeline",
        "beginTimelinePlacement",
        "cancelTimelinePlacement",
        "moveClip",
        "moveClips",
        "deltaMs",
        "trackDelta",
        "splitClip",
        "positionMs",
        "trimClipStart",
        "trimClipEnd",
        "endMs",
        "deleteClipLeft",
        "deleteClipRight",
        "removeClip",
        "removeClips",
        "addTrack",
        "kind",
        "removeLastTrack",
        "setTrackMuted",
        "muted",
        "setTrackState",
        "state",
        "enabled",
        "trackState",
        "QVariantMap",
        "trackLocked",
        "trackVisible",
        "trackSolo",
        "trackSyncLocked",
        "trackTargeted",
        "snapTime",
        "requestedMs",
        "excludedClipIds",
        "thresholdMs",
        "rippleDeleteClips",
        "rippleTrimClipEnd",
        "closeGap",
        "addMarker",
        "color",
        "updateMarker",
        "markerId",
        "removeMarker",
        "setSnappingEnabled",
        "saveProject",
        "loadProject",
        "probeMedia",
        "generateDetailedPreviews",
        "startExport",
        "preset",
        "startExportWithSettings",
        "settings",
        "downloadCaptionFont",
        "family",
        "suggestedExportPath",
        "cancelExport",
        "transcribeMedia",
        "model",
        "language",
        "cancelTranscription",
        "cancelDemucs",
        "importSubtitles",
        "exportTranscriptSrt",
        "exportTranscriptTtml",
        "updateTranscriptSegment",
        "index",
        "text",
        "addTranscriptToTimeline",
        "removeTranscriptFromTimeline",
        "translateTranscript",
        "targetLanguage",
        "testTranslationProvider",
        "cancelTranslation",
        "undo",
        "redo",
        "clearError",
        "setColorSetting",
        "key",
        "QVariant",
        "value",
        "setClipColorSetting",
        "resetClipColorSettings",
        "setClipEffectSetting",
        "resetClipEffectSettings",
        "generateTimedTextToSpeech",
        "QVariantList",
        "segments",
        "gender",
        "addClipEffect",
        "removeClipEffect",
        "instanceId",
        "moveClipEffect",
        "offset",
        "setClipEffectEnabled",
        "setClipEffectParameter",
        "parameterId",
        "resetClipEffectInstance",
        "beginCustomBlurMaskEdit",
        "endCustomBlurMaskEdit",
        "setCustomBlurMask",
        "requestEffectControls",
        "requestEffectsBrowser",
        "requestEffectPreview",
        "defaultAppSettings",
        "applyAppSettings",
        "resetAppSettings",
        "clearMediaCache",
        "setMediaColorSetting",
        "startPreviewDecode",
        "mediaKind",
        "sourcePositionMs",
        "durationMs",
        "sourceWidth",
        "sourceHeight",
        "frameRate",
        "audioEnabled",
        "volume",
        "audioPath",
        "requestPreviewFrame",
        "prewarmPreviewSource",
        "releasePreviewSource",
        "previewDecodeStatistics",
        "stopPreviewDecode",
        "markGuiScope",
        "label",
        "timelineTileToken",
        "timelineTilesAvailable",
        "waveformWindowToken",
        "waveformWindowsAvailable",
        "setPreviewSurfaceSize",
        "beginTimelineInteraction",
        "touchTimelineInteraction",
        "endTimelineInteraction",
        "coreVersion",
        "projectId",
        "projectName",
        "projectLocation",
        "projectFile",
        "sequenceId",
        "activeWorkspace",
        "layoutPreset",
        "captionFontFamily",
        "downloadedCaptionFonts",
        "captionFontSize",
        "captionTextColor",
        "captionBold",
        "captionItalic",
        "captionBackgroundVisible",
        "captionBackgroundColor",
        "captionPosition",
        "captionAlignment",
        "captionPositionX",
        "captionPositionY",
        "captionBlurEnabled",
        "captionBlurTrackingEnabled",
        "captionBlurRegionX",
        "captionBlurRegionY",
        "captionBlurRegionWidth",
        "captionBlurRegionHeight",
        "captionBlurStrength",
        "captionBlurPadding",
        "media",
        "clips",
        "timelineClipModel",
        "TimelineClipModel*",
        "hasSubtitleClips",
        "videoTrackCount",
        "audioTrackCount",
        "mutedTracks",
        "trackStates",
        "markers",
        "snappingEnabled",
        "mediaCount",
        "mediaImportInProgress",
        "mediaImportProgress",
        "timelinePlacementInProgress",
        "timelinePlacementProgress",
        "timelinePlacementStatus",
        "hasSequence",
        "hasMedia",
        "canExport",
        "playheadMs",
        "playing",
        "dirty",
        "canUndo",
        "canRedo",
        "exportInProgress",
        "exportProgress",
        "exportStatus",
        "transcript",
        "transcriptionInProgress",
        "transcriptionStatus",
        "transcriptionProgress",
        "transcriptLanguage",
        "transcribedMediaIds",
        "downloadedWhisperModels",
        "translationInProgress",
        "translationStatus",
        "translationTestInProgress",
        "demucsInProgress",
        "demucsProgress",
        "demucsStatus",
        "lastError",
        "colorSettings",
        "activeColorClip",
        "activeColorMedia",
        "selectedClipId",
        "selectedClip",
        "effectDefinitions",
        "keyframeEngine",
        "KeyframeEngine*",
        "textToSpeechEngine",
        "TextToSpeechEngine*",
        "videoPreviewHelper",
        "VideoPreviewHelper*",
        "customBlurEditClipId",
        "customBlurEditInstanceId",
        "appSettings",
        "mediaCachePath",
        "mediaCacheSize",
        "previewFrameUrl",
        "previewDecoding",
        "previewError",
        "timelineInteractionActive"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'projectChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'sequenceChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'mediaChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'mediaImportChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'timelinePlacementChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'timelinePlacementFinished'
        QtMocHelpers::SignalData<void(bool, const QStringList &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 }, { QMetaType::QStringList, 9 },
        }}),
        // Signal 'clipsChanged'
        QtMocHelpers::SignalData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'tracksChanged'
        QtMocHelpers::SignalData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'markersChanged'
        QtMocHelpers::SignalData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'snappingChanged'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'timelineChanged'
        QtMocHelpers::SignalData<void()>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'playheadChanged'
        QtMocHelpers::SignalData<void()>(15, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'playingChanged'
        QtMocHelpers::SignalData<void()>(16, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dirtyChanged'
        QtMocHelpers::SignalData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'historyChanged'
        QtMocHelpers::SignalData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'activeWorkspaceChanged'
        QtMocHelpers::SignalData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'layoutPresetChanged'
        QtMocHelpers::SignalData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'captionStyleChanged'
        QtMocHelpers::SignalData<void()>(21, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'captionFontsChanged'
        QtMocHelpers::SignalData<void()>(22, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'exportStateChanged'
        QtMocHelpers::SignalData<void()>(23, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'demucsChanged'
        QtMocHelpers::SignalData<void()>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'demucsFinished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 }, { QMetaType::QString, 26 },
        }}),
        // Signal 'errorChanged'
        QtMocHelpers::SignalData<void()>(27, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'exportFinished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(28, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 }, { QMetaType::QString, 29 },
        }}),
        // Signal 'transcriptionFinished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 }, { QMetaType::QString, 31 },
        }}),
        // Signal 'transcriptChanged'
        QtMocHelpers::SignalData<void()>(32, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'whisperModelsChanged'
        QtMocHelpers::SignalData<void()>(33, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'colorSettingsChanged'
        QtMocHelpers::SignalData<void()>(34, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'selectionChanged'
        QtMocHelpers::SignalData<void()>(35, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'selectionDetailChanged'
        QtMocHelpers::SignalData<void()>(36, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'effectControlsRequested'
        QtMocHelpers::SignalData<void()>(37, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'customBlurEditChanged'
        QtMocHelpers::SignalData<void()>(38, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'effectsBrowserRequested'
        QtMocHelpers::SignalData<void()>(39, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'effectPreviewReady'
        QtMocHelpers::SignalData<void(const QString &, const QString &, bool, const QString &)>(40, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 41 }, { QMetaType::Bool, 42 }, { QMetaType::QString, 43 },
        }}),
        // Signal 'appSettingsChanged'
        QtMocHelpers::SignalData<void()>(44, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'mediaCacheChanged'
        QtMocHelpers::SignalData<void()>(45, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'previewFrameChanged'
        QtMocHelpers::SignalData<void()>(46, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'previewStateChanged'
        QtMocHelpers::SignalData<void()>(47, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'previewErrorChanged'
        QtMocHelpers::SignalData<void()>(48, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'timelineInteractionChanged'
        QtMocHelpers::SignalData<void()>(49, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'autoSaveCompleted'
        QtMocHelpers::SignalData<void(const QString &)>(50, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'setCaptionBlurRegionNormalized'
        QtMocHelpers::MethodData<void(double, double, double, double)>(52, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 53 }, { QMetaType::Double, 54 }, { QMetaType::Double, 55 }, { QMetaType::Double, 56 },
        }}),
        // Method 'setCaptionPositionNormalized'
        QtMocHelpers::MethodData<void(double, double)>(57, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 53 }, { QMetaType::Double, 54 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, bool, const QString &)>(58, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 59 }, { QMetaType::QString, 60 }, { QMetaType::Bool, 61 }, { QMetaType::QString, 62 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, bool)>(58, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 59 }, { QMetaType::QString, 60 }, { QMetaType::Bool, 61 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(58, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 59 }, { QMetaType::QString, 60 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool(const QString &)>(58, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 59 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool()>(58, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool),
        // Method 'importMedia'
        QtMocHelpers::MethodData<int(const QStringList &, bool)>(63, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::QStringList, 64 }, { QMetaType::Bool, 65 },
        }}),
        // Method 'importMedia'
        QtMocHelpers::MethodData<int(const QStringList &)>(63, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Int, {{
            { QMetaType::QStringList, 64 },
        }}),
        // Method 'importMediaAsync'
        QtMocHelpers::MethodData<void(const QStringList &, bool)>(66, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 64 }, { QMetaType::Bool, 65 },
        }}),
        // Method 'importMediaAsync'
        QtMocHelpers::MethodData<void(const QStringList &)>(66, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QStringList, 64 },
        }}),
        // Method 'cancelMediaImport'
        QtMocHelpers::MethodData<void()>(67, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'removeMedia'
        QtMocHelpers::MethodData<bool(const QString &)>(68, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'removeMediaSelection'
        QtMocHelpers::MethodData<bool(const QStringList &)>(69, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QStringList, 70 },
        }}),
        // Method 'renameMedia'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(71, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 59 },
        }}),
        // Method 'openMediaExternally'
        QtMocHelpers::MethodData<bool(const QString &)>(72, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'revealMediaInFileManager'
        QtMocHelpers::MethodData<bool(const QString &)>(73, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'copyMediaPath'
        QtMocHelpers::MethodData<bool(const QString &)>(74, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'createSequence'
        QtMocHelpers::MethodData<bool(const QString &)>(61, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 59 },
        }}),
        // Method 'createSequence'
        QtMocHelpers::MethodData<bool()>(61, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool),
        // Method 'addClip'
        QtMocHelpers::MethodData<QString(const QString &, qint64, const QString &)>(75, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 76 }, { QMetaType::QString, 77 },
        }}),
        // Method 'addClip'
        QtMocHelpers::MethodData<QString(const QString &, qint64)>(75, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 76 },
        }}),
        // Method 'addClip'
        QtMocHelpers::MethodData<QString(const QString &)>(75, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'addMediaToTimeline'
        QtMocHelpers::MethodData<QStringList(const QString &, qint64, const QString &)>(78, 2, QMC::AccessPublic, QMetaType::QStringList, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 76 }, { QMetaType::QString, 77 },
        }}),
        // Method 'addMediaToTimeline'
        QtMocHelpers::MethodData<QStringList(const QString &, qint64)>(78, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QStringList, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 76 },
        }}),
        // Method 'addMediaToTimeline'
        QtMocHelpers::MethodData<QStringList(const QString &)>(78, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QStringList, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'addMediaSelectionToTimeline'
        QtMocHelpers::MethodData<bool(const QStringList &)>(79, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QStringList, 70 },
        }}),
        // Method 'beginTimelinePlacement'
        QtMocHelpers::MethodData<bool(const QStringList &, qint64, const QString &)>(80, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QStringList, 70 }, { QMetaType::LongLong, 76 }, { QMetaType::QString, 77 },
        }}),
        // Method 'cancelTimelinePlacement'
        QtMocHelpers::MethodData<void()>(81, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'moveClip'
        QtMocHelpers::MethodData<bool(const QString &, qint64, const QString &)>(82, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::LongLong, 76 }, { QMetaType::QString, 77 },
        }}),
        // Method 'moveClip'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(82, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::LongLong, 76 },
        }}),
        // Method 'moveClips'
        QtMocHelpers::MethodData<bool(const QStringList &, qint64, int)>(83, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QStringList, 9 }, { QMetaType::LongLong, 84 }, { QMetaType::Int, 85 },
        }}),
        // Method 'moveClips'
        QtMocHelpers::MethodData<bool(const QStringList &, qint64)>(83, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QStringList, 9 }, { QMetaType::LongLong, 84 },
        }}),
        // Method 'splitClip'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(86, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::LongLong, 87 },
        }}),
        // Method 'trimClipStart'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(88, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::LongLong, 76 },
        }}),
        // Method 'trimClipEnd'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(89, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::LongLong, 90 },
        }}),
        // Method 'deleteClipLeft'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(91, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::LongLong, 87 },
        }}),
        // Method 'deleteClipRight'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(92, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::LongLong, 87 },
        }}),
        // Method 'removeClip'
        QtMocHelpers::MethodData<bool(const QString &)>(93, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 },
        }}),
        // Method 'removeClips'
        QtMocHelpers::MethodData<bool(const QStringList &)>(94, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QStringList, 9 },
        }}),
        // Method 'addTrack'
        QtMocHelpers::MethodData<QString(const QString &)>(95, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 96 },
        }}),
        // Method 'removeLastTrack'
        QtMocHelpers::MethodData<bool(const QString &)>(97, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 96 },
        }}),
        // Method 'setTrackMuted'
        QtMocHelpers::MethodData<bool(const QString &, bool)>(98, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 77 }, { QMetaType::Bool, 99 },
        }}),
        // Method 'setTrackState'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, bool)>(100, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 77 }, { QMetaType::QString, 101 }, { QMetaType::Bool, 102 },
        }}),
        // Method 'trackState'
        QtMocHelpers::MethodData<QVariantMap(const QString &) const>(103, 2, QMC::AccessPublic, 0x80000000 | 104, {{
            { QMetaType::QString, 77 },
        }}),
        // Method 'trackLocked'
        QtMocHelpers::MethodData<bool(const QString &) const>(105, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 77 },
        }}),
        // Method 'trackVisible'
        QtMocHelpers::MethodData<bool(const QString &) const>(106, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 77 },
        }}),
        // Method 'trackSolo'
        QtMocHelpers::MethodData<bool(const QString &) const>(107, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 77 },
        }}),
        // Method 'trackSyncLocked'
        QtMocHelpers::MethodData<bool(const QString &) const>(108, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 77 },
        }}),
        // Method 'trackTargeted'
        QtMocHelpers::MethodData<bool(const QString &) const>(109, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 77 },
        }}),
        // Method 'snapTime'
        QtMocHelpers::MethodData<qint64(qint64, const QStringList &, qint64) const>(110, 2, QMC::AccessPublic, QMetaType::LongLong, {{
            { QMetaType::LongLong, 111 }, { QMetaType::QStringList, 112 }, { QMetaType::LongLong, 113 },
        }}),
        // Method 'snapTime'
        QtMocHelpers::MethodData<qint64(qint64, const QStringList &) const>(110, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::LongLong, {{
            { QMetaType::LongLong, 111 }, { QMetaType::QStringList, 112 },
        }}),
        // Method 'snapTime'
        QtMocHelpers::MethodData<qint64(qint64) const>(110, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::LongLong, {{
            { QMetaType::LongLong, 111 },
        }}),
        // Method 'rippleDeleteClips'
        QtMocHelpers::MethodData<bool(const QStringList &)>(114, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QStringList, 9 },
        }}),
        // Method 'rippleTrimClipEnd'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(115, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::LongLong, 90 },
        }}),
        // Method 'closeGap'
        QtMocHelpers::MethodData<bool(const QString &, qint64, qint64)>(116, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 77 }, { QMetaType::LongLong, 76 }, { QMetaType::LongLong, 90 },
        }}),
        // Method 'addMarker'
        QtMocHelpers::MethodData<QString(qint64, const QString &, const QString &)>(117, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::LongLong, 87 }, { QMetaType::QString, 59 }, { QMetaType::QString, 118 },
        }}),
        // Method 'addMarker'
        QtMocHelpers::MethodData<QString(qint64, const QString &)>(117, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::LongLong, 87 }, { QMetaType::QString, 59 },
        }}),
        // Method 'addMarker'
        QtMocHelpers::MethodData<QString(qint64)>(117, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::LongLong, 87 },
        }}),
        // Method 'updateMarker'
        QtMocHelpers::MethodData<bool(const QString &, qint64, const QString &, const QString &)>(119, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 120 }, { QMetaType::LongLong, 87 }, { QMetaType::QString, 59 }, { QMetaType::QString, 118 },
        }}),
        // Method 'removeMarker'
        QtMocHelpers::MethodData<bool(const QString &)>(121, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 120 },
        }}),
        // Method 'setSnappingEnabled'
        QtMocHelpers::MethodData<void(bool)>(122, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 102 },
        }}),
        // Method 'saveProject'
        QtMocHelpers::MethodData<bool(const QString &)>(123, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'saveProject'
        QtMocHelpers::MethodData<bool()>(123, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool),
        // Method 'loadProject'
        QtMocHelpers::MethodData<bool(const QString &)>(124, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'probeMedia'
        QtMocHelpers::MethodData<QVariantMap(const QString &, bool)>(125, 2, QMC::AccessPublic, 0x80000000 | 104, {{
            { QMetaType::QString, 51 }, { QMetaType::Bool, 126 },
        }}),
        // Method 'probeMedia'
        QtMocHelpers::MethodData<QVariantMap(const QString &)>(125, 2, QMC::AccessPublic | QMC::MethodCloned, 0x80000000 | 104, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'startExport'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(127, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 29 }, { QMetaType::QString, 128 },
        }}),
        // Method 'startExport'
        QtMocHelpers::MethodData<bool(const QString &)>(127, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 29 },
        }}),
        // Method 'startExportWithSettings'
        QtMocHelpers::MethodData<bool(const QString &, const QVariantMap &)>(129, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 29 }, { 0x80000000 | 104, 130 },
        }}),
        // Method 'downloadCaptionFont'
        QtMocHelpers::MethodData<bool(const QString &)>(131, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 132 },
        }}),
        // Method 'suggestedExportPath'
        QtMocHelpers::MethodData<QString() const>(133, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'cancelExport'
        QtMocHelpers::MethodData<void()>(134, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'transcribeMedia'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QString &)>(135, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 136 }, { QMetaType::QString, 137 },
        }}),
        // Method 'transcribeMedia'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(135, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 136 },
        }}),
        // Method 'transcribeMedia'
        QtMocHelpers::MethodData<bool(const QString &)>(135, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'cancelTranscription'
        QtMocHelpers::MethodData<void()>(138, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'cancelDemucs'
        QtMocHelpers::MethodData<void()>(139, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'importSubtitles'
        QtMocHelpers::MethodData<bool(const QString &)>(140, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'exportTranscriptSrt'
        QtMocHelpers::MethodData<bool(const QString &)>(141, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'exportTranscriptTtml'
        QtMocHelpers::MethodData<bool(const QString &)>(142, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'updateTranscriptSegment'
        QtMocHelpers::MethodData<bool(int, const QString &)>(143, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::Int, 144 }, { QMetaType::QString, 145 },
        }}),
        // Method 'addTranscriptToTimeline'
        QtMocHelpers::MethodData<bool()>(146, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'removeTranscriptFromTimeline'
        QtMocHelpers::MethodData<bool()>(147, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'translateTranscript'
        QtMocHelpers::MethodData<bool(const QString &, const QVariantMap &)>(148, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 149 }, { 0x80000000 | 104, 130 },
        }}),
        // Method 'translateTranscript'
        QtMocHelpers::MethodData<bool(const QString &)>(148, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 149 },
        }}),
        // Method 'testTranslationProvider'
        QtMocHelpers::MethodData<bool(const QVariantMap &)>(150, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 104, 130 },
        }}),
        // Method 'testTranslationProvider'
        QtMocHelpers::MethodData<bool()>(150, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool),
        // Method 'cancelTranslation'
        QtMocHelpers::MethodData<void()>(151, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'undo'
        QtMocHelpers::MethodData<void()>(152, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'redo'
        QtMocHelpers::MethodData<void()>(153, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'clearError'
        QtMocHelpers::MethodData<void()>(154, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setColorSetting'
        QtMocHelpers::MethodData<bool(const QString &, const QVariant &)>(155, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 156 }, { 0x80000000 | 157, 158 },
        }}),
        // Method 'setClipColorSetting'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QVariant &)>(159, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 156 }, { 0x80000000 | 157, 158 },
        }}),
        // Method 'resetClipColorSettings'
        QtMocHelpers::MethodData<bool(const QString &)>(160, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 },
        }}),
        // Method 'setClipEffectSetting'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QVariant &)>(161, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 156 }, { 0x80000000 | 157, 158 },
        }}),
        // Method 'resetClipEffectSettings'
        QtMocHelpers::MethodData<bool(const QString &)>(162, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 },
        }}),
        // Method 'generateTimedTextToSpeech'
        QtMocHelpers::MethodData<bool(const QVariantList &, const QString &, const QString &)>(163, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 164, 165 }, { QMetaType::QString, 137 }, { QMetaType::QString, 166 },
        }}),
        // Method 'addClipEffect'
        QtMocHelpers::MethodData<QString(const QString &, const QString &)>(167, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 41 },
        }}),
        // Method 'removeClipEffect'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(168, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 169 },
        }}),
        // Method 'moveClipEffect'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, int)>(170, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 169 }, { QMetaType::Int, 171 },
        }}),
        // Method 'setClipEffectEnabled'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, bool)>(172, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 169 }, { QMetaType::Bool, 102 },
        }}),
        // Method 'setClipEffectParameter'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QString &, const QVariant &)>(173, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 169 }, { QMetaType::QString, 174 }, { 0x80000000 | 157, 158 },
        }}),
        // Method 'resetClipEffectInstance'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(175, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 169 },
        }}),
        // Method 'beginCustomBlurMaskEdit'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(176, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 169 },
        }}),
        // Method 'endCustomBlurMaskEdit'
        QtMocHelpers::MethodData<void()>(177, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setCustomBlurMask'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, double, double, double, double)>(178, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 169 }, { QMetaType::Double, 53 }, { QMetaType::Double, 54 },
            { QMetaType::Double, 55 }, { QMetaType::Double, 56 },
        }}),
        // Method 'requestEffectControls'
        QtMocHelpers::MethodData<void(const QString &)>(179, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 26 },
        }}),
        // Method 'requestEffectsBrowser'
        QtMocHelpers::MethodData<void(const QString &)>(180, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 26 },
        }}),
        // Method 'requestEffectsBrowser'
        QtMocHelpers::MethodData<void()>(180, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Method 'requestEffectPreview'
        QtMocHelpers::MethodData<QString(const QString &, const QString &, bool)>(181, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 41 }, { QMetaType::Bool, 42 },
        }}),
        // Method 'requestEffectPreview'
        QtMocHelpers::MethodData<QString(const QString &, const QString &)>(181, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 26 }, { QMetaType::QString, 41 },
        }}),
        // Method 'defaultAppSettings'
        QtMocHelpers::MethodData<QVariantMap() const>(182, 2, QMC::AccessPublic, 0x80000000 | 104),
        // Method 'applyAppSettings'
        QtMocHelpers::MethodData<bool(const QVariantMap &)>(183, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 104, 130 },
        }}),
        // Method 'resetAppSettings'
        QtMocHelpers::MethodData<bool()>(184, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'clearMediaCache'
        QtMocHelpers::MethodData<bool()>(185, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setMediaColorSetting'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QVariant &)>(186, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 156 }, { 0x80000000 | 157, 158 },
        }}),
        // Method 'startPreviewDecode'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, qint64, qint64, int, int, double, bool, double, const QString &)>(187, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 51 }, { QMetaType::QString, 188 }, { QMetaType::LongLong, 189 }, { QMetaType::LongLong, 190 },
            { QMetaType::Int, 191 }, { QMetaType::Int, 192 }, { QMetaType::Double, 193 }, { QMetaType::Bool, 194 },
            { QMetaType::Double, 195 }, { QMetaType::QString, 196 },
        }}),
        // Method 'startPreviewDecode'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, qint64, qint64, int, int, double, bool, double)>(187, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 51 }, { QMetaType::QString, 188 }, { QMetaType::LongLong, 189 }, { QMetaType::LongLong, 190 },
            { QMetaType::Int, 191 }, { QMetaType::Int, 192 }, { QMetaType::Double, 193 }, { QMetaType::Bool, 194 },
            { QMetaType::Double, 195 },
        }}),
        // Method 'requestPreviewFrame'
        QtMocHelpers::MethodData<bool(const QString &, qint64, int, int)>(197, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 51 }, { QMetaType::LongLong, 189 }, { QMetaType::Int, 191 }, { QMetaType::Int, 192 },
        }}),
        // Method 'prewarmPreviewSource'
        QtMocHelpers::MethodData<void(const QString &, int, int)>(198, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 51 }, { QMetaType::Int, 191 }, { QMetaType::Int, 192 },
        }}),
        // Method 'prewarmPreviewSource'
        QtMocHelpers::MethodData<void(const QString &, int)>(198, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 51 }, { QMetaType::Int, 191 },
        }}),
        // Method 'prewarmPreviewSource'
        QtMocHelpers::MethodData<void(const QString &)>(198, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'releasePreviewSource'
        QtMocHelpers::MethodData<void(const QString &)>(199, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'previewDecodeStatistics'
        QtMocHelpers::MethodData<QVariantMap() const>(200, 2, QMC::AccessPublic, 0x80000000 | 104),
        // Method 'stopPreviewDecode'
        QtMocHelpers::MethodData<void()>(201, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'markGuiScope'
        QtMocHelpers::MethodData<void(const QString &) const>(202, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 203 },
        }}),
        // Method 'timelineTileToken'
        QtMocHelpers::MethodData<QString(const QString &)>(204, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'timelineTilesAvailable'
        QtMocHelpers::MethodData<bool() const>(205, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'waveformWindowToken'
        QtMocHelpers::MethodData<QString(const QString &)>(206, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 51 },
        }}),
        // Method 'waveformWindowsAvailable'
        QtMocHelpers::MethodData<bool() const>(207, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setPreviewSurfaceSize'
        QtMocHelpers::MethodData<void(int, int)>(208, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 55 }, { QMetaType::Int, 56 },
        }}),
        // Method 'beginTimelineInteraction'
        QtMocHelpers::MethodData<void()>(209, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'touchTimelineInteraction'
        QtMocHelpers::MethodData<void()>(210, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'endTimelineInteraction'
        QtMocHelpers::MethodData<void()>(211, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'coreVersion'
        QtMocHelpers::PropertyData<QString>(212, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Constant),
        // property 'projectId'
        QtMocHelpers::PropertyData<QString>(213, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'projectName'
        QtMocHelpers::PropertyData<QString>(214, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'projectLocation'
        QtMocHelpers::PropertyData<QString>(215, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'projectFile'
        QtMocHelpers::PropertyData<QString>(216, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'sequenceId'
        QtMocHelpers::PropertyData<QString>(217, QMetaType::QString, QMC::DefaultPropertyFlags, 1),
        // property 'sequenceName'
        QtMocHelpers::PropertyData<QString>(62, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'activeWorkspace'
        QtMocHelpers::PropertyData<QString>(218, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'layoutPreset'
        QtMocHelpers::PropertyData<QString>(219, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 16),
        // property 'captionFontFamily'
        QtMocHelpers::PropertyData<QString>(220, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'downloadedCaptionFonts'
        QtMocHelpers::PropertyData<QStringList>(221, QMetaType::QStringList, QMC::DefaultPropertyFlags, 18),
        // property 'captionFontSize'
        QtMocHelpers::PropertyData<int>(222, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionTextColor'
        QtMocHelpers::PropertyData<QString>(223, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionBold'
        QtMocHelpers::PropertyData<bool>(224, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionItalic'
        QtMocHelpers::PropertyData<bool>(225, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionBackgroundVisible'
        QtMocHelpers::PropertyData<bool>(226, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionBackgroundColor'
        QtMocHelpers::PropertyData<QString>(227, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionPosition'
        QtMocHelpers::PropertyData<QString>(228, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionAlignment'
        QtMocHelpers::PropertyData<QString>(229, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionPositionX'
        QtMocHelpers::PropertyData<double>(230, QMetaType::Double, QMC::DefaultPropertyFlags, 17),
        // property 'captionPositionY'
        QtMocHelpers::PropertyData<double>(231, QMetaType::Double, QMC::DefaultPropertyFlags, 17),
        // property 'captionBlurEnabled'
        QtMocHelpers::PropertyData<bool>(232, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionBlurTrackingEnabled'
        QtMocHelpers::PropertyData<bool>(233, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionBlurRegionX'
        QtMocHelpers::PropertyData<double>(234, QMetaType::Double, QMC::DefaultPropertyFlags, 17),
        // property 'captionBlurRegionY'
        QtMocHelpers::PropertyData<double>(235, QMetaType::Double, QMC::DefaultPropertyFlags, 17),
        // property 'captionBlurRegionWidth'
        QtMocHelpers::PropertyData<double>(236, QMetaType::Double, QMC::DefaultPropertyFlags, 17),
        // property 'captionBlurRegionHeight'
        QtMocHelpers::PropertyData<double>(237, QMetaType::Double, QMC::DefaultPropertyFlags, 17),
        // property 'captionBlurStrength'
        QtMocHelpers::PropertyData<int>(238, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'captionBlurPadding'
        QtMocHelpers::PropertyData<int>(239, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'media'
        QtMocHelpers::PropertyData<QVariantList>(240, 0x80000000 | 164, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 2),
        // property 'clips'
        QtMocHelpers::PropertyData<QVariantList>(241, 0x80000000 | 164, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 6),
        // property 'timelineClipModel'
        QtMocHelpers::PropertyData<TimelineClipModel*>(242, 0x80000000 | 243, QMC::DefaultPropertyFlags | QMC::EnumOrFlag | QMC::Constant),
        // property 'hasSubtitleClips'
        QtMocHelpers::PropertyData<bool>(244, QMetaType::Bool, QMC::DefaultPropertyFlags, 6),
        // property 'videoTrackCount'
        QtMocHelpers::PropertyData<int>(245, QMetaType::Int, QMC::DefaultPropertyFlags, 7),
        // property 'audioTrackCount'
        QtMocHelpers::PropertyData<int>(246, QMetaType::Int, QMC::DefaultPropertyFlags, 7),
        // property 'mutedTracks'
        QtMocHelpers::PropertyData<QStringList>(247, QMetaType::QStringList, QMC::DefaultPropertyFlags, 7),
        // property 'trackStates'
        QtMocHelpers::PropertyData<QVariantList>(248, 0x80000000 | 164, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 7),
        // property 'markers'
        QtMocHelpers::PropertyData<QVariantList>(249, 0x80000000 | 164, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 8),
        // property 'snappingEnabled'
        QtMocHelpers::PropertyData<bool>(250, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 9),
        // property 'mediaCount'
        QtMocHelpers::PropertyData<int>(251, QMetaType::Int, QMC::DefaultPropertyFlags, 2),
        // property 'mediaImportInProgress'
        QtMocHelpers::PropertyData<bool>(252, QMetaType::Bool, QMC::DefaultPropertyFlags, 3),
        // property 'mediaImportProgress'
        QtMocHelpers::PropertyData<int>(253, QMetaType::Int, QMC::DefaultPropertyFlags, 3),
        // property 'timelinePlacementInProgress'
        QtMocHelpers::PropertyData<bool>(254, QMetaType::Bool, QMC::DefaultPropertyFlags, 4),
        // property 'timelinePlacementProgress'
        QtMocHelpers::PropertyData<double>(255, QMetaType::Double, QMC::DefaultPropertyFlags, 4),
        // property 'timelinePlacementStatus'
        QtMocHelpers::PropertyData<QString>(256, QMetaType::QString, QMC::DefaultPropertyFlags, 4),
        // property 'hasSequence'
        QtMocHelpers::PropertyData<bool>(257, QMetaType::Bool, QMC::DefaultPropertyFlags, 1),
        // property 'hasMedia'
        QtMocHelpers::PropertyData<bool>(258, QMetaType::Bool, QMC::DefaultPropertyFlags, 2),
        // property 'canExport'
        QtMocHelpers::PropertyData<bool>(259, QMetaType::Bool, QMC::DefaultPropertyFlags, 10),
        // property 'durationMs'
        QtMocHelpers::PropertyData<qint64>(190, QMetaType::LongLong, QMC::DefaultPropertyFlags, 10),
        // property 'playheadMs'
        QtMocHelpers::PropertyData<qint64>(260, QMetaType::LongLong, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 11),
        // property 'playing'
        QtMocHelpers::PropertyData<bool>(261, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 12),
        // property 'dirty'
        QtMocHelpers::PropertyData<bool>(262, QMetaType::Bool, QMC::DefaultPropertyFlags, 13),
        // property 'canUndo'
        QtMocHelpers::PropertyData<bool>(263, QMetaType::Bool, QMC::DefaultPropertyFlags, 14),
        // property 'canRedo'
        QtMocHelpers::PropertyData<bool>(264, QMetaType::Bool, QMC::DefaultPropertyFlags, 14),
        // property 'exportInProgress'
        QtMocHelpers::PropertyData<bool>(265, QMetaType::Bool, QMC::DefaultPropertyFlags, 19),
        // property 'exportProgress'
        QtMocHelpers::PropertyData<double>(266, QMetaType::Double, QMC::DefaultPropertyFlags, 19),
        // property 'exportStatus'
        QtMocHelpers::PropertyData<QString>(267, QMetaType::QString, QMC::DefaultPropertyFlags, 19),
        // property 'transcript'
        QtMocHelpers::PropertyData<QVariantList>(268, 0x80000000 | 164, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 25),
        // property 'transcriptionInProgress'
        QtMocHelpers::PropertyData<bool>(269, QMetaType::Bool, QMC::DefaultPropertyFlags, 25),
        // property 'transcriptionStatus'
        QtMocHelpers::PropertyData<QString>(270, QMetaType::QString, QMC::DefaultPropertyFlags, 25),
        // property 'transcriptionProgress'
        QtMocHelpers::PropertyData<double>(271, QMetaType::Double, QMC::DefaultPropertyFlags, 25),
        // property 'transcriptLanguage'
        QtMocHelpers::PropertyData<QString>(272, QMetaType::QString, QMC::DefaultPropertyFlags, 25),
        // property 'transcribedMediaIds'
        QtMocHelpers::PropertyData<QStringList>(273, QMetaType::QStringList, QMC::DefaultPropertyFlags, 25),
        // property 'downloadedWhisperModels'
        QtMocHelpers::PropertyData<QStringList>(274, QMetaType::QStringList, QMC::DefaultPropertyFlags, 26),
        // property 'translationInProgress'
        QtMocHelpers::PropertyData<bool>(275, QMetaType::Bool, QMC::DefaultPropertyFlags, 25),
        // property 'translationStatus'
        QtMocHelpers::PropertyData<QString>(276, QMetaType::QString, QMC::DefaultPropertyFlags, 25),
        // property 'translationTestInProgress'
        QtMocHelpers::PropertyData<bool>(277, QMetaType::Bool, QMC::DefaultPropertyFlags, 25),
        // property 'demucsInProgress'
        QtMocHelpers::PropertyData<bool>(278, QMetaType::Bool, QMC::DefaultPropertyFlags, 20),
        // property 'demucsProgress'
        QtMocHelpers::PropertyData<double>(279, QMetaType::Double, QMC::DefaultPropertyFlags, 20),
        // property 'demucsStatus'
        QtMocHelpers::PropertyData<QString>(280, QMetaType::QString, QMC::DefaultPropertyFlags, 20),
        // property 'lastError'
        QtMocHelpers::PropertyData<QString>(281, QMetaType::QString, QMC::DefaultPropertyFlags, 22),
        // property 'colorSettings'
        QtMocHelpers::PropertyData<QVariantMap>(282, 0x80000000 | 104, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 27),
        // property 'activeColorClip'
        QtMocHelpers::PropertyData<QVariantMap>(283, 0x80000000 | 104, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 27),
        // property 'activeColorMedia'
        QtMocHelpers::PropertyData<QVariantMap>(284, 0x80000000 | 104, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 27),
        // property 'selectedClipId'
        QtMocHelpers::PropertyData<QString>(285, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 28),
        // property 'selectedClip'
        QtMocHelpers::PropertyData<QVariantMap>(286, 0x80000000 | 104, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 29),
        // property 'effectDefinitions'
        QtMocHelpers::PropertyData<QVariantList>(287, 0x80000000 | 164, QMC::DefaultPropertyFlags | QMC::EnumOrFlag | QMC::Constant),
        // property 'keyframeEngine'
        QtMocHelpers::PropertyData<KeyframeEngine*>(288, 0x80000000 | 289, QMC::DefaultPropertyFlags | QMC::EnumOrFlag | QMC::Constant),
        // property 'textToSpeechEngine'
        QtMocHelpers::PropertyData<TextToSpeechEngine*>(290, 0x80000000 | 291, QMC::DefaultPropertyFlags | QMC::EnumOrFlag | QMC::Constant),
        // property 'videoPreviewHelper'
        QtMocHelpers::PropertyData<VideoPreviewHelper*>(292, 0x80000000 | 293, QMC::DefaultPropertyFlags | QMC::EnumOrFlag | QMC::Constant),
        // property 'customBlurEditClipId'
        QtMocHelpers::PropertyData<QString>(294, QMetaType::QString, QMC::DefaultPropertyFlags, 31),
        // property 'customBlurEditInstanceId'
        QtMocHelpers::PropertyData<QString>(295, QMetaType::QString, QMC::DefaultPropertyFlags, 31),
        // property 'appSettings'
        QtMocHelpers::PropertyData<QVariantMap>(296, 0x80000000 | 104, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 34),
        // property 'mediaCachePath'
        QtMocHelpers::PropertyData<QString>(297, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Constant),
        // property 'mediaCacheSize'
        QtMocHelpers::PropertyData<QString>(298, QMetaType::QString, QMC::DefaultPropertyFlags, 35),
        // property 'previewFrameUrl'
        QtMocHelpers::PropertyData<QString>(299, QMetaType::QString, QMC::DefaultPropertyFlags, 36),
        // property 'previewDecoding'
        QtMocHelpers::PropertyData<bool>(300, QMetaType::Bool, QMC::DefaultPropertyFlags, 37),
        // property 'previewError'
        QtMocHelpers::PropertyData<QString>(301, QMetaType::QString, QMC::DefaultPropertyFlags, 38),
        // property 'timelineInteractionActive'
        QtMocHelpers::PropertyData<bool>(302, QMetaType::Bool, QMC::DefaultPropertyFlags, 39),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<Backend, qt_meta_tag_ZN7BackendE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject Backend::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7BackendE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7BackendE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN7BackendE_t>.metaTypes,
    nullptr
} };

void Backend::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Backend *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->projectChanged(); break;
        case 1: _t->sequenceChanged(); break;
        case 2: _t->mediaChanged(); break;
        case 3: _t->mediaImportChanged(); break;
        case 4: _t->timelinePlacementChanged(); break;
        case 5: _t->timelinePlacementFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2]))); break;
        case 6: _t->clipsChanged(); break;
        case 7: _t->tracksChanged(); break;
        case 8: _t->markersChanged(); break;
        case 9: _t->snappingChanged(); break;
        case 10: _t->timelineChanged(); break;
        case 11: _t->playheadChanged(); break;
        case 12: _t->playingChanged(); break;
        case 13: _t->dirtyChanged(); break;
        case 14: _t->historyChanged(); break;
        case 15: _t->activeWorkspaceChanged(); break;
        case 16: _t->layoutPresetChanged(); break;
        case 17: _t->captionStyleChanged(); break;
        case 18: _t->captionFontsChanged(); break;
        case 19: _t->exportStateChanged(); break;
        case 20: _t->demucsChanged(); break;
        case 21: _t->demucsFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 22: _t->errorChanged(); break;
        case 23: _t->exportFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 24: _t->transcriptionFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 25: _t->transcriptChanged(); break;
        case 26: _t->whisperModelsChanged(); break;
        case 27: _t->colorSettingsChanged(); break;
        case 28: _t->selectionChanged(); break;
        case 29: _t->selectionDetailChanged(); break;
        case 30: _t->effectControlsRequested(); break;
        case 31: _t->customBlurEditChanged(); break;
        case 32: _t->effectsBrowserRequested(); break;
        case 33: _t->effectPreviewReady((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 34: _t->appSettingsChanged(); break;
        case 35: _t->mediaCacheChanged(); break;
        case 36: _t->previewFrameChanged(); break;
        case 37: _t->previewStateChanged(); break;
        case 38: _t->previewErrorChanged(); break;
        case 39: _t->timelineInteractionChanged(); break;
        case 40: _t->autoSaveCompleted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 41: _t->setCaptionBlurRegionNormalized((*reinterpret_cast<std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[4]))); break;
        case 42: _t->setCaptionPositionNormalized((*reinterpret_cast<std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2]))); break;
        case 43: { bool _r = _t->newProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 44: { bool _r = _t->newProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 45: { bool _r = _t->newProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 46: { bool _r = _t->newProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 47: { bool _r = _t->newProject();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 48: { int _r = _t->importMedia((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 49: { int _r = _t->importMedia((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 50: _t->importMediaAsync((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 51: _t->importMediaAsync((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 52: _t->cancelMediaImport(); break;
        case 53: { bool _r = _t->removeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 54: { bool _r = _t->removeMediaSelection((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 55: { bool _r = _t->renameMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 56: { bool _r = _t->openMediaExternally((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 57: { bool _r = _t->revealMediaInFileManager((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 58: { bool _r = _t->copyMediaPath((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 59: { bool _r = _t->createSequence((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 60: { bool _r = _t->createSequence();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 61: { QString _r = _t->addClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 62: { QString _r = _t->addClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 63: { QString _r = _t->addClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 64: { QStringList _r = _t->addMediaToTimeline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 65: { QStringList _r = _t->addMediaToTimeline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 66: { QStringList _r = _t->addMediaToTimeline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 67: { bool _r = _t->addMediaSelectionToTimeline((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 68: { bool _r = _t->beginTimelinePlacement((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 69: _t->cancelTimelinePlacement(); break;
        case 70: { bool _r = _t->moveClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 71: { bool _r = _t->moveClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 72: { bool _r = _t->moveClips((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 73: { bool _r = _t->moveClips((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 74: { bool _r = _t->splitClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 75: { bool _r = _t->trimClipStart((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 76: { bool _r = _t->trimClipEnd((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 77: { bool _r = _t->deleteClipLeft((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 78: { bool _r = _t->deleteClipRight((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 79: { bool _r = _t->removeClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 80: { bool _r = _t->removeClips((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 81: { QString _r = _t->addTrack((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 82: { bool _r = _t->removeLastTrack((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 83: { bool _r = _t->setTrackMuted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 84: { bool _r = _t->setTrackState((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 85: { QVariantMap _r = _t->trackState((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 86: { bool _r = _t->trackLocked((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 87: { bool _r = _t->trackVisible((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 88: { bool _r = _t->trackSolo((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 89: { bool _r = _t->trackSyncLocked((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 90: { bool _r = _t->trackTargeted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 91: { qint64 _r = _t->snapTime((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 92: { qint64 _r = _t->snapTime((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 93: { qint64 _r = _t->snapTime((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 94: { bool _r = _t->rippleDeleteClips((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 95: { bool _r = _t->rippleTrimClipEnd((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 96: { bool _r = _t->closeGap((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 97: { QString _r = _t->addMarker((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 98: { QString _r = _t->addMarker((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 99: { QString _r = _t->addMarker((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 100: { bool _r = _t->updateMarker((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 101: { bool _r = _t->removeMarker((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 102: _t->setSnappingEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 103: { bool _r = _t->saveProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 104: { bool _r = _t->saveProject();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 105: { bool _r = _t->loadProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 106: { QVariantMap _r = _t->probeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 107: { QVariantMap _r = _t->probeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 108: { bool _r = _t->startExport((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 109: { bool _r = _t->startExport((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 110: { bool _r = _t->startExportWithSettings((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 111: { bool _r = _t->downloadCaptionFont((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 112: { QString _r = _t->suggestedExportPath();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 113: _t->cancelExport(); break;
        case 114: { bool _r = _t->transcribeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 115: { bool _r = _t->transcribeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 116: { bool _r = _t->transcribeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 117: _t->cancelTranscription(); break;
        case 118: _t->cancelDemucs(); break;
        case 119: { bool _r = _t->importSubtitles((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 120: { bool _r = _t->exportTranscriptSrt((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 121: { bool _r = _t->exportTranscriptTtml((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 122: { bool _r = _t->updateTranscriptSegment((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 123: { bool _r = _t->addTranscriptToTimeline();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 124: { bool _r = _t->removeTranscriptFromTimeline();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 125: { bool _r = _t->translateTranscript((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 126: { bool _r = _t->translateTranscript((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 127: { bool _r = _t->testTranslationProvider((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 128: { bool _r = _t->testTranslationProvider();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 129: _t->cancelTranslation(); break;
        case 130: _t->undo(); break;
        case 131: _t->redo(); break;
        case 132: _t->clearError(); break;
        case 133: { bool _r = _t->setColorSetting((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 134: { bool _r = _t->setClipColorSetting((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 135: { bool _r = _t->resetClipColorSettings((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 136: { bool _r = _t->setClipEffectSetting((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 137: { bool _r = _t->resetClipEffectSettings((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 138: { bool _r = _t->generateTimedTextToSpeech((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 139: { QString _r = _t->addClipEffect((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 140: { bool _r = _t->removeClipEffect((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 141: { bool _r = _t->moveClipEffect((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 142: { bool _r = _t->setClipEffectEnabled((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 143: { bool _r = _t->setClipEffectParameter((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 144: { bool _r = _t->resetClipEffectInstance((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 145: { bool _r = _t->beginCustomBlurMaskEdit((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 146: _t->endCustomBlurMaskEdit(); break;
        case 147: { bool _r = _t->setCustomBlurMask((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[6])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 148: _t->requestEffectControls((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 149: _t->requestEffectsBrowser((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 150: _t->requestEffectsBrowser(); break;
        case 151: { QString _r = _t->requestEffectPreview((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 152: { QString _r = _t->requestEffectPreview((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 153: { QVariantMap _r = _t->defaultAppSettings();
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 154: { bool _r = _t->applyAppSettings((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 155: { bool _r = _t->resetAppSettings();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 156: { bool _r = _t->clearMediaCache();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 157: { bool _r = _t->setMediaColorSetting((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 158: { bool _r = _t->startPreviewDecode((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[7])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[8])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[9])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[10])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 159: { bool _r = _t->startPreviewDecode((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[7])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[8])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[9])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 160: { bool _r = _t->requestPreviewFrame((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 161: _t->prewarmPreviewSource((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3]))); break;
        case 162: _t->prewarmPreviewSource((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 163: _t->prewarmPreviewSource((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 164: _t->releasePreviewSource((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 165: { QVariantMap _r = _t->previewDecodeStatistics();
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 166: _t->stopPreviewDecode(); break;
        case 167: _t->markGuiScope((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 168: { QString _r = _t->timelineTileToken((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 169: { bool _r = _t->timelineTilesAvailable();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 170: { QString _r = _t->waveformWindowToken((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 171: { bool _r = _t->waveformWindowsAvailable();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 172: _t->setPreviewSurfaceSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 173: _t->beginTimelineInteraction(); break;
        case 174: _t->touchTimelineInteraction(); break;
        case 175: _t->endTimelineInteraction(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::projectChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::sequenceChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::mediaChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::mediaImportChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::timelinePlacementChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)(bool , const QStringList & )>(_a, &Backend::timelinePlacementFinished, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::clipsChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::tracksChanged, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::markersChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::snappingChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::timelineChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::playheadChanged, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::playingChanged, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::dirtyChanged, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::historyChanged, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::activeWorkspaceChanged, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::layoutPresetChanged, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::captionStyleChanged, 17))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::captionFontsChanged, 18))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::exportStateChanged, 19))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::demucsChanged, 20))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)(bool , const QString & )>(_a, &Backend::demucsFinished, 21))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::errorChanged, 22))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)(bool , const QString & )>(_a, &Backend::exportFinished, 23))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)(bool , const QString & )>(_a, &Backend::transcriptionFinished, 24))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::transcriptChanged, 25))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::whisperModelsChanged, 26))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::colorSettingsChanged, 27))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::selectionChanged, 28))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::selectionDetailChanged, 29))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::effectControlsRequested, 30))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::customBlurEditChanged, 31))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::effectsBrowserRequested, 32))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)(const QString & , const QString & , bool , const QString & )>(_a, &Backend::effectPreviewReady, 33))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::appSettingsChanged, 34))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::mediaCacheChanged, 35))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::previewFrameChanged, 36))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::previewStateChanged, 37))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::previewErrorChanged, 38))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::timelineInteractionChanged, 39))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)(const QString & )>(_a, &Backend::autoSaveCompleted, 40))
            return;
    }
    if (_c == QMetaObject::RegisterPropertyMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 77:
            *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< KeyframeEngine* >(); break;
        case 78:
            *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< TextToSpeechEngine* >(); break;
        case 31:
            *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< TimelineClipModel* >(); break;
        case 79:
            *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< VideoPreviewHelper* >(); break;
        }
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->coreVersion(); break;
        case 1: *reinterpret_cast<QString*>(_v) = _t->projectId(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->projectName(); break;
        case 3: *reinterpret_cast<QString*>(_v) = _t->projectLocation(); break;
        case 4: *reinterpret_cast<QString*>(_v) = _t->projectFile(); break;
        case 5: *reinterpret_cast<QString*>(_v) = _t->sequenceId(); break;
        case 6: *reinterpret_cast<QString*>(_v) = _t->sequenceName(); break;
        case 7: *reinterpret_cast<QString*>(_v) = _t->activeWorkspace(); break;
        case 8: *reinterpret_cast<QString*>(_v) = _t->layoutPreset(); break;
        case 9: *reinterpret_cast<QString*>(_v) = _t->captionFontFamily(); break;
        case 10: *reinterpret_cast<QStringList*>(_v) = _t->downloadedCaptionFonts(); break;
        case 11: *reinterpret_cast<int*>(_v) = _t->captionFontSize(); break;
        case 12: *reinterpret_cast<QString*>(_v) = _t->captionTextColor(); break;
        case 13: *reinterpret_cast<bool*>(_v) = _t->captionBold(); break;
        case 14: *reinterpret_cast<bool*>(_v) = _t->captionItalic(); break;
        case 15: *reinterpret_cast<bool*>(_v) = _t->captionBackgroundVisible(); break;
        case 16: *reinterpret_cast<QString*>(_v) = _t->captionBackgroundColor(); break;
        case 17: *reinterpret_cast<QString*>(_v) = _t->captionPosition(); break;
        case 18: *reinterpret_cast<QString*>(_v) = _t->captionAlignment(); break;
        case 19: *reinterpret_cast<double*>(_v) = _t->captionPositionX(); break;
        case 20: *reinterpret_cast<double*>(_v) = _t->captionPositionY(); break;
        case 21: *reinterpret_cast<bool*>(_v) = _t->captionBlurEnabled(); break;
        case 22: *reinterpret_cast<bool*>(_v) = _t->captionBlurTrackingEnabled(); break;
        case 23: *reinterpret_cast<double*>(_v) = _t->captionBlurRegionX(); break;
        case 24: *reinterpret_cast<double*>(_v) = _t->captionBlurRegionY(); break;
        case 25: *reinterpret_cast<double*>(_v) = _t->captionBlurRegionWidth(); break;
        case 26: *reinterpret_cast<double*>(_v) = _t->captionBlurRegionHeight(); break;
        case 27: *reinterpret_cast<int*>(_v) = _t->captionBlurStrength(); break;
        case 28: *reinterpret_cast<int*>(_v) = _t->captionBlurPadding(); break;
        case 29: *reinterpret_cast<QVariantList*>(_v) = _t->media(); break;
        case 30: *reinterpret_cast<QVariantList*>(_v) = _t->clips(); break;
        case 31: *reinterpret_cast<TimelineClipModel**>(_v) = _t->timelineClipModel(); break;
        case 32: *reinterpret_cast<bool*>(_v) = _t->hasSubtitleClips(); break;
        case 33: *reinterpret_cast<int*>(_v) = _t->videoTrackCount(); break;
        case 34: *reinterpret_cast<int*>(_v) = _t->audioTrackCount(); break;
        case 35: *reinterpret_cast<QStringList*>(_v) = _t->mutedTracks(); break;
        case 36: *reinterpret_cast<QVariantList*>(_v) = _t->trackStates(); break;
        case 37: *reinterpret_cast<QVariantList*>(_v) = _t->markers(); break;
        case 38: *reinterpret_cast<bool*>(_v) = _t->snappingEnabled(); break;
        case 39: *reinterpret_cast<int*>(_v) = _t->mediaCount(); break;
        case 40: *reinterpret_cast<bool*>(_v) = _t->mediaImportInProgress(); break;
        case 41: *reinterpret_cast<int*>(_v) = _t->mediaImportProgress(); break;
        case 42: *reinterpret_cast<bool*>(_v) = _t->timelinePlacementInProgress(); break;
        case 43: *reinterpret_cast<double*>(_v) = _t->timelinePlacementProgress(); break;
        case 44: *reinterpret_cast<QString*>(_v) = _t->timelinePlacementStatus(); break;
        case 45: *reinterpret_cast<bool*>(_v) = _t->hasSequence(); break;
        case 46: *reinterpret_cast<bool*>(_v) = _t->hasMedia(); break;
        case 47: *reinterpret_cast<bool*>(_v) = _t->canExport(); break;
        case 48: *reinterpret_cast<qint64*>(_v) = _t->durationMs(); break;
        case 49: *reinterpret_cast<qint64*>(_v) = _t->playheadMs(); break;
        case 50: *reinterpret_cast<bool*>(_v) = _t->playing(); break;
        case 51: *reinterpret_cast<bool*>(_v) = _t->dirty(); break;
        case 52: *reinterpret_cast<bool*>(_v) = _t->canUndo(); break;
        case 53: *reinterpret_cast<bool*>(_v) = _t->canRedo(); break;
        case 54: *reinterpret_cast<bool*>(_v) = _t->exportInProgress(); break;
        case 55: *reinterpret_cast<double*>(_v) = _t->exportProgress(); break;
        case 56: *reinterpret_cast<QString*>(_v) = _t->exportStatus(); break;
        case 57: *reinterpret_cast<QVariantList*>(_v) = _t->transcript(); break;
        case 58: *reinterpret_cast<bool*>(_v) = _t->transcriptionInProgress(); break;
        case 59: *reinterpret_cast<QString*>(_v) = _t->transcriptionStatus(); break;
        case 60: *reinterpret_cast<double*>(_v) = _t->transcriptionProgress(); break;
        case 61: *reinterpret_cast<QString*>(_v) = _t->transcriptLanguage(); break;
        case 62: *reinterpret_cast<QStringList*>(_v) = _t->transcribedMediaIds(); break;
        case 63: *reinterpret_cast<QStringList*>(_v) = _t->downloadedWhisperModels(); break;
        case 64: *reinterpret_cast<bool*>(_v) = _t->translationInProgress(); break;
        case 65: *reinterpret_cast<QString*>(_v) = _t->translationStatus(); break;
        case 66: *reinterpret_cast<bool*>(_v) = _t->translationTestInProgress(); break;
        case 67: *reinterpret_cast<bool*>(_v) = _t->demucsInProgress(); break;
        case 68: *reinterpret_cast<double*>(_v) = _t->demucsProgress(); break;
        case 69: *reinterpret_cast<QString*>(_v) = _t->demucsStatus(); break;
        case 70: *reinterpret_cast<QString*>(_v) = _t->lastError(); break;
        case 71: *reinterpret_cast<QVariantMap*>(_v) = _t->colorSettings(); break;
        case 72: *reinterpret_cast<QVariantMap*>(_v) = _t->activeColorClip(); break;
        case 73: *reinterpret_cast<QVariantMap*>(_v) = _t->activeColorMedia(); break;
        case 74: *reinterpret_cast<QString*>(_v) = _t->selectedClipId(); break;
        case 75: *reinterpret_cast<QVariantMap*>(_v) = _t->selectedClip(); break;
        case 76: *reinterpret_cast<QVariantList*>(_v) = _t->effectDefinitions(); break;
        case 77: *reinterpret_cast<KeyframeEngine**>(_v) = _t->keyframeEngine(); break;
        case 78: *reinterpret_cast<TextToSpeechEngine**>(_v) = _t->textToSpeechEngine(); break;
        case 79: *reinterpret_cast<VideoPreviewHelper**>(_v) = _t->videoPreviewHelper(); break;
        case 80: *reinterpret_cast<QString*>(_v) = _t->customBlurEditClipId(); break;
        case 81: *reinterpret_cast<QString*>(_v) = _t->customBlurEditInstanceId(); break;
        case 82: *reinterpret_cast<QVariantMap*>(_v) = _t->appSettings(); break;
        case 83: *reinterpret_cast<QString*>(_v) = _t->mediaCachePath(); break;
        case 84: *reinterpret_cast<QString*>(_v) = _t->mediaCacheSize(); break;
        case 85: *reinterpret_cast<QString*>(_v) = _t->previewFrameUrl(); break;
        case 86: *reinterpret_cast<bool*>(_v) = _t->previewDecoding(); break;
        case 87: *reinterpret_cast<QString*>(_v) = _t->previewError(); break;
        case 88: *reinterpret_cast<bool*>(_v) = _t->timelineInteractionActive(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 2: _t->setProjectName(*reinterpret_cast<QString*>(_v)); break;
        case 6: _t->setSequenceName(*reinterpret_cast<QString*>(_v)); break;
        case 7: _t->setActiveWorkspace(*reinterpret_cast<QString*>(_v)); break;
        case 8: _t->setLayoutPreset(*reinterpret_cast<QString*>(_v)); break;
        case 9: _t->setCaptionFontFamily(*reinterpret_cast<QString*>(_v)); break;
        case 11: _t->setCaptionFontSize(*reinterpret_cast<int*>(_v)); break;
        case 12: _t->setCaptionTextColor(*reinterpret_cast<QString*>(_v)); break;
        case 13: _t->setCaptionBold(*reinterpret_cast<bool*>(_v)); break;
        case 14: _t->setCaptionItalic(*reinterpret_cast<bool*>(_v)); break;
        case 15: _t->setCaptionBackgroundVisible(*reinterpret_cast<bool*>(_v)); break;
        case 16: _t->setCaptionBackgroundColor(*reinterpret_cast<QString*>(_v)); break;
        case 17: _t->setCaptionPosition(*reinterpret_cast<QString*>(_v)); break;
        case 18: _t->setCaptionAlignment(*reinterpret_cast<QString*>(_v)); break;
        case 21: _t->setCaptionBlurEnabled(*reinterpret_cast<bool*>(_v)); break;
        case 22: _t->setCaptionBlurTrackingEnabled(*reinterpret_cast<bool*>(_v)); break;
        case 27: _t->setCaptionBlurStrength(*reinterpret_cast<int*>(_v)); break;
        case 28: _t->setCaptionBlurPadding(*reinterpret_cast<int*>(_v)); break;
        case 38: _t->setSnappingEnabled(*reinterpret_cast<bool*>(_v)); break;
        case 49: _t->setPlayheadMs(*reinterpret_cast<qint64*>(_v)); break;
        case 50: _t->setPlaying(*reinterpret_cast<bool*>(_v)); break;
        case 74: _t->setSelectedClipId(*reinterpret_cast<QString*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *Backend::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Backend::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7BackendE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Backend::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 176)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 176;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 176)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 176;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 89;
    }
    return _id;
}

// SIGNAL 0
void Backend::projectChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void Backend::sequenceChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void Backend::mediaChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void Backend::mediaImportChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void Backend::timelinePlacementChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void Backend::timelinePlacementFinished(bool _t1, const QStringList & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}

// SIGNAL 6
void Backend::clipsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void Backend::tracksChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void Backend::markersChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void Backend::snappingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void Backend::timelineChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void Backend::playheadChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}

// SIGNAL 12
void Backend::playingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}

// SIGNAL 13
void Backend::dirtyChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 13, nullptr);
}

// SIGNAL 14
void Backend::historyChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 14, nullptr);
}

// SIGNAL 15
void Backend::activeWorkspaceChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 15, nullptr);
}

// SIGNAL 16
void Backend::layoutPresetChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 16, nullptr);
}

// SIGNAL 17
void Backend::captionStyleChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 17, nullptr);
}

// SIGNAL 18
void Backend::captionFontsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 18, nullptr);
}

// SIGNAL 19
void Backend::exportStateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 19, nullptr);
}

// SIGNAL 20
void Backend::demucsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 20, nullptr);
}

// SIGNAL 21
void Backend::demucsFinished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 21, nullptr, _t1, _t2);
}

// SIGNAL 22
void Backend::errorChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 22, nullptr);
}

// SIGNAL 23
void Backend::exportFinished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 23, nullptr, _t1, _t2);
}

// SIGNAL 24
void Backend::transcriptionFinished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 24, nullptr, _t1, _t2);
}

// SIGNAL 25
void Backend::transcriptChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 25, nullptr);
}

// SIGNAL 26
void Backend::whisperModelsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 26, nullptr);
}

// SIGNAL 27
void Backend::colorSettingsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 27, nullptr);
}

// SIGNAL 28
void Backend::selectionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 28, nullptr);
}

// SIGNAL 29
void Backend::selectionDetailChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 29, nullptr);
}

// SIGNAL 30
void Backend::effectControlsRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 30, nullptr);
}

// SIGNAL 31
void Backend::customBlurEditChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 31, nullptr);
}

// SIGNAL 32
void Backend::effectsBrowserRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 32, nullptr);
}

// SIGNAL 33
void Backend::effectPreviewReady(const QString & _t1, const QString & _t2, bool _t3, const QString & _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 33, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 34
void Backend::appSettingsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 34, nullptr);
}

// SIGNAL 35
void Backend::mediaCacheChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 35, nullptr);
}

// SIGNAL 36
void Backend::previewFrameChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 36, nullptr);
}

// SIGNAL 37
void Backend::previewStateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 37, nullptr);
}

// SIGNAL 38
void Backend::previewErrorChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 38, nullptr);
}

// SIGNAL 39
void Backend::timelineInteractionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 39, nullptr);
}

// SIGNAL 40
void Backend::autoSaveCompleted(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 40, nullptr, _t1);
}
QT_WARNING_POP
