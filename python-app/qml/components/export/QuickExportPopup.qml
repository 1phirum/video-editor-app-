// qmllint disable
// Quick Export: one preset, one path, one button.
//
// The full Export page exists for the render you care about - codec, profile,
// loudness, ducking, two-pass. This is for the other case: the sequence is
// finished and you want the file now. So it offers only the choices that change
// the answer, and shows what you are about to get before you commit to it.
//
// A popover anchored to its toolbar icon rather than a window, because it is a
// one-decision affordance and taking over the screen for it would be out of
// proportion. It stays open during the render and reports progress in place.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CutPro 1.0
import "../../theme"
import "../common"

Popup {
    id: quickExport

    width: 420
    padding: 0
    modal: false
    dim: false
    closePolicy: Backend.exportInProgress
                 ? Popup.NoAutoClose
                 : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    // Where the file goes. Seeded from the app's own suggestion the first time
    // it opens and whenever the user has not overridden it, so the path is
    // never blank.
    property string outputPath: ""
    property bool pathCustomised: false
    property int presetIndex: 0

    readonly property var source: {
        // sequenceSourceInfo() walks the clip list in C++, so the clip
        // notifier has to be read here or this never re-evaluates.
        void Backend.mediaClips.length
        return Backend.sequenceSourceInfo()
    }
    readonly property var preset: presets[presetIndex] || presets[0]

    // Short edge, not height. The named presets are written for landscape, but
    // a 720x1280 vertical sequence exported as "1080p" must become 1080x1920,
    // not 1920x1080 with the picture pillarboxed into the middle of it. Scaling
    // the short edge preserves the sequence's own shape, which is what the user
    // means by "1080p" whichever way up their footage is.
    property var presets: [
        { label: qsTr("Match Source - Adaptive High Bitrate"),   quality: "high",   shortEdge: 0 },
        { label: qsTr("Match Source - Adaptive Medium Bitrate"), quality: "medium", shortEdge: 0 },
        { label: qsTr("Match Source - Adaptive Low Bitrate"),    quality: "low",    shortEdge: 0 },
        { label: qsTr("High Quality 2160p 4K"),                  quality: "high",   shortEdge: 2160 },
        { label: qsTr("High Quality 1080p HD"),                  quality: "high",   shortEdge: 1080 },
        { label: qsTr("High Quality 720p HD"),                   quality: "high",   shortEdge: 720 },
        { label: qsTr("High Quality 480p SD Wide"),              quality: "high",   shortEdge: 480 }
    ]

    // h264 refuses odd dimensions, so every computed size lands on an even number.
    function even(value) {
        return Math.max(2, 2 * Math.round(value / 2))
    }

    readonly property int targetWidth: {
        var sw = Math.max(1, Number(source.width))
        var sh = Math.max(1, Number(source.height))
        if (preset.shortEdge <= 0)
            return even(sw)
        return sw <= sh ? even(preset.shortEdge)
                        : even(preset.shortEdge * sw / sh)
    }
    readonly property int targetHeight: {
        var sw = Math.max(1, Number(source.width))
        var sh = Math.max(1, Number(source.height))
        if (preset.shortEdge <= 0)
            return even(sh)
        return sw <= sh ? even(preset.shortEdge * sh / sw)
                        : even(preset.shortEdge)
    }

    // The same shape the Export page hands to startExportWithSettings, so the
    // render path is identical - this is not a second, simpler exporter.
    function exportSettings() {
        return {
            quality: preset.quality,
            width: targetWidth,
            height: targetHeight,
            frameRate: Number(source.frameRate),
            videoBitrateMbps: 0,
            videoFormat: "h264",
            profile: "high",
            encodingSpeed: "medium",
            maximumRenderQuality: false,
            audioEnabled: true,
            audioSampleRate: 48000,
            audioChannels: 2,
            audioBitrateKbps: 192,
            normalizeLoudness: false,
            loudnessTargetLufs: -14,
            duckMusicUnderNarration: false,
            duckAmountDb: -12
        }
    }

    readonly property var estimate: {
        // exportEstimate() reads the sequence duration in C++; touch it so a
        // trim or a new clip updates the quoted size.
        void Backend.durationMs
        return Backend.exportEstimate(exportSettings())
    }

    function ensurePath() {
        if (!pathCustomised || outputPath.length === 0)
            outputPath = withMp4(Backend.suggestedExportPath())
    }

    function withMp4(path) {
        var value = String(path || "")
        if (value.length === 0)
            return value
        return value.replace(/\.[^.\\/]+$/, "") + ".mp4"
    }

    function fileNameOf(path) {
        var normalized = String(path || "").replace(/\\/g, "/")
        var cut = normalized.lastIndexOf("/")
        return cut >= 0 ? normalized.slice(cut + 1) : normalized
    }

    function folderOf(path) {
        var normalized = String(path || "").replace(/\\/g, "/")
        var cut = normalized.lastIndexOf("/")
        return cut > 0 ? normalized.slice(0, cut) : ""
    }

    // Abbreviated the way Premiere's is - the middle of a long path is the least
    // informative part of it, so the drive and the file name survive.
    function shortPath(path) {
        var folder = folderOf(path)
        var name = fileNameOf(path)
        if (folder.length <= 22)
            return folder.length > 0 ? folder + "/" + name : name
        return folder.slice(0, 10) + "..." + "/" + name
    }

    function timecode(ms) {
        var fps = Math.max(1, Number(source.frameRate))
        var total = Math.max(0, Math.floor(Number(ms) / 1000))
        var frames = Math.floor((Math.max(0, Number(ms)) % 1000) / 1000 * fps)
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        function pad(v) { return v < 10 ? "0" + v : String(v) }
        return pad(h) + ":" + pad(m) + ":" + pad(s) + ":" + pad(frames)
    }

    onAboutToShow: ensurePath()

    background: Rectangle {
        color: Theme.bgPanel
        border.color: Theme.border
        border.width: 1
        radius: Theme.radiusMd
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Export to")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("MP4 video (*.mp4)")]
        defaultSuffix: "mp4"
        onAccepted: {
            quickExport.outputPath = quickExport.withMp4(
                        String(selectedFile).replace("file:///", ""))
            quickExport.pathCustomised = true
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        // ---- Title ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 6
            Label {
                text: qsTr("Quick Export:")
                color: Theme.textPrimary
                font.pixelSize: Theme.fsMd
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: Backend.sequenceName
                color: Theme.textPrimary
                font.pixelSize: Theme.fsMd
                elide: Text.ElideRight
            }
        }

        // ---- Destination ---------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 4

            Label {
                text: qsTr("File Name & Location")
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Image {
                    source: "../../assets/icons/folder.svg"
                    sourceSize.width: 14
                    sourceSize.height: 14
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    opacity: 0.8
                }
                Label {
                    id: pathLabel
                    Layout.fillWidth: true
                    text: quickExport.shortPath(quickExport.outputPath)
                    color: Theme.accent
                    font.pixelSize: Theme.fsSm
                    font.underline: pathHover.hovered
                    elide: Text.ElideMiddle
                    HoverHandler {
                        id: pathHover
                        cursorShape: Qt.PointingHandCursor
                        enabled: !Backend.exportInProgress
                    }
                    TapHandler {
                        enabled: !Backend.exportInProgress
                        onTapped: saveDialog.open()
                    }
                    ToolTip.visible: pathHover.hovered
                    ToolTip.text: quickExport.outputPath
                }
            }
        }

        // ---- Preset --------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 4

            Label {
                text: qsTr("Preset")
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }
            DarkComboBox {
                Layout.fillWidth: true
                enabled: !Backend.exportInProgress
                model: quickExport.presets.map(function (p) { return p.label })
                currentIndex: quickExport.presetIndex
                onActivated: (index) => quickExport.presetIndex = index
            }
        }

        // ---- What you are about to get -------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 2

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("H.264  |  %1 x %2  |  %3 fps  |  %4 Mbps  |  %5  |  %6")
                    .arg(quickExport.targetWidth)
                    .arg(quickExport.targetHeight)
                    .arg(Number(quickExport.source.frameRate).toFixed(
                             Number(quickExport.source.frameRate) % 1 === 0 ? 0 : 2))
                    .arg(Number(quickExport.estimate.videoMbps).toFixed(1))
                    .arg(quickExport.timecode(Backend.durationMs))
                    .arg(qsTr("Stereo"))
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: qsTr("Estimated File Size: %1 MB")
                    .arg(Math.max(1, Math.round(Number(quickExport.estimate.sizeMb))))
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
        }

        // ---- Progress, in place --------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 4
            visible: Backend.exportInProgress

            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: Theme.bgPrimary
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, Backend.exportProgress))
                    height: parent.height
                    radius: parent.radius
                    color: Theme.accent
                }
            }
            Label {
                text: qsTr("%1  %2%").arg(Backend.exportStatus)
                                     .arg(Math.round(Backend.exportProgress * 100))
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
            ExportProgressDetails {
                Layout.fillWidth: true
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            wrapMode: Text.WordWrap
            visible: !Backend.canExport
            text: qsTr("Add media to the sequence before exporting.")
            color: Theme.danger
            font.pixelSize: Theme.fsSm
        }

        // ---- Action --------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            spacing: 8
            Item { Layout.fillWidth: true }
            DarkDialogButton {
                text: qsTr("Cancel")
                flat: true
                visible: Backend.exportInProgress
                onClicked: Backend.cancelExport()
            }
            DarkDialogButton {
                text: Backend.exportInProgress ? qsTr("Exporting...") : qsTr("Export")
                highlighted: true
                enabled: Backend.canExport && !Backend.exportInProgress
                         && quickExport.outputPath.length > 0
                onClicked: Backend.startExportWithSettings(quickExport.outputPath,
                                                           quickExport.exportSettings())
            }
        }
    }

    Connections {
        target: Backend
        function onExportFinished(success, outputPath) {
            // Leave it open on failure so the error stays attached to the thing
            // that produced it; close on success, which is the whole point of a
            // one-button export.
            if (success)
                quickExport.close()
        }
    }
}
