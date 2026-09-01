pragma ComponentBehavior: Bound
// qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CutPro 1.0
import "../theme"
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"

Rectangle {
    id: root
    color: Theme.bgPanel

    property string outputPath: ""
    property bool outputPathCustomized: false
    property int presetIndex: 0
    property int frameSizeIndex: 0
    property int frameRateIndex: 0
    property int customWidth: 1920
    property int customHeight: 1080
    property real videoBitrateMbps: 16
    property int profileIndex: 2
    property int encodingSpeedIndex: 1
    property bool maximumRenderQuality: true
    property bool audioEnabled: true
    property int audioSampleRateIndex: 1
    property int audioChannelsIndex: 1
    property int audioBitrateIndex: 1

    readonly property var sourceMedia: firstVisualMedia()
    readonly property int sourceWidth: sourceMedia && sourceMedia.width > 0
                                               ? sourceMedia.width : 1920
    readonly property int sourceHeight: sourceMedia && sourceMedia.height > 0
                                                ? sourceMedia.height : 1080
    readonly property real sourceFrameRate:
        sourceMedia && sourceMedia.frameRate > 0 ? sourceMedia.frameRate : 30
    readonly property var selectedFrameSize: frameSizeOptions[frameSizeIndex]
    readonly property var selectedFrameRate: frameRateOptions[frameRateIndex]
    readonly property int effectiveWidth: selectedFrameSize.width > 0
                                                   ? selectedFrameSize.width
                                                   : selectedFrameSize.custom
                                                     ? customWidth : sourceWidth
    readonly property int effectiveHeight: selectedFrameSize.height > 0
                                                    ? selectedFrameSize.height
                                                    : selectedFrameSize.custom
                                                      ? customHeight : sourceHeight
    readonly property real effectiveFrameRate: selectedFrameRate.value > 0
                                                       ? selectedFrameRate.value
                                                       : sourceFrameRate
    readonly property int audioBitrateKbps: [128, 192, 256, 320][audioBitrateIndex]
    readonly property int audioSampleRate: [44100, 48000][audioSampleRateIndex]
    readonly property int audioChannels: [1, 2][audioChannelsIndex]
    readonly property real estimatedSizeMb:
        Math.max(0, Backend.durationMs / 1000)
        * (videoBitrateMbps + (audioEnabled ? audioBitrateKbps / 1000 : 0)) / 8

    property var frameSizeOptions: [
        { label: "Match Source", width: 0, height: 0, custom: false },
        { label: "3840 x 2160 (UHD)", width: 3840, height: 2160, custom: false },
        { label: "2560 x 1440 (QHD)", width: 2560, height: 1440, custom: false },
        { label: "1920 x 1080 (Full HD)", width: 1920, height: 1080, custom: false },
        { label: "1280 x 720 (HD)", width: 1280, height: 720, custom: false },
        { label: "854 x 480 (SD)", width: 854, height: 480, custom: false },
        { label: "Custom", width: 0, height: 0, custom: true }
    ]
    property var frameRateOptions: [
        { label: "Match Source", value: 0 },
        { label: "23.976 fps", value: 23.976 },
        { label: "24 fps", value: 24 },
        { label: "25 fps", value: 25 },
        { label: "29.97 fps", value: 29.97 },
        { label: "30 fps", value: 30 },
        { label: "50 fps", value: 50 },
        { label: "59.94 fps", value: 59.94 },
        { label: "60 fps", value: 60 }
    ]

    function firstVisualMedia() {
        var firstClip = null
        var clips = Backend.mediaClips
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            if (clip.kind === "audio")
                continue
            if (!firstClip || clip.startMs < firstClip.startMs)
                firstClip = clip
        }
        if (!firstClip)
            return null
        var media = Backend.mediaById(String(firstClip.mediaId || ""))
        return media && media.id ? media : null
    }

    function mediaUrl(path) {
        if (!path)
            return ""
        var normalized = String(path).replace(/\\/g, "/")
        return normalized.charAt(0) === "/"
                ? "file://" + encodeURI(normalized)
                : "file:///" + encodeURI(normalized)
    }

    function durationText(milliseconds) {
        var total = Math.max(0, Math.floor(milliseconds / 1000))
        var seconds = total % 60
        var minutes = Math.floor(total / 60) % 60
        var hours = Math.floor(total / 3600)
        function pad(value) { return value < 10 ? "0" + value : String(value) }
        return pad(hours) + ":" + pad(minutes) + ":" + pad(seconds)
    }

    function safeFileName(value) {
        return String(value || "sequence").replace(/[\\/:*?"<>|]/g, "_")
    }

    function applyPreset(index) {
        presetIndex = index
        if (index === 0) {
            frameSizeIndex = 0
            frameRateIndex = 0
            videoBitrateMbps = sourceWidth >= 3840 ? 45 : 16
            encodingSpeedIndex = 1
        } else if (index === 1) {
            frameSizeIndex = 3
            frameRateIndex = 0
            videoBitrateMbps = 12
            encodingSpeedIndex = 1
        } else if (index === 2) {
            frameSizeIndex = 1
            frameRateIndex = 0
            videoBitrateMbps = 45
            encodingSpeedIndex = 2
        } else {
            frameSizeIndex = 4
            frameRateIndex = 5
            videoBitrateMbps = 5
            encodingSpeedIndex = 0
        }
    }

    function exportSettings() {
        return {
            quality: presetIndex === 3 ? "low" : presetIndex === 1 ? "medium" : "high",
            width: effectiveWidth,
            height: effectiveHeight,
            frameRate: effectiveFrameRate,
            videoBitrateMbps: videoBitrateMbps,
            profile: ["baseline", "main", "high"][profileIndex],
            encodingSpeed: ["fast", "medium", "slow"][encodingSpeedIndex],
            maximumRenderQuality: maximumRenderQuality,
            audioEnabled: audioEnabled,
            audioSampleRate: audioSampleRate,
            audioChannels: audioChannels,
            audioBitrateKbps: audioBitrateKbps
        }
    }

    function useSuggestedOutputPath() {
        outputPath = Backend.suggestedExportPath()
        outputPathCustomized = false
    }

    function openOutputDialog() {
        if (outputPath !== "") {
            var normalized = String(outputPath).replace(/\\/g, "/")
            var separator = normalized.lastIndexOf("/")
            if (separator > 0)
                outputFolderDialog.currentFolder = mediaUrl(normalized.slice(0, separator))
        }
        outputFolderDialog.open()
    }

    function outputFileName() {
        var normalized = String(outputPath || "").replace(/\\/g, "/")
        var separator = normalized.lastIndexOf("/")
        var name = separator >= 0 ? normalized.slice(separator + 1) : normalized
        if (!name || name === ".mp4")
            name = safeFileName(Backend.sequenceName) + ".mp4"
        if (!/\.mp4$/i.test(name))
            name += ".mp4"
        return name
    }

    function localPathFromUrl(value) {
        var url = String(value || "")
        if (!url)
            return ""

        var path = url
        if (/^file:\/\//i.test(url)) {
            path = url.slice(7)
            if (/^\/[A-Za-z]:\//.test(path))
                path = path.slice(1)
            else if (!/^\//.test(path))
                path = "//" + path
        }

        try {
            return decodeURIComponent(path)
        } catch (error) {
            return path
        }
    }

    Component.onCompleted: {
        applyPreset(0)
        useSuggestedOutputPath()
    }

    Connections {
        target: Backend
        function onProjectChanged() {
            if (!root.outputPathCustomized)
                root.useSuggestedOutputPath()
        }
        function onSequenceChanged() {
            if (!root.outputPathCustomized)
                root.useSuggestedOutputPath()
        }
    }

    FolderDialog {
        id: outputFolderDialog
        title: "Choose export folder"
        onAccepted: {
            var folder = root.localPathFromUrl(selectedFolder)
            if (!folder)
                return
            root.outputPath = folder.replace(/\\/g, "/") + "/" + root.outputFileName()
            root.outputPathCustomized = true
        }
    }

    component DarkTextField: TextField {
        implicitHeight: 30
        leftPadding: 9
        rightPadding: 9
        color: Theme.textPrimary
        placeholderTextColor: Theme.textMuted
        selectionColor: Theme.accent
        selectedTextColor: "white"
        font.pixelSize: Theme.fsSm
        background: Rectangle {
            color: parent.activeFocus ? Theme.hover : Theme.bgPrimary
            border.width: 1
            border.color: parent.activeFocus ? Theme.accent : Theme.border
            radius: Theme.radiusSm
        }
    }

    component SecondaryButton: Button {
        implicitHeight: 32
        leftPadding: 14
        rightPadding: 14
        hoverEnabled: true
        HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Theme.fsSm
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.down ? Qt.darker(Theme.hover, 1.2)
                               : parent.hovered ? Theme.hover : Theme.bgSidebar
            border.width: 1
            border.color: Theme.border
            radius: Theme.radiusSm
        }
    }

    component PrimaryButton: Button {
        implicitHeight: 34
        leftPadding: 20
        rightPadding: 20
        hoverEnabled: true
        HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? "white" : Theme.textMuted
            font.pixelSize: Theme.fsSm
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: !parent.enabled ? Theme.hover
                                  : parent.down ? Qt.darker(Theme.accent, 1.25)
                                                : Theme.accent
            radius: Theme.radiusSm
        }
    }

    component ExportSlider: Slider {
        implicitHeight: 28
        hoverEnabled: true
        HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        background: Rectangle {
            x: parent.leftPadding
            y: Math.round(parent.topPadding + parent.availableHeight / 2 - height / 2)
            implicitWidth: 180
            implicitHeight: 4
            width: parent.availableWidth
            height: implicitHeight
            radius: 2
            color: Theme.bgPrimary
            Rectangle {
                width: parent.parent.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.accent
            }
        }
        handle: Rectangle {
            x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
            y: parent.topPadding + parent.availableHeight / 2 - height / 2
            implicitWidth: 12
            implicitHeight: 12
            radius: 6
            color: parent.pressed ? Theme.accent : Theme.textPrimary
            border.width: 1
            border.color: Theme.bgPrimary
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: Theme.bgSidebar

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "Export"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsXl
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: Backend.canExport
                              ? Backend.sequenceName + "  |  " + root.durationText(Backend.durationMs)
                              : "Add media to the timeline before exporting"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsXs
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "H.264  |  " + root.effectiveWidth + " x "
                          + root.effectiveHeight + "  |  "
                          + root.effectiveFrameRate.toFixed(root.effectiveFrameRate % 1 ? 3 : 0)
                          + " fps"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 16

            Rectangle {
                Layout.preferredWidth: Math.max(360, Math.min(460, root.width * 0.36))
                Layout.fillHeight: true
                color: Theme.bgSidebar
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        color: Theme.bgPanel

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            spacing: 14

                            Text {
                                text: "Output Preview"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsSm
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "Entire Sequence"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fsXs
                                Layout.rightMargin: 12
                            }
                        }
                    }

                    ExportPreviewPlayer {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(360, width * 9 / 16 + 68)
                        frameRate: root.effectiveFrameRate
                        audioEnabled: root.audioEnabled
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 14
                        spacing: 10

                        Text {
                            text: root.sourceMedia ? root.sourceMedia.name : Backend.sequenceName
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsMd
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 18
                            rowSpacing: 7

                            Text { text: "Source"; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            Text {
                                text: root.sourceWidth + " x " + root.sourceHeight + "  |  "
                                      + root.sourceFrameRate.toFixed(root.sourceFrameRate % 1 ? 3 : 0) + " fps"
                                color: Theme.textSecondary; font.pixelSize: Theme.fsXs
                            }
                            Text { text: "Output"; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            Text {
                                text: root.effectiveWidth + " x " + root.effectiveHeight + "  |  "
                                      + root.effectiveFrameRate.toFixed(root.effectiveFrameRate % 1 ? 3 : 0) + " fps"
                                color: Theme.textSecondary; font.pixelSize: Theme.fsXs
                            }
                            Text { text: "Codec"; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            Text { text: "H.264 / AAC"; color: Theme.textSecondary; font.pixelSize: Theme.fsXs }
                            Text { text: "Estimated"; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            Text {
                                text: root.estimatedSizeMb.toFixed(1) + " MB"
                                color: Theme.textSecondary; font.pixelSize: Theme.fsXs
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: Theme.bgPanel
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            Text { text: "Output"; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            Text {
                                Layout.fillWidth: true
                                text: root.outputPath === "" ? "No destination selected" : root.outputPath
                                color: root.outputPath === "" ? Theme.textMuted : Theme.textSecondary
                                font.pixelSize: Theme.fsXs
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideMiddle
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bgPanel
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        color: Theme.bgSidebar
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            Text {
                                text: "Export Settings"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsSm
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "FFmpeg H.264"
                                color: Theme.textMuted
                                font.pixelSize: Theme.fsXs
                            }
                        }
                    }

                    ScrollView {
                        id: settingsScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded

                        ColumnLayout {
                            width: settingsScroll.availableWidth
                            spacing: 8

                            ExportSection {
                                Layout.fillWidth: true
                                title: "File"
                                summary: ["Match Source - High Bitrate", "YouTube 1080p", "High Quality 4K", "Fast 720p"][root.presetIndex]

                                ExportSettingRow {
                                    label: "File name"
                                    DarkTextField {
                                        Layout.fillWidth: true
                                        text: root.outputPath
                                        placeholderText: "Default export location"
                                        onTextEdited: {
                                            root.outputPath = text
                                            root.outputPathCustomized = true
                                        }
                                    }
                                    IconButton {
                                        iconName: "home"
                                        boxSize: 30
                                        glyphSize: 14
                                        onClicked: root.useSuggestedOutputPath()
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Use default export folder"
                                    }
                                    SecondaryButton {
                                        text: "Browse"
                                        onClicked: root.openOutputDialog()
                                    }
                                }

                                ExportSettingRow {
                                    label: "Format"
                                    ExportComboBox { Layout.fillWidth: true; model: ["H.264"]; enabled: false }
                                }

                                ExportSettingRow {
                                    label: "Preset"
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: ["Match Source - High Bitrate", "YouTube 1080p", "High Quality 4K", "Fast 720p"]
                                        currentIndex: root.presetIndex
                                        onActivated: index => root.applyPreset(index)
                                    }
                                }

                            }

                            ExportSection {
                                Layout.fillWidth: true
                                title: "Video"
                                summary: root.effectiveWidth + " x " + root.effectiveHeight + ", "
                                         + root.effectiveFrameRate.toFixed(root.effectiveFrameRate % 1 ? 3 : 0) + " fps"

                                ExportSettingRow {
                                    label: "Frame size"
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: root.frameSizeOptions
                                        textRole: "label"
                                        currentIndex: root.frameSizeIndex
                                        onActivated: index => root.frameSizeIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    visible: root.selectedFrameSize.custom
                                    label: "Dimensions"
                                    CaptionSpinBox {
                                        Layout.fillWidth: true
                                        from: 320; to: 7680; stepSize: 2
                                        value: root.customWidth
                                        onValueModified: root.customWidth = value + value % 2
                                    }
                                    Text { text: "x"; color: Theme.textMuted; font.pixelSize: Theme.fsSm }
                                    CaptionSpinBox {
                                        Layout.fillWidth: true
                                        from: 240; to: 4320; stepSize: 2
                                        value: root.customHeight
                                        onValueModified: root.customHeight = value + value % 2
                                    }
                                }

                                ExportSettingRow {
                                    label: "Frame rate"
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: root.frameRateOptions
                                        textRole: "label"
                                        currentIndex: root.frameRateIndex
                                        onActivated: index => root.frameRateIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    label: "Profile"
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: ["Baseline", "Main", "High"]
                                        currentIndex: root.profileIndex
                                        onActivated: index => root.profileIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    label: "Encoding speed"
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: ["Fast", "Balanced", "Higher Quality"]
                                        currentIndex: root.encodingSpeedIndex
                                        onActivated: index => root.encodingSpeedIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    label: "Target bitrate"
                                    description: "Mbps"
                                    ExportSlider {
                                        Layout.fillWidth: true
                                        from: 2; to: 100; stepSize: 1
                                        value: root.videoBitrateMbps
                                        onMoved: root.videoBitrateMbps = value
                                    }
                                    Text {
                                        Layout.preferredWidth: 50
                                        text: Math.round(root.videoBitrateMbps) + " Mbps"
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fsXs
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                ExportSettingRow {
                                    label: "Render quality"
                                    CaptionSwitch {
                                        text: "Use Maximum Render Quality"
                                        checked: root.maximumRenderQuality
                                        onToggled: root.maximumRenderQuality = checked
                                    }
                                }
                            }

                            ExportSection {
                                Layout.fillWidth: true
                                title: "Audio"
                                summary: root.audioEnabled
                                         ? root.audioSampleRate + " Hz, " + root.audioBitrateKbps + " kbps"
                                         : "Disabled"

                                ExportSettingRow {
                                    label: "Export audio"
                                    CaptionSwitch {
                                        text: checked ? "Enabled" : "Disabled"
                                        checked: root.audioEnabled
                                        onToggled: root.audioEnabled = checked
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled
                                    label: "Sample rate"
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: ["44100 Hz", "48000 Hz"]
                                        currentIndex: root.audioSampleRateIndex
                                        onActivated: index => root.audioSampleRateIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled
                                    label: "Channels"
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: ["Mono", "Stereo"]
                                        currentIndex: root.audioChannelsIndex
                                        onActivated: index => root.audioChannelsIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled
                                    label: "Audio bitrate"
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: ["128 kbps", "192 kbps", "256 kbps", "320 kbps"]
                                        currentIndex: root.audioBitrateIndex
                                        onActivated: index => root.audioBitrateIndex = index
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true; Layout.preferredHeight: 4 }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.bgSidebar

            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: Backend.lastError !== "" ? Backend.lastError
                                                          : Backend.exportStatus || "Ready to export"
                            color: Backend.lastError !== "" ? Theme.danger : Theme.textSecondary
                            font.pixelSize: Theme.fsSm
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: Backend.exportInProgress
                            text: Math.round(Backend.exportProgress * 100) + "%"
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsXs
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 3
                        color: Theme.bgPrimary
                        Rectangle {
                            width: parent.width * Backend.exportProgress
                            height: parent.height
                            color: Theme.accent
                        }
                    }
                }

                SecondaryButton {
                    text: Backend.exportInProgress ? "Cancel Export" : "Cancel"
                    enabled: Backend.exportInProgress
                    onClicked: Backend.cancelExport()
                }

                PrimaryButton {
                    text: Backend.exportInProgress ? "Exporting..." : "Export"
                    enabled: Backend.canExport && !Backend.exportInProgress
                             && root.outputPath !== ""
                    onClicked: Backend.startExportWithSettings(root.outputPath,
                                                                root.exportSettings())
                }
            }
        }
    }
}
