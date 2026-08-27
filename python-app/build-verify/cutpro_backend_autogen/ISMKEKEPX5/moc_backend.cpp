/****************************************************************************
** Meta object code from reading C++ file 'backend.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/app/backend.h"
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
        "errorChanged",
        "exportFinished",
        "success",
        "outputPath",
        "transcriptChanged",
        "colorSettingsChanged",
        "selectionChanged",
        "effectControlsRequested",
        "customBlurEditChanged",
        "effectsBrowserRequested",
        "effectPreviewReady",
        "clipId",
        "effectId",
        "animated",
        "url",
        "appSettingsChanged",
        "mediaCacheChanged",
        "previewFrameChanged",
        "previewStateChanged",
        "previewErrorChanged",
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
        "removeMedia",
        "mediaId",
        "addClip",
        "startMs",
        "track",
        "addMediaToTimeline",
        "moveClip",
        "moveClips",
        "clipIds",
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
        "importSubtitles",
        "exportTranscriptSrt",
        "updateTranscriptSegment",
        "index",
        "text",
        "addTranscriptToTimeline",
        "removeTranscriptFromTimeline",
        "translateTranscript",
        "targetLanguage",
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
        "requestPreviewFrame",
        "stopPreviewDecode",
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
        "QVariantList",
        "clips",
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
        "transcriptLanguage",
        "translationInProgress",
        "translationStatus",
        "lastError",
        "colorSettings",
        "activeColorClip",
        "activeColorMedia",
        "selectedClipId",
        "selectedClip",
        "effectDefinitions",
        "customBlurEditClipId",
        "customBlurEditInstanceId",
        "appSettings",
        "mediaCachePath",
        "mediaCacheSize",
        "previewFrameUrl",
        "previewDecoding",
        "previewError"
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
        // Signal 'clipsChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'tracksChanged'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'markersChanged'
        QtMocHelpers::SignalData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'snappingChanged'
        QtMocHelpers::SignalData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'timelineChanged'
        QtMocHelpers::SignalData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'playheadChanged'
        QtMocHelpers::SignalData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'playingChanged'
        QtMocHelpers::SignalData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dirtyChanged'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'historyChanged'
        QtMocHelpers::SignalData<void()>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'activeWorkspaceChanged'
        QtMocHelpers::SignalData<void()>(15, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'layoutPresetChanged'
        QtMocHelpers::SignalData<void()>(16, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'captionStyleChanged'
        QtMocHelpers::SignalData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'captionFontsChanged'
        QtMocHelpers::SignalData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'exportStateChanged'
        QtMocHelpers::SignalData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'errorChanged'
        QtMocHelpers::SignalData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'exportFinished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 22 }, { QMetaType::QString, 23 },
        }}),
        // Signal 'transcriptChanged'
        QtMocHelpers::SignalData<void()>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'colorSettingsChanged'
        QtMocHelpers::SignalData<void()>(25, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'selectionChanged'
        QtMocHelpers::SignalData<void()>(26, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'effectControlsRequested'
        QtMocHelpers::SignalData<void()>(27, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'customBlurEditChanged'
        QtMocHelpers::SignalData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'effectsBrowserRequested'
        QtMocHelpers::SignalData<void()>(29, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'effectPreviewReady'
        QtMocHelpers::SignalData<void(const QString &, const QString &, bool, const QString &)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 32 }, { QMetaType::Bool, 33 }, { QMetaType::QString, 34 },
        }}),
        // Signal 'appSettingsChanged'
        QtMocHelpers::SignalData<void()>(35, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'mediaCacheChanged'
        QtMocHelpers::SignalData<void()>(36, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'previewFrameChanged'
        QtMocHelpers::SignalData<void()>(37, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'previewStateChanged'
        QtMocHelpers::SignalData<void()>(38, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'previewErrorChanged'
        QtMocHelpers::SignalData<void()>(39, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'autoSaveCompleted'
        QtMocHelpers::SignalData<void(const QString &)>(40, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 41 },
        }}),
        // Method 'setCaptionBlurRegionNormalized'
        QtMocHelpers::MethodData<void(double, double, double, double)>(42, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 43 }, { QMetaType::Double, 44 }, { QMetaType::Double, 45 }, { QMetaType::Double, 46 },
        }}),
        // Method 'setCaptionPositionNormalized'
        QtMocHelpers::MethodData<void(double, double)>(47, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 43 }, { QMetaType::Double, 44 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, bool, const QString &)>(48, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 49 }, { QMetaType::QString, 50 }, { QMetaType::Bool, 51 }, { QMetaType::QString, 52 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, bool)>(48, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 49 }, { QMetaType::QString, 50 }, { QMetaType::Bool, 51 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(48, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 49 }, { QMetaType::QString, 50 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool(const QString &)>(48, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 49 },
        }}),
        // Method 'newProject'
        QtMocHelpers::MethodData<bool()>(48, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool),
        // Method 'importMedia'
        QtMocHelpers::MethodData<int(const QStringList &, bool)>(53, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::QStringList, 54 }, { QMetaType::Bool, 55 },
        }}),
        // Method 'importMedia'
        QtMocHelpers::MethodData<int(const QStringList &)>(53, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Int, {{
            { QMetaType::QStringList, 54 },
        }}),
        // Method 'importMediaAsync'
        QtMocHelpers::MethodData<void(const QStringList &, bool)>(56, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 54 }, { QMetaType::Bool, 55 },
        }}),
        // Method 'importMediaAsync'
        QtMocHelpers::MethodData<void(const QStringList &)>(56, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QStringList, 54 },
        }}),
        // Method 'removeMedia'
        QtMocHelpers::MethodData<bool(const QString &)>(57, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 58 },
        }}),
        // Method 'createSequence'
        QtMocHelpers::MethodData<bool(const QString &)>(51, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 49 },
        }}),
        // Method 'createSequence'
        QtMocHelpers::MethodData<bool()>(51, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool),
        // Method 'addClip'
        QtMocHelpers::MethodData<QString(const QString &, qint64, const QString &)>(59, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 58 }, { QMetaType::LongLong, 60 }, { QMetaType::QString, 61 },
        }}),
        // Method 'addClip'
        QtMocHelpers::MethodData<QString(const QString &, qint64)>(59, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 58 }, { QMetaType::LongLong, 60 },
        }}),
        // Method 'addClip'
        QtMocHelpers::MethodData<QString(const QString &)>(59, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 58 },
        }}),
        // Method 'addMediaToTimeline'
        QtMocHelpers::MethodData<QStringList(const QString &, qint64, const QString &)>(62, 2, QMC::AccessPublic, QMetaType::QStringList, {{
            { QMetaType::QString, 58 }, { QMetaType::LongLong, 60 }, { QMetaType::QString, 61 },
        }}),
        // Method 'addMediaToTimeline'
        QtMocHelpers::MethodData<QStringList(const QString &, qint64)>(62, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QStringList, {{
            { QMetaType::QString, 58 }, { QMetaType::LongLong, 60 },
        }}),
        // Method 'addMediaToTimeline'
        QtMocHelpers::MethodData<QStringList(const QString &)>(62, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QStringList, {{
            { QMetaType::QString, 58 },
        }}),
        // Method 'moveClip'
        QtMocHelpers::MethodData<bool(const QString &, qint64, const QString &)>(63, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 60 }, { QMetaType::QString, 61 },
        }}),
        // Method 'moveClip'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(63, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 60 },
        }}),
        // Method 'moveClips'
        QtMocHelpers::MethodData<bool(const QStringList &, qint64, int)>(64, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QStringList, 65 }, { QMetaType::LongLong, 66 }, { QMetaType::Int, 67 },
        }}),
        // Method 'moveClips'
        QtMocHelpers::MethodData<bool(const QStringList &, qint64)>(64, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QStringList, 65 }, { QMetaType::LongLong, 66 },
        }}),
        // Method 'splitClip'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(68, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 69 },
        }}),
        // Method 'trimClipStart'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(70, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 60 },
        }}),
        // Method 'trimClipEnd'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(71, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 72 },
        }}),
        // Method 'deleteClipLeft'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(73, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 69 },
        }}),
        // Method 'deleteClipRight'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(74, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 69 },
        }}),
        // Method 'removeClip'
        QtMocHelpers::MethodData<bool(const QString &)>(75, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'removeClips'
        QtMocHelpers::MethodData<bool(const QStringList &)>(76, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QStringList, 65 },
        }}),
        // Method 'addTrack'
        QtMocHelpers::MethodData<QString(const QString &)>(77, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 78 },
        }}),
        // Method 'removeLastTrack'
        QtMocHelpers::MethodData<bool(const QString &)>(79, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 78 },
        }}),
        // Method 'setTrackMuted'
        QtMocHelpers::MethodData<bool(const QString &, bool)>(80, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 61 }, { QMetaType::Bool, 81 },
        }}),
        // Method 'setTrackState'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, bool)>(82, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 61 }, { QMetaType::QString, 83 }, { QMetaType::Bool, 84 },
        }}),
        // Method 'trackState'
        QtMocHelpers::MethodData<QVariantMap(const QString &) const>(85, 2, QMC::AccessPublic, 0x80000000 | 86, {{
            { QMetaType::QString, 61 },
        }}),
        // Method 'trackLocked'
        QtMocHelpers::MethodData<bool(const QString &) const>(87, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 61 },
        }}),
        // Method 'trackVisible'
        QtMocHelpers::MethodData<bool(const QString &) const>(88, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 61 },
        }}),
        // Method 'trackSolo'
        QtMocHelpers::MethodData<bool(const QString &) const>(89, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 61 },
        }}),
        // Method 'trackSyncLocked'
        QtMocHelpers::MethodData<bool(const QString &) const>(90, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 61 },
        }}),
        // Method 'trackTargeted'
        QtMocHelpers::MethodData<bool(const QString &) const>(91, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 61 },
        }}),
        // Method 'snapTime'
        QtMocHelpers::MethodData<qint64(qint64, const QStringList &, qint64) const>(92, 2, QMC::AccessPublic, QMetaType::LongLong, {{
            { QMetaType::LongLong, 93 }, { QMetaType::QStringList, 94 }, { QMetaType::LongLong, 95 },
        }}),
        // Method 'snapTime'
        QtMocHelpers::MethodData<qint64(qint64, const QStringList &) const>(92, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::LongLong, {{
            { QMetaType::LongLong, 93 }, { QMetaType::QStringList, 94 },
        }}),
        // Method 'snapTime'
        QtMocHelpers::MethodData<qint64(qint64) const>(92, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::LongLong, {{
            { QMetaType::LongLong, 93 },
        }}),
        // Method 'rippleDeleteClips'
        QtMocHelpers::MethodData<bool(const QStringList &)>(96, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QStringList, 65 },
        }}),
        // Method 'rippleTrimClipEnd'
        QtMocHelpers::MethodData<bool(const QString &, qint64)>(97, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::LongLong, 72 },
        }}),
        // Method 'closeGap'
        QtMocHelpers::MethodData<bool(const QString &, qint64, qint64)>(98, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 61 }, { QMetaType::LongLong, 60 }, { QMetaType::LongLong, 72 },
        }}),
        // Method 'addMarker'
        QtMocHelpers::MethodData<QString(qint64, const QString &, const QString &)>(99, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::LongLong, 69 }, { QMetaType::QString, 49 }, { QMetaType::QString, 100 },
        }}),
        // Method 'addMarker'
        QtMocHelpers::MethodData<QString(qint64, const QString &)>(99, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::LongLong, 69 }, { QMetaType::QString, 49 },
        }}),
        // Method 'addMarker'
        QtMocHelpers::MethodData<QString(qint64)>(99, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::LongLong, 69 },
        }}),
        // Method 'updateMarker'
        QtMocHelpers::MethodData<bool(const QString &, qint64, const QString &, const QString &)>(101, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 102 }, { QMetaType::LongLong, 69 }, { QMetaType::QString, 49 }, { QMetaType::QString, 100 },
        }}),
        // Method 'removeMarker'
        QtMocHelpers::MethodData<bool(const QString &)>(103, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 102 },
        }}),
        // Method 'setSnappingEnabled'
        QtMocHelpers::MethodData<void(bool)>(104, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 84 },
        }}),
        // Method 'saveProject'
        QtMocHelpers::MethodData<bool(const QString &)>(105, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 41 },
        }}),
        // Method 'saveProject'
        QtMocHelpers::MethodData<bool()>(105, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool),
        // Method 'loadProject'
        QtMocHelpers::MethodData<bool(const QString &)>(106, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 41 },
        }}),
        // Method 'probeMedia'
        QtMocHelpers::MethodData<QVariantMap(const QString &)>(107, 2, QMC::AccessPublic, 0x80000000 | 86, {{
            { QMetaType::QString, 41 },
        }}),
        // Method 'startExport'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(108, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 23 }, { QMetaType::QString, 109 },
        }}),
        // Method 'startExport'
        QtMocHelpers::MethodData<bool(const QString &)>(108, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 23 },
        }}),
        // Method 'startExportWithSettings'
        QtMocHelpers::MethodData<bool(const QString &, const QVariantMap &)>(110, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 23 }, { 0x80000000 | 86, 111 },
        }}),
        // Method 'downloadCaptionFont'
        QtMocHelpers::MethodData<bool(const QString &)>(112, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 113 },
        }}),
        // Method 'suggestedExportPath'
        QtMocHelpers::MethodData<QString() const>(114, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'cancelExport'
        QtMocHelpers::MethodData<void()>(115, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'transcribeMedia'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QString &)>(116, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 58 }, { QMetaType::QString, 117 }, { QMetaType::QString, 118 },
        }}),
        // Method 'transcribeMedia'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(116, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 58 }, { QMetaType::QString, 117 },
        }}),
        // Method 'transcribeMedia'
        QtMocHelpers::MethodData<bool(const QString &)>(116, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 58 },
        }}),
        // Method 'cancelTranscription'
        QtMocHelpers::MethodData<void()>(119, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'importSubtitles'
        QtMocHelpers::MethodData<bool(const QString &)>(120, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 41 },
        }}),
        // Method 'exportTranscriptSrt'
        QtMocHelpers::MethodData<bool(const QString &)>(121, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 41 },
        }}),
        // Method 'updateTranscriptSegment'
        QtMocHelpers::MethodData<bool(int, const QString &)>(122, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::Int, 123 }, { QMetaType::QString, 124 },
        }}),
        // Method 'addTranscriptToTimeline'
        QtMocHelpers::MethodData<bool()>(125, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'removeTranscriptFromTimeline'
        QtMocHelpers::MethodData<bool()>(126, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'translateTranscript'
        QtMocHelpers::MethodData<bool(const QString &)>(127, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 128 },
        }}),
        // Method 'cancelTranslation'
        QtMocHelpers::MethodData<void()>(129, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'undo'
        QtMocHelpers::MethodData<void()>(130, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'redo'
        QtMocHelpers::MethodData<void()>(131, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'clearError'
        QtMocHelpers::MethodData<void()>(132, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setColorSetting'
        QtMocHelpers::MethodData<bool(const QString &, const QVariant &)>(133, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 134 }, { 0x80000000 | 135, 136 },
        }}),
        // Method 'setClipColorSetting'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QVariant &)>(137, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 134 }, { 0x80000000 | 135, 136 },
        }}),
        // Method 'resetClipColorSettings'
        QtMocHelpers::MethodData<bool(const QString &)>(138, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'setClipEffectSetting'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QVariant &)>(139, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 134 }, { 0x80000000 | 135, 136 },
        }}),
        // Method 'resetClipEffectSettings'
        QtMocHelpers::MethodData<bool(const QString &)>(140, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'addClipEffect'
        QtMocHelpers::MethodData<QString(const QString &, const QString &)>(141, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 32 },
        }}),
        // Method 'removeClipEffect'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(142, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 143 },
        }}),
        // Method 'moveClipEffect'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, int)>(144, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 143 }, { QMetaType::Int, 145 },
        }}),
        // Method 'setClipEffectEnabled'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, bool)>(146, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 143 }, { QMetaType::Bool, 84 },
        }}),
        // Method 'setClipEffectParameter'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QString &, const QVariant &)>(147, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 143 }, { QMetaType::QString, 148 }, { 0x80000000 | 135, 136 },
        }}),
        // Method 'resetClipEffectInstance'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(149, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 143 },
        }}),
        // Method 'beginCustomBlurMaskEdit'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(150, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 143 },
        }}),
        // Method 'endCustomBlurMaskEdit'
        QtMocHelpers::MethodData<void()>(151, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setCustomBlurMask'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, double, double, double, double)>(152, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 143 }, { QMetaType::Double, 43 }, { QMetaType::Double, 44 },
            { QMetaType::Double, 45 }, { QMetaType::Double, 46 },
        }}),
        // Method 'requestEffectControls'
        QtMocHelpers::MethodData<void(const QString &)>(153, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'requestEffectsBrowser'
        QtMocHelpers::MethodData<void(const QString &)>(154, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'requestEffectsBrowser'
        QtMocHelpers::MethodData<void()>(154, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Method 'requestEffectPreview'
        QtMocHelpers::MethodData<QString(const QString &, const QString &, bool)>(155, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 32 }, { QMetaType::Bool, 33 },
        }}),
        // Method 'requestEffectPreview'
        QtMocHelpers::MethodData<QString(const QString &, const QString &)>(155, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 31 }, { QMetaType::QString, 32 },
        }}),
        // Method 'defaultAppSettings'
        QtMocHelpers::MethodData<QVariantMap() const>(156, 2, QMC::AccessPublic, 0x80000000 | 86),
        // Method 'applyAppSettings'
        QtMocHelpers::MethodData<bool(const QVariantMap &)>(157, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 86, 111 },
        }}),
        // Method 'resetAppSettings'
        QtMocHelpers::MethodData<bool()>(158, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'clearMediaCache'
        QtMocHelpers::MethodData<bool()>(159, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setMediaColorSetting'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QVariant &)>(160, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 58 }, { QMetaType::QString, 134 }, { 0x80000000 | 135, 136 },
        }}),
        // Method 'startPreviewDecode'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, qint64, qint64, int, int, double, bool, double)>(161, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 41 }, { QMetaType::QString, 162 }, { QMetaType::LongLong, 163 }, { QMetaType::LongLong, 164 },
            { QMetaType::Int, 165 }, { QMetaType::Int, 166 }, { QMetaType::Double, 167 }, { QMetaType::Bool, 168 },
            { QMetaType::Double, 169 },
        }}),
        // Method 'requestPreviewFrame'
        QtMocHelpers::MethodData<bool(const QString &, qint64, int, int)>(170, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 41 }, { QMetaType::LongLong, 163 }, { QMetaType::Int, 165 }, { QMetaType::Int, 166 },
        }}),
        // Method 'stopPreviewDecode'
        QtMocHelpers::MethodData<void()>(171, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'coreVersion'
        QtMocHelpers::PropertyData<QString>(172, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Constant),
        // property 'projectId'
        QtMocHelpers::PropertyData<QString>(173, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'projectName'
        QtMocHelpers::PropertyData<QString>(174, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'projectLocation'
        QtMocHelpers::PropertyData<QString>(175, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'projectFile'
        QtMocHelpers::PropertyData<QString>(176, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'sequenceId'
        QtMocHelpers::PropertyData<QString>(177, QMetaType::QString, QMC::DefaultPropertyFlags, 1),
        // property 'sequenceName'
        QtMocHelpers::PropertyData<QString>(52, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'activeWorkspace'
        QtMocHelpers::PropertyData<QString>(178, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 13),
        // property 'layoutPreset'
        QtMocHelpers::PropertyData<QString>(179, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 14),
        // property 'captionFontFamily'
        QtMocHelpers::PropertyData<QString>(180, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'downloadedCaptionFonts'
        QtMocHelpers::PropertyData<QStringList>(181, QMetaType::QStringList, QMC::DefaultPropertyFlags, 16),
        // property 'captionFontSize'
        QtMocHelpers::PropertyData<int>(182, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionTextColor'
        QtMocHelpers::PropertyData<QString>(183, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionBold'
        QtMocHelpers::PropertyData<bool>(184, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionItalic'
        QtMocHelpers::PropertyData<bool>(185, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionBackgroundVisible'
        QtMocHelpers::PropertyData<bool>(186, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionBackgroundColor'
        QtMocHelpers::PropertyData<QString>(187, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionPosition'
        QtMocHelpers::PropertyData<QString>(188, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionAlignment'
        QtMocHelpers::PropertyData<QString>(189, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionPositionX'
        QtMocHelpers::PropertyData<double>(190, QMetaType::Double, QMC::DefaultPropertyFlags, 15),
        // property 'captionPositionY'
        QtMocHelpers::PropertyData<double>(191, QMetaType::Double, QMC::DefaultPropertyFlags, 15),
        // property 'captionBlurEnabled'
        QtMocHelpers::PropertyData<bool>(192, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionBlurTrackingEnabled'
        QtMocHelpers::PropertyData<bool>(193, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionBlurRegionX'
        QtMocHelpers::PropertyData<double>(194, QMetaType::Double, QMC::DefaultPropertyFlags, 15),
        // property 'captionBlurRegionY'
        QtMocHelpers::PropertyData<double>(195, QMetaType::Double, QMC::DefaultPropertyFlags, 15),
        // property 'captionBlurRegionWidth'
        QtMocHelpers::PropertyData<double>(196, QMetaType::Double, QMC::DefaultPropertyFlags, 15),
        // property 'captionBlurRegionHeight'
        QtMocHelpers::PropertyData<double>(197, QMetaType::Double, QMC::DefaultPropertyFlags, 15),
        // property 'captionBlurStrength'
        QtMocHelpers::PropertyData<int>(198, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'captionBlurPadding'
        QtMocHelpers::PropertyData<int>(199, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'media'
        QtMocHelpers::PropertyData<QVariantList>(200, 0x80000000 | 201, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 2),
        // property 'clips'
        QtMocHelpers::PropertyData<QVariantList>(202, 0x80000000 | 201, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 4),
        // property 'hasSubtitleClips'
        QtMocHelpers::PropertyData<bool>(203, QMetaType::Bool, QMC::DefaultPropertyFlags, 4),
        // property 'videoTrackCount'
        QtMocHelpers::PropertyData<int>(204, QMetaType::Int, QMC::DefaultPropertyFlags, 5),
        // property 'audioTrackCount'
        QtMocHelpers::PropertyData<int>(205, QMetaType::Int, QMC::DefaultPropertyFlags, 5),
        // property 'mutedTracks'
        QtMocHelpers::PropertyData<QStringList>(206, QMetaType::QStringList, QMC::DefaultPropertyFlags, 5),
        // property 'trackStates'
        QtMocHelpers::PropertyData<QVariantList>(207, 0x80000000 | 201, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 5),
        // property 'markers'
        QtMocHelpers::PropertyData<QVariantList>(208, 0x80000000 | 201, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 6),
        // property 'snappingEnabled'
        QtMocHelpers::PropertyData<bool>(209, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 7),
        // property 'mediaCount'
        QtMocHelpers::PropertyData<int>(210, QMetaType::Int, QMC::DefaultPropertyFlags, 2),
        // property 'mediaImportInProgress'
        QtMocHelpers::PropertyData<bool>(211, QMetaType::Bool, QMC::DefaultPropertyFlags, 3),
        // property 'mediaImportProgress'
        QtMocHelpers::PropertyData<int>(212, QMetaType::Int, QMC::DefaultPropertyFlags, 3),
        // property 'hasSequence'
        QtMocHelpers::PropertyData<bool>(213, QMetaType::Bool, QMC::DefaultPropertyFlags, 1),
        // property 'hasMedia'
        QtMocHelpers::PropertyData<bool>(214, QMetaType::Bool, QMC::DefaultPropertyFlags, 2),
        // property 'canExport'
        QtMocHelpers::PropertyData<bool>(215, QMetaType::Bool, QMC::DefaultPropertyFlags, 8),
        // property 'durationMs'
        QtMocHelpers::PropertyData<qint64>(164, QMetaType::LongLong, QMC::DefaultPropertyFlags, 8),
        // property 'playheadMs'
        QtMocHelpers::PropertyData<qint64>(216, QMetaType::LongLong, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 9),
        // property 'playing'
        QtMocHelpers::PropertyData<bool>(217, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 10),
        // property 'dirty'
        QtMocHelpers::PropertyData<bool>(218, QMetaType::Bool, QMC::DefaultPropertyFlags, 11),
        // property 'canUndo'
        QtMocHelpers::PropertyData<bool>(219, QMetaType::Bool, QMC::DefaultPropertyFlags, 12),
        // property 'canRedo'
        QtMocHelpers::PropertyData<bool>(220, QMetaType::Bool, QMC::DefaultPropertyFlags, 12),
        // property 'exportInProgress'
        QtMocHelpers::PropertyData<bool>(221, QMetaType::Bool, QMC::DefaultPropertyFlags, 17),
        // property 'exportProgress'
        QtMocHelpers::PropertyData<double>(222, QMetaType::Double, QMC::DefaultPropertyFlags, 17),
        // property 'exportStatus'
        QtMocHelpers::PropertyData<QString>(223, QMetaType::QString, QMC::DefaultPropertyFlags, 17),
        // property 'transcript'
        QtMocHelpers::PropertyData<QVariantList>(224, 0x80000000 | 201, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 20),
        // property 'transcriptionInProgress'
        QtMocHelpers::PropertyData<bool>(225, QMetaType::Bool, QMC::DefaultPropertyFlags, 20),
        // property 'transcriptionStatus'
        QtMocHelpers::PropertyData<QString>(226, QMetaType::QString, QMC::DefaultPropertyFlags, 20),
        // property 'transcriptLanguage'
        QtMocHelpers::PropertyData<QString>(227, QMetaType::QString, QMC::DefaultPropertyFlags, 20),
        // property 'translationInProgress'
        QtMocHelpers::PropertyData<bool>(228, QMetaType::Bool, QMC::DefaultPropertyFlags, 20),
        // property 'translationStatus'
        QtMocHelpers::PropertyData<QString>(229, QMetaType::QString, QMC::DefaultPropertyFlags, 20),
        // property 'lastError'
        QtMocHelpers::PropertyData<QString>(230, QMetaType::QString, QMC::DefaultPropertyFlags, 18),
        // property 'colorSettings'
        QtMocHelpers::PropertyData<QVariantMap>(231, 0x80000000 | 86, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 21),
        // property 'activeColorClip'
        QtMocHelpers::PropertyData<QVariantMap>(232, 0x80000000 | 86, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 21),
        // property 'activeColorMedia'
        QtMocHelpers::PropertyData<QVariantMap>(233, 0x80000000 | 86, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 21),
        // property 'selectedClipId'
        QtMocHelpers::PropertyData<QString>(234, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 22),
        // property 'selectedClip'
        QtMocHelpers::PropertyData<QVariantMap>(235, 0x80000000 | 86, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 22),
        // property 'effectDefinitions'
        QtMocHelpers::PropertyData<QVariantList>(236, 0x80000000 | 201, QMC::DefaultPropertyFlags | QMC::EnumOrFlag | QMC::Constant),
        // property 'customBlurEditClipId'
        QtMocHelpers::PropertyData<QString>(237, QMetaType::QString, QMC::DefaultPropertyFlags, 24),
        // property 'customBlurEditInstanceId'
        QtMocHelpers::PropertyData<QString>(238, QMetaType::QString, QMC::DefaultPropertyFlags, 24),
        // property 'appSettings'
        QtMocHelpers::PropertyData<QVariantMap>(239, 0x80000000 | 86, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 27),
        // property 'mediaCachePath'
        QtMocHelpers::PropertyData<QString>(240, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Constant),
        // property 'mediaCacheSize'
        QtMocHelpers::PropertyData<QString>(241, QMetaType::QString, QMC::DefaultPropertyFlags, 28),
        // property 'previewFrameUrl'
        QtMocHelpers::PropertyData<QString>(242, QMetaType::QString, QMC::DefaultPropertyFlags, 29),
        // property 'previewDecoding'
        QtMocHelpers::PropertyData<bool>(243, QMetaType::Bool, QMC::DefaultPropertyFlags, 30),
        // property 'previewError'
        QtMocHelpers::PropertyData<QString>(244, QMetaType::QString, QMC::DefaultPropertyFlags, 31),
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
        case 4: _t->clipsChanged(); break;
        case 5: _t->tracksChanged(); break;
        case 6: _t->markersChanged(); break;
        case 7: _t->snappingChanged(); break;
        case 8: _t->timelineChanged(); break;
        case 9: _t->playheadChanged(); break;
        case 10: _t->playingChanged(); break;
        case 11: _t->dirtyChanged(); break;
        case 12: _t->historyChanged(); break;
        case 13: _t->activeWorkspaceChanged(); break;
        case 14: _t->layoutPresetChanged(); break;
        case 15: _t->captionStyleChanged(); break;
        case 16: _t->captionFontsChanged(); break;
        case 17: _t->exportStateChanged(); break;
        case 18: _t->errorChanged(); break;
        case 19: _t->exportFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 20: _t->transcriptChanged(); break;
        case 21: _t->colorSettingsChanged(); break;
        case 22: _t->selectionChanged(); break;
        case 23: _t->effectControlsRequested(); break;
        case 24: _t->customBlurEditChanged(); break;
        case 25: _t->effectsBrowserRequested(); break;
        case 26: _t->effectPreviewReady((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 27: _t->appSettingsChanged(); break;
        case 28: _t->mediaCacheChanged(); break;
        case 29: _t->previewFrameChanged(); break;
        case 30: _t->previewStateChanged(); break;
        case 31: _t->previewErrorChanged(); break;
        case 32: _t->autoSaveCompleted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 33: _t->setCaptionBlurRegionNormalized((*reinterpret_cast<std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[4]))); break;
        case 34: _t->setCaptionPositionNormalized((*reinterpret_cast<std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2]))); break;
        case 35: { bool _r = _t->newProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 36: { bool _r = _t->newProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 37: { bool _r = _t->newProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 38: { bool _r = _t->newProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 39: { bool _r = _t->newProject();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 40: { int _r = _t->importMedia((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 41: { int _r = _t->importMedia((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 42: _t->importMediaAsync((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 43: _t->importMediaAsync((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 44: { bool _r = _t->removeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 45: { bool _r = _t->createSequence((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 46: { bool _r = _t->createSequence();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 47: { QString _r = _t->addClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 48: { QString _r = _t->addClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 49: { QString _r = _t->addClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 50: { QStringList _r = _t->addMediaToTimeline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 51: { QStringList _r = _t->addMediaToTimeline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 52: { QStringList _r = _t->addMediaToTimeline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 53: { bool _r = _t->moveClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 54: { bool _r = _t->moveClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 55: { bool _r = _t->moveClips((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 56: { bool _r = _t->moveClips((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 57: { bool _r = _t->splitClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 58: { bool _r = _t->trimClipStart((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 59: { bool _r = _t->trimClipEnd((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 60: { bool _r = _t->deleteClipLeft((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 61: { bool _r = _t->deleteClipRight((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 62: { bool _r = _t->removeClip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 63: { bool _r = _t->removeClips((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 64: { QString _r = _t->addTrack((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 65: { bool _r = _t->removeLastTrack((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 66: { bool _r = _t->setTrackMuted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 67: { bool _r = _t->setTrackState((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 68: { QVariantMap _r = _t->trackState((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 69: { bool _r = _t->trackLocked((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 70: { bool _r = _t->trackVisible((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 71: { bool _r = _t->trackSolo((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 72: { bool _r = _t->trackSyncLocked((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 73: { bool _r = _t->trackTargeted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 74: { qint64 _r = _t->snapTime((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 75: { qint64 _r = _t->snapTime((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 76: { qint64 _r = _t->snapTime((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 77: { bool _r = _t->rippleDeleteClips((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 78: { bool _r = _t->rippleTrimClipEnd((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 79: { bool _r = _t->closeGap((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 80: { QString _r = _t->addMarker((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 81: { QString _r = _t->addMarker((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 82: { QString _r = _t->addMarker((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 83: { bool _r = _t->updateMarker((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 84: { bool _r = _t->removeMarker((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 85: _t->setSnappingEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 86: { bool _r = _t->saveProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 87: { bool _r = _t->saveProject();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 88: { bool _r = _t->loadProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 89: { QVariantMap _r = _t->probeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 90: { bool _r = _t->startExport((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 91: { bool _r = _t->startExport((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 92: { bool _r = _t->startExportWithSettings((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 93: { bool _r = _t->downloadCaptionFont((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 94: { QString _r = _t->suggestedExportPath();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 95: _t->cancelExport(); break;
        case 96: { bool _r = _t->transcribeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 97: { bool _r = _t->transcribeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 98: { bool _r = _t->transcribeMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 99: _t->cancelTranscription(); break;
        case 100: { bool _r = _t->importSubtitles((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 101: { bool _r = _t->exportTranscriptSrt((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 102: { bool _r = _t->updateTranscriptSegment((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 103: { bool _r = _t->addTranscriptToTimeline();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 104: { bool _r = _t->removeTranscriptFromTimeline();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 105: { bool _r = _t->translateTranscript((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 106: _t->cancelTranslation(); break;
        case 107: _t->undo(); break;
        case 108: _t->redo(); break;
        case 109: _t->clearError(); break;
        case 110: { bool _r = _t->setColorSetting((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 111: { bool _r = _t->setClipColorSetting((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 112: { bool _r = _t->resetClipColorSettings((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 113: { bool _r = _t->setClipEffectSetting((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 114: { bool _r = _t->resetClipEffectSettings((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 115: { QString _r = _t->addClipEffect((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 116: { bool _r = _t->removeClipEffect((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 117: { bool _r = _t->moveClipEffect((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 118: { bool _r = _t->setClipEffectEnabled((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 119: { bool _r = _t->setClipEffectParameter((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 120: { bool _r = _t->resetClipEffectInstance((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 121: { bool _r = _t->beginCustomBlurMaskEdit((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 122: _t->endCustomBlurMaskEdit(); break;
        case 123: { bool _r = _t->setCustomBlurMask((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[6])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 124: _t->requestEffectControls((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 125: _t->requestEffectsBrowser((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 126: _t->requestEffectsBrowser(); break;
        case 127: { QString _r = _t->requestEffectPreview((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 128: { QString _r = _t->requestEffectPreview((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 129: { QVariantMap _r = _t->defaultAppSettings();
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 130: { bool _r = _t->applyAppSettings((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 131: { bool _r = _t->resetAppSettings();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 132: { bool _r = _t->clearMediaCache();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 133: { bool _r = _t->setMediaColorSetting((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 134: { bool _r = _t->startPreviewDecode((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[7])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[8])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[9])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 135: { bool _r = _t->requestPreviewFrame((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 136: _t->stopPreviewDecode(); break;
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
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::clipsChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::tracksChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::markersChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::snappingChanged, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::timelineChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::playheadChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::playingChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::dirtyChanged, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::historyChanged, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::activeWorkspaceChanged, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::layoutPresetChanged, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::captionStyleChanged, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::captionFontsChanged, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::exportStateChanged, 17))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::errorChanged, 18))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)(bool , const QString & )>(_a, &Backend::exportFinished, 19))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::transcriptChanged, 20))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::colorSettingsChanged, 21))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::selectionChanged, 22))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::effectControlsRequested, 23))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::customBlurEditChanged, 24))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::effectsBrowserRequested, 25))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)(const QString & , const QString & , bool , const QString & )>(_a, &Backend::effectPreviewReady, 26))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::appSettingsChanged, 27))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::mediaCacheChanged, 28))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::previewFrameChanged, 29))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::previewStateChanged, 30))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)()>(_a, &Backend::previewErrorChanged, 31))
            return;
        if (QtMocHelpers::indexOfMethod<void (Backend::*)(const QString & )>(_a, &Backend::autoSaveCompleted, 32))
            return;
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
        case 31: *reinterpret_cast<bool*>(_v) = _t->hasSubtitleClips(); break;
        case 32: *reinterpret_cast<int*>(_v) = _t->videoTrackCount(); break;
        case 33: *reinterpret_cast<int*>(_v) = _t->audioTrackCount(); break;
        case 34: *reinterpret_cast<QStringList*>(_v) = _t->mutedTracks(); break;
        case 35: *reinterpret_cast<QVariantList*>(_v) = _t->trackStates(); break;
        case 36: *reinterpret_cast<QVariantList*>(_v) = _t->markers(); break;
        case 37: *reinterpret_cast<bool*>(_v) = _t->snappingEnabled(); break;
        case 38: *reinterpret_cast<int*>(_v) = _t->mediaCount(); break;
        case 39: *reinterpret_cast<bool*>(_v) = _t->mediaImportInProgress(); break;
        case 40: *reinterpret_cast<int*>(_v) = _t->mediaImportProgress(); break;
        case 41: *reinterpret_cast<bool*>(_v) = _t->hasSequence(); break;
        case 42: *reinterpret_cast<bool*>(_v) = _t->hasMedia(); break;
        case 43: *reinterpret_cast<bool*>(_v) = _t->canExport(); break;
        case 44: *reinterpret_cast<qint64*>(_v) = _t->durationMs(); break;
        case 45: *reinterpret_cast<qint64*>(_v) = _t->playheadMs(); break;
        case 46: *reinterpret_cast<bool*>(_v) = _t->playing(); break;
        case 47: *reinterpret_cast<bool*>(_v) = _t->dirty(); break;
        case 48: *reinterpret_cast<bool*>(_v) = _t->canUndo(); break;
        case 49: *reinterpret_cast<bool*>(_v) = _t->canRedo(); break;
        case 50: *reinterpret_cast<bool*>(_v) = _t->exportInProgress(); break;
        case 51: *reinterpret_cast<double*>(_v) = _t->exportProgress(); break;
        case 52: *reinterpret_cast<QString*>(_v) = _t->exportStatus(); break;
        case 53: *reinterpret_cast<QVariantList*>(_v) = _t->transcript(); break;
        case 54: *reinterpret_cast<bool*>(_v) = _t->transcriptionInProgress(); break;
        case 55: *reinterpret_cast<QString*>(_v) = _t->transcriptionStatus(); break;
        case 56: *reinterpret_cast<QString*>(_v) = _t->transcriptLanguage(); break;
        case 57: *reinterpret_cast<bool*>(_v) = _t->translationInProgress(); break;
        case 58: *reinterpret_cast<QString*>(_v) = _t->translationStatus(); break;
        case 59: *reinterpret_cast<QString*>(_v) = _t->lastError(); break;
        case 60: *reinterpret_cast<QVariantMap*>(_v) = _t->colorSettings(); break;
        case 61: *reinterpret_cast<QVariantMap*>(_v) = _t->activeColorClip(); break;
        case 62: *reinterpret_cast<QVariantMap*>(_v) = _t->activeColorMedia(); break;
        case 63: *reinterpret_cast<QString*>(_v) = _t->selectedClipId(); break;
        case 64: *reinterpret_cast<QVariantMap*>(_v) = _t->selectedClip(); break;
        case 65: *reinterpret_cast<QVariantList*>(_v) = _t->effectDefinitions(); break;
        case 66: *reinterpret_cast<QString*>(_v) = _t->customBlurEditClipId(); break;
        case 67: *reinterpret_cast<QString*>(_v) = _t->customBlurEditInstanceId(); break;
        case 68: *reinterpret_cast<QVariantMap*>(_v) = _t->appSettings(); break;
        case 69: *reinterpret_cast<QString*>(_v) = _t->mediaCachePath(); break;
        case 70: *reinterpret_cast<QString*>(_v) = _t->mediaCacheSize(); break;
        case 71: *reinterpret_cast<QString*>(_v) = _t->previewFrameUrl(); break;
        case 72: *reinterpret_cast<bool*>(_v) = _t->previewDecoding(); break;
        case 73: *reinterpret_cast<QString*>(_v) = _t->previewError(); break;
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
        case 37: _t->setSnappingEnabled(*reinterpret_cast<bool*>(_v)); break;
        case 45: _t->setPlayheadMs(*reinterpret_cast<qint64*>(_v)); break;
        case 46: _t->setPlaying(*reinterpret_cast<bool*>(_v)); break;
        case 63: _t->setSelectedClipId(*reinterpret_cast<QString*>(_v)); break;
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
        if (_id < 137)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 137;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 137)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 137;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 74;
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
void Backend::clipsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void Backend::tracksChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void Backend::markersChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void Backend::snappingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void Backend::timelineChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void Backend::playheadChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void Backend::playingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void Backend::dirtyChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}

// SIGNAL 12
void Backend::historyChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}

// SIGNAL 13
void Backend::activeWorkspaceChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 13, nullptr);
}

// SIGNAL 14
void Backend::layoutPresetChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 14, nullptr);
}

// SIGNAL 15
void Backend::captionStyleChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 15, nullptr);
}

// SIGNAL 16
void Backend::captionFontsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 16, nullptr);
}

// SIGNAL 17
void Backend::exportStateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 17, nullptr);
}

// SIGNAL 18
void Backend::errorChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 18, nullptr);
}

// SIGNAL 19
void Backend::exportFinished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 19, nullptr, _t1, _t2);
}

// SIGNAL 20
void Backend::transcriptChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 20, nullptr);
}

// SIGNAL 21
void Backend::colorSettingsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 21, nullptr);
}

// SIGNAL 22
void Backend::selectionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 22, nullptr);
}

// SIGNAL 23
void Backend::effectControlsRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 23, nullptr);
}

// SIGNAL 24
void Backend::customBlurEditChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 24, nullptr);
}

// SIGNAL 25
void Backend::effectsBrowserRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 25, nullptr);
}

// SIGNAL 26
void Backend::effectPreviewReady(const QString & _t1, const QString & _t2, bool _t3, const QString & _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 26, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 27
void Backend::appSettingsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 27, nullptr);
}

// SIGNAL 28
void Backend::mediaCacheChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 28, nullptr);
}

// SIGNAL 29
void Backend::previewFrameChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 29, nullptr);
}

// SIGNAL 30
void Backend::previewStateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 30, nullptr);
}

// SIGNAL 31
void Backend::previewErrorChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 31, nullptr);
}

// SIGNAL 32
void Backend::autoSaveCompleted(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 32, nullptr, _t1);
}
QT_WARNING_POP
