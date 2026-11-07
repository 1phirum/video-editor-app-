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
    property int videoEncoderIndex: 0
    property bool maximumRenderQuality: true
    property bool audioEnabled: true
    property int audioSampleRateIndex: 1
    property int audioChannelsIndex: 1
    property int audioBitrateIndex: 1
    property bool normalizeLoudness: false
    property int loudnessTargetIndex: 0
    property bool duckMusicUnderNarration: false
    property int duckAmountIndex: 1
    property int videoFormatIndex: 0

    // How far the music/original bed is pushed under the narration voice while it
    // speaks. Given as a positive dB reduction the export builder maps onto the
    // sidechain compressor; -12 dB is the usual "voiceover over music" duck.
    readonly property var duckAmounts: [
        { label: qsTr("Light (-6 dB)"), db: 6 },
        { label: qsTr("Medium (-12 dB)"), db: 12 },
        { label: qsTr("Strong (-18 dB)"), db: 18 }
    ]
    readonly property var selectedDuckAmount:
        duckAmounts[duckAmountIndex] || duckAmounts[1]

    // Delivery loudness targets for the optional master normalization pass. The
    // integrated-loudness figure each platform normalizes uploads to, so an
    // export lands at the right level instead of drifting with how loud the
    // narration and music happened to sit. -14 LUFS is the social-video norm.
    readonly property var loudnessTargets: [
        { label: qsTr("-14 LUFS (YouTube, social)"), lufs: -14 },
        { label: qsTr("-16 LUFS (podcast, Apple)"), lufs: -16 },
        { label: qsTr("-23 LUFS (broadcast, EBU R128)"), lufs: -23 }
    ]
    readonly property var selectedLoudnessTarget:
        loudnessTargets[loudnessTargetIndex] || loudnessTargets[0]

    // Output codec choices, same line-up a client sees in CapCut. Each carries
    // the FFmpeg code the export builder branches on and the container the codec
    // has to live in - RLE only muxes into .mov, the rest ride in .mp4. The
    // Profile row (Baseline/Main/High) is an H.264 concept, so it only shows for
    // H.264; the others pick their profile from the pixel format on their own.
    property var videoFormatOptions: [
        { label: "H.264", code: "h264", ext: "mp4", desc: qsTr("The most common, plays everywhere") },
        { label: "HEVC", code: "hevc", ext: "mp4", desc: qsTr("Efficient compression, saves space") },
        { label: "HEVC (422)", code: "hevc422", ext: "mp4", desc: qsTr("10-bit 4:2:2 for colour grading") },
        { label: "AV1", code: "av1", ext: "mp4", desc: qsTr("Smallest files, newer devices") },
        { label: "RLE", code: "rle", ext: "mov", desc: qsTr("Lossless, keeps alpha. Very large file") }
    ]
    readonly property var selectedVideoFormat:
        videoFormatOptions[videoFormatIndex] || videoFormatOptions[0]

    // Keep the file extension matched to the codec's container. Rewrites the
    // extension on the current path (whether the client typed it or not) so an
    // RLE export never lands as a .mp4 FFmpeg would refuse to mux.
    function withExtension(path, ext) {
        var value = String(path || "")
        if (value.length === 0)
            return value
        return value.replace(/\.[^.\\/]+$/, "") + "." + ext
    }

    onVideoFormatIndexChanged: {
        outputPath = withExtension(outputPath, selectedVideoFormat.ext)
    }

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
    // The encoder runs at constant quality and treats the max bitrate as a
    // ceiling, so multiplying that ceiling by the duration describes a file it
    // will almost never write. What the size actually follows is how much
    // picture there is to describe: the footage is fitted inside the output
    // frame, so letterbox bars cost close to nothing, and upscaling cannot
    // invent detail the source never had - which is why the same clip at 8K is
    // nowhere near four times its 4K size. Estimate from the fitted picture,
    // allow enlargement to cost up to four times the source's own pixels, and
    // stop at the ceiling the encoder would clamp to anyway. The bits-per-pixel
    // figures are measured off this encoder at each CRF, not guessed.
    readonly property real qualityBitsPerPixel:
        selectedPreset.quality === "low" ? 0.08
                                         : selectedPreset.quality === "medium" ? 0.13 : 0.20
    readonly property real fittedScale:
        Math.min(effectiveWidth / Math.max(1, sourceWidth),
                 effectiveHeight / Math.max(1, sourceHeight))
    readonly property real codedPixels:
        Math.min(sourceWidth * sourceHeight * Math.min(fittedScale * fittedScale, 4),
                 effectiveWidth * effectiveHeight)
    readonly property real estimatedVideoMbps:
        Math.min(videoBitrateMbps > 0 ? videoBitrateMbps : Number.MAX_VALUE,
                 Math.max(0.3, codedPixels * effectiveFrameRate * qualityBitsPerPixel / 1000000))
    readonly property real estimatedSizeMb:
        Math.max(0, Backend.durationMs / 1000)
        * (estimatedVideoMbps + (audioEnabled ? audioBitrateKbps / 1000 : 0)) / 8

    property var frameSizeOptions: [
        { label: qsTr("Match Source"), width: 0, height: 0, custom: false },
        { label: "7680 x 4320 (8K UHD)", width: 7680, height: 4320, custom: false },
        { label: "5120 x 2880 (5K)", width: 5120, height: 2880, custom: false },
        { label: "3840 x 2160 (4K UHD)", width: 3840, height: 2160, custom: false },
        { label: "2560 x 1440 (QHD)", width: 2560, height: 1440, custom: false },
        { label: "1920 x 1080 (Full HD)", width: 1920, height: 1080, custom: false },
        { label: "1280 x 720 (HD)", width: 1280, height: 720, custom: false },
        { label: "854 x 480 (SD)", width: 854, height: 480, custom: false },
        { label: "640 x 360 (360p)", width: 640, height: 360, custom: false },
        // Vertical / social sizes so the one-click TikTok/Reels/Shorts presets
        // can land on a real frame-size row instead of dropping to Custom.
        { label: "1080 x 1920 (Vertical HD)", width: 1080, height: 1920, custom: false },
        { label: "720 x 1280 (Vertical)", width: 720, height: 1280, custom: false },
        { label: "1080 x 1080 (Square)", width: 1080, height: 1080, custom: false },
        { label: qsTr("Custom"), width: 0, height: 0, custom: true }
    ]
    // One row per preset, so the menu, the summary line, applyPreset and the
    // quality flag all read the same list instead of four parallel ones that
    // have to agree on an index. Bitrates are whole Mbps so the max bitrate
    // slider can land exactly on what a preset asked for, and they are ceilings
    // rather than quotas - the encoder runs at constant quality and only climbs
    // this high when the picture genuinely needs it.
    property var exportPresets: [
        { label: "Match Source - High Bitrate", width: 0, height: 0, bitrate: 0, speed: 1, quality: "high" },
        { label: "8K 4320p", width: 7680, height: 4320, bitrate: 80, speed: 2, quality: "high" },
        { label: "5K 2880p", width: 5120, height: 2880, bitrate: 55, speed: 2, quality: "high" },
        { label: "4K 2160p", width: 3840, height: 2160, bitrate: 45, speed: 2, quality: "high" },
        { label: "1440p QHD", width: 2560, height: 1440, bitrate: 24, speed: 1, quality: "high" },
        { label: "1080p Full HD", width: 1920, height: 1080, bitrate: 12, speed: 1, quality: "medium" },
        { label: "720p HD", width: 1280, height: 720, bitrate: 6, speed: 1, quality: "medium" },
        { label: "480p SD", width: 854, height: 480, bitrate: 3, speed: 0, quality: "low" },
        { label: "360p", width: 640, height: 360, bitrate: 1, speed: 0, quality: "low" },
        // Social one-click targets. Portrait 9:16 for TikTok / Reels / Shorts,
        // 16:9 for YouTube. Bitrates match each platform's sweet spot: pushing
        // higher just gets re-compressed on upload, so these are the ceilings
        // that survive the platform's own encoder without wasting file size.
        { label: "TikTok / Reels (Vertical 1080p)", width: 1080, height: 1920, bitrate: 12, speed: 1, quality: "high" },
        { label: "YouTube (1080p)", width: 1920, height: 1080, bitrate: 12, speed: 1, quality: "high" },
        { label: "YouTube Shorts (Vertical 1080p)", width: 1080, height: 1920, bitrate: 12, speed: 1, quality: "high" }
    ]
    readonly property var selectedPreset: exportPresets[presetIndex] || exportPresets[0]

    // One-click social targets shown as buttons above the Preset row. Each maps
    // onto a full row of exportPresets (matched by label) so the summary line,
    // bitrate slider and quality flag all stay in sync with the dropdown.
    property var socialPresets: [
        { label: "TikTok", icon: "video", preset: "TikTok / Reels (Vertical 1080p)" },
        { label: "Reels", icon: "instagram", preset: "TikTok / Reels (Vertical 1080p)" },
        { label: "YouTube", icon: "youtube", preset: "YouTube (1080p)" },
        { label: "Shorts", icon: "video", preset: "YouTube Shorts (Vertical 1080p)" }
    ]

    function presetIndexForLabel(label) {
        for (var i = 0; i < exportPresets.length; ++i)
            if (exportPresets[i].label === label)
                return i
        return -1
    }

    function applySocialPreset(label) {
        var index = presetIndexForLabel(label)
        if (index >= 0)
            applyPreset(index)
    }
    property var frameRateOptions: [
        { label: qsTr("Match Source"), value: 0 },
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

    function frameSizeIndexFor(width, height) {
        for (var i = 0; i < frameSizeOptions.length; ++i) {
            var option = frameSizeOptions[i]
            if (option.width === width && option.height === height)
                return i
        }
        return 0
    }

    function applyPreset(index) {
        var preset = exportPresets[index]
        if (!preset)
            return
        presetIndex = index
        // Every preset is a resolution choice, so the footage keeps its own
        // frame rate. Changing that belongs to the Frame rate row.
        frameRateIndex = 0
        encodingSpeedIndex = preset.speed
        if (preset.width > 0) {
            frameSizeIndex = frameSizeIndexFor(preset.width, preset.height)
            videoBitrateMbps = preset.bitrate
        } else {
            // Match Source leaves the size alone, so the only thing left to
            // decide is how much bitrate the footage's own resolution earns.
            frameSizeIndex = 0
            videoBitrateMbps = sourceWidth >= 7680 ? 80
                             : sourceWidth >= 5120 ? 55
                             : sourceWidth >= 3840 ? 45
                             : sourceWidth >= 2560 ? 24 : 16
        }
    }

    function exportSettings() {
        return {
            quality: selectedPreset.quality,
            width: effectiveWidth,
            height: effectiveHeight,
            frameRate: effectiveFrameRate,
            videoBitrateMbps: videoBitrateMbps,
            videoFormat: selectedVideoFormat.code,
            profile: ["baseline", "main", "high"][profileIndex],
            encodingSpeed: ["fast", "medium", "slow"][encodingSpeedIndex],
            videoEncoder: ["auto", "qsv", "software"][videoEncoderIndex],
            maximumRenderQuality: maximumRenderQuality,
            audioEnabled: audioEnabled,
            audioSampleRate: audioSampleRate,
            audioChannels: audioChannels,
            audioBitrateKbps: audioBitrateKbps,
            normalizeLoudness: normalizeLoudness,
            loudnessTargetLufs: selectedLoudnessTarget.lufs,
            duckMusicUnderNarration: duckMusicUnderNarration,
            duckAmountDb: selectedDuckAmount.db
        }
    }

    function useSuggestedOutputPath() {
        outputPath = withExtension(Backend.suggestedExportPath(),
                                   selectedVideoFormat.ext)
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
        var ext = selectedVideoFormat.ext
        var normalized = String(outputPath || "").replace(/\\/g, "/")
        var separator = normalized.lastIndexOf("/")
        var name = separator >= 0 ? normalized.slice(separator + 1) : normalized
        if (!name || name === "." + ext)
            name = safeFileName(Backend.sequenceName) + "." + ext
        // Force the codec's own container: an RLE stream can't live in a .mp4.
        name = name.replace(/\.[^.]+$/, "")
        return name + "." + ext
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
        title: qsTr("Choose export folder")
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
            border.color: parent.activeFocus ? Theme.accent : Theme.outline
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
            border.color: Theme.outline
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
                        text: qsTr("Export")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsXl
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: Backend.canExport
                              ? Backend.sequenceName + "  |  " + root.durationText(Backend.durationMs)
                              : qsTr("Add media to the timeline before exporting")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsXs
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: root.selectedVideoFormat.label + "  |  " + root.effectiveWidth + " x "
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
                color: Theme.outline
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
                color: Theme.bgCard
                radius: Theme.radiusMd
                border.width: 1
                border.color: Theme.outline

                ColumnLayout {
                    anchors.fill: parent
                    // Inside the outline, not over it. A child anchored to the
                    // full rect paints across the border, which left the frame
                    // showing only alongside the body and vanishing behind the
                    // title and output strips.
                    anchors.margins: 1
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        color: Theme.bgCardHeader
                        // One step tighter than the card's, because this strip
                        // sits inside the 1px outline.
                        radius: Theme.radiusMd - 1

                        // Squares off the bottom corners: only the two corners
                        // this strip shares with the card should be round.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: parent.radius
                            color: parent.color
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.divider
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            spacing: 14

                            Text {
                                text: qsTr("Output Preview")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsSm
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("Entire Sequence")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fsXs
                                Layout.rightMargin: 12
                            }
                        }
                    }

                    ExportPreviewPlayer {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(360, width * 9 / 16 + 68)
                        frameRate: Backend.videoPreviewHelper.activeMedia
                                   ? Number(Backend.videoPreviewHelper.activeMedia.frameRate || root.sourceFrameRate)
                                   : root.sourceFrameRate
                        outputWidth: root.effectiveWidth
                        outputHeight: root.effectiveHeight
                        audioEnabled: root.audioEnabled
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.divider
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 14
                        spacing: 10

                        Text {
                            text: root.sourceMedia ? qsTr("First source: %1").arg(root.sourceMedia.name) : Backend.sequenceName
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

                            Text { text: qsTr("Source"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            Text {
                                text: root.sourceWidth + " x " + root.sourceHeight + "  |  "
                                      + root.sourceFrameRate.toFixed(root.sourceFrameRate % 1 ? 3 : 0) + " fps"
                                color: Theme.textSecondary; font.pixelSize: Theme.fsXs
                            }
                            Text { text: qsTr("Output"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            Text {
                                text: root.effectiveWidth + " x " + root.effectiveHeight + "  |  "
                                      + root.effectiveFrameRate.toFixed(root.effectiveFrameRate % 1 ? 3 : 0) + " fps"
                                color: Theme.textSecondary; font.pixelSize: Theme.fsXs
                            }
                            Text { text: qsTr("Codec"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            Text { text: root.selectedVideoFormat.label + " / AAC"; color: Theme.textSecondary; font.pixelSize: Theme.fsXs }
                            Text { text: qsTr("Estimated"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
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
                        color: Theme.bgCardHeader
                        radius: Theme.radiusMd - 1

                        // Squares off the top corners; the round pair is the one
                        // shared with the bottom of the card.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: parent.radius
                            color: parent.color
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 1
                            color: Theme.divider
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            Text { text: qsTr("Output"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            Text {
                                Layout.fillWidth: true
                                text: root.outputPath === "" ? qsTr("No destination selected") : root.outputPath
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
                // A trough, one step darker than the page, because it holds the
                // three section cards rather than content of its own. Every
                // boundary on this screen is then a change in tone and the
                // hairlines only have to trace them.
                color: Theme.bgPrimary
                radius: Theme.radiusMd
                border.width: 1
                border.color: Theme.outline

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        color: Theme.bgCardHeader
                        radius: Theme.radiusMd - 1

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: parent.radius
                            color: parent.color
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.divider
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            Text {
                                text: qsTr("Export Settings")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsSm
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("FFmpeg ") + root.selectedVideoFormat.label
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
                            spacing: 10

                            // Keeps the top card clear of the header hairline.
                            Item { Layout.fillWidth: true; Layout.preferredHeight: 2 }

                            // One-click social targets. Non-technical clients pick
                            // where the recap is going and every resolution / bitrate
                            // decision is made for them.
                            ExportSection {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                title: qsTr("Export for")
                                summary: qsTr("One click")

                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Repeater {
                                        model: root.socialPresets
                                        delegate: Rectangle {
                                            id: socialChip
                                            required property var modelData
                                            readonly property bool current:
                                                root.selectedPreset.label === modelData.preset
                                            width: chipRow.implicitWidth + 24
                                            height: 34
                                            radius: 8
                                            color: current ? Theme.accent
                                                   : chipHover.hovered ? Theme.hover : Theme.bgCard
                                            border.width: 1
                                            border.color: current ? Theme.accent : Theme.border

                                            RowLayout {
                                                id: chipRow
                                                anchors.centerIn: parent
                                                spacing: 6
                                                Rectangle {
                                                    // Tiny aspect-ratio badge so vertical vs
                                                    // landscape reads at a glance.
                                                    property bool vertical: socialChip.modelData.preset.indexOf("Vertical") >= 0
                                                    width: vertical ? 9 : 16
                                                    height: vertical ? 16 : 9
                                                    radius: 2
                                                    color: "transparent"
                                                    border.width: 1.5
                                                    border.color: socialChip.current ? "#ffffff" : Theme.textMuted
                                                    Layout.alignment: Qt.AlignVCenter
                                                }
                                                Text {
                                                    text: socialChip.modelData.label
                                                    color: socialChip.current ? "#ffffff" : Theme.textPrimary
                                                    font.pixelSize: Theme.fsSm
                                                    font.weight: Font.DemiBold
                                                }
                                            }

                                            HoverHandler { id: chipHover }
                                            TapHandler {
                                                onTapped: root.applySocialPreset(socialChip.modelData.preset)
                                            }
                                            ToolTip.visible: chipHover.hovered
                                            ToolTip.text: socialChip.modelData.preset
                                        }
                                    }
                                }
                            }

                            ExportSection {
                                Layout.fillWidth: true
                                // Inset on both sides so a card edge never lands
                                // on the column's own outline, which is what made
                                // the left border read as a double line.
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                title: qsTr("File")
                                summary: root.selectedPreset.label

                                ExportSettingRow {
                                    label: qsTr("File name")
                                    DarkTextField {
                                        Layout.fillWidth: true
                                        text: root.outputPath
                                        placeholderText: qsTr("Default export location")
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
                                        ToolTip.text: qsTr("Use default export folder")
                                    }
                                    SecondaryButton {
                                        text: qsTr("Browse")
                                        onClicked: root.openOutputDialog()
                                    }
                                }

                                ExportSettingRow {
                                    label: qsTr("Format")
                                    description: root.selectedVideoFormat.desc
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: root.videoFormatOptions
                                        textRole: "label"
                                        currentIndex: root.videoFormatIndex
                                        onActivated: index => root.videoFormatIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    label: qsTr("Preset")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: root.exportPresets
                                        textRole: "label"
                                        currentIndex: root.presetIndex
                                        onActivated: index => root.applyPreset(index)
                                    }
                                }

                            }

                            ExportSection {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                title: qsTr("Video")
                                summary: root.effectiveWidth + " x " + root.effectiveHeight + ", "
                                         + root.effectiveFrameRate.toFixed(root.effectiveFrameRate % 1 ? 3 : 0) + " fps"

                                ExportSettingRow {
                                    label: qsTr("Frame size")
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
                                    label: qsTr("Dimensions")
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
                                    label: qsTr("Frame rate")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: root.frameRateOptions
                                        textRole: "label"
                                        currentIndex: root.frameRateIndex
                                        onActivated: index => root.frameRateIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    // Baseline/Main/High are H.264 profiles; the
                                    // other codecs derive their profile from the
                                    // pixel format, so this row only applies here.
                                    visible: root.selectedVideoFormat.code === "h264"
                                    label: qsTr("Profile")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: [qsTr("Baseline"), qsTr("Main"), qsTr("High")]
                                        currentIndex: root.profileIndex
                                        onActivated: index => root.profileIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    label: qsTr("Encoding speed")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: [qsTr("Fast"), qsTr("Balanced"), qsTr("Higher Quality")]
                                        currentIndex: root.encodingSpeedIndex
                                        onActivated: index => root.encodingSpeedIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    label: qsTr("Encoder")
                                    description: qsTr("Auto checks hardware before exporting")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: root.selectedVideoFormat.code === "h264" || root.selectedVideoFormat.code === "hevc"
                                               ? [qsTr("Auto"), qsTr("Intel Quick Sync"), qsTr("Software")]
                                               : [qsTr("Software")]
                                        currentIndex: model.length === 1 ? 0 : root.videoEncoderIndex
                                        onActivated: index => root.videoEncoderIndex = model.length === 1 ? 2 : index
                                    }
                                }

                                ExportSettingRow {
                                    label: qsTr("Max bitrate")
                                    description: qsTr("Mbps ceiling")
                                    ExportSlider {
                                        Layout.fillWidth: true
                                        // 8K needs far more headroom than 1080p
                                        // ever did; 200 is where the export
                                        // builder clamps, so stop at the same
                                        // place rather than above it.
                                        from: 1; to: 200; stepSize: 1
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
                                    label: qsTr("Render quality")
                                    CaptionSwitch {
                                        text: qsTr("Use Maximum Render Quality")
                                        checked: root.maximumRenderQuality
                                        onToggled: root.maximumRenderQuality = checked
                                    }
                                }
                            }

                            ExportSection {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                title: qsTr("Audio")
                                summary: root.audioEnabled
                                         ? root.audioSampleRate + " Hz, " + root.audioBitrateKbps + " kbps"
                                         : "Disabled"

                                ExportSettingRow {
                                    label: qsTr("Export audio")
                                    CaptionSwitch {
                                        text: checked ? qsTr("Enabled") : qsTr("Disabled")
                                        checked: root.audioEnabled
                                        onToggled: root.audioEnabled = checked
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled
                                    label: qsTr("Sample rate")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: ["44100 Hz", "48000 Hz"]
                                        currentIndex: root.audioSampleRateIndex
                                        onActivated: index => root.audioSampleRateIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled
                                    label: qsTr("Channels")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: [qsTr("Mono"), qsTr("Stereo")]
                                        currentIndex: root.audioChannelsIndex
                                        onActivated: index => root.audioChannelsIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled
                                    label: qsTr("Audio bitrate")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: ["128 kbps", "192 kbps", "256 kbps", "320 kbps"]
                                        currentIndex: root.audioBitrateIndex
                                        onActivated: index => root.audioBitrateIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled
                                    label: qsTr("Normalize loudness")
                                    CaptionSwitch {
                                        text: checked ? qsTr("Enabled") : qsTr("Disabled")
                                        checked: root.normalizeLoudness
                                        onToggled: root.normalizeLoudness = checked
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled && root.normalizeLoudness
                                    label: qsTr("Loudness target")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: root.loudnessTargets.map(t => t.label)
                                        currentIndex: root.loudnessTargetIndex
                                        onActivated: index => root.loudnessTargetIndex = index
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled
                                    label: qsTr("Duck music under narration")
                                    CaptionSwitch {
                                        text: checked ? qsTr("Enabled") : qsTr("Disabled")
                                        checked: root.duckMusicUnderNarration
                                        onToggled: root.duckMusicUnderNarration = checked
                                    }
                                }

                                ExportSettingRow {
                                    enabled: root.audioEnabled && root.duckMusicUnderNarration
                                    label: qsTr("Ducking amount")
                                    ExportComboBox {
                                        Layout.fillWidth: true
                                        model: root.duckAmounts.map(a => a.label)
                                        currentIndex: root.duckAmountIndex
                                        onActivated: index => root.duckAmountIndex = index
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true; Layout.preferredHeight: 12 }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Backend.exportInProgress ? 80 : 64
            color: Theme.bgSidebar

            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.outline }

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
                                                          : Backend.exportStatus || qsTr("Ready to export")
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
                    ExportProgressDetails {
                        Layout.fillWidth: true
                    }
                    // The track exists only while there is progress to show. Idle, it
                    // was a 3px dark rule that began under the status text and stopped
                    // in mid-air short of the buttons, which reads as a broken border
                    // rather than as an empty progress bar.
                    Rectangle {
                        visible: Backend.exportInProgress || Backend.exportProgress > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        radius: 2
                        color: Theme.bgPrimary
                        Rectangle {
                            visible: Backend.exportProgress > 0
                            width: Math.max(parent.radius * 2,
                                            parent.width * Backend.exportProgress)
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                        }
                    }
                }

                SecondaryButton {
                    text: Backend.exportInProgress ? qsTr("Cancel Export") : qsTr("Cancel")
                    enabled: Backend.exportInProgress
                    onClicked: Backend.cancelExport()
                }

                PrimaryButton {
                    text: Backend.exportInProgress ? qsTr("Exporting...") : qsTr("Export")
                    enabled: Backend.canExport && !Backend.exportInProgress
                             && root.outputPath !== ""
                    onClicked: Backend.startExportWithSettings(root.outputPath,
                                                                root.exportSettings())
                }
            }
        }
    }
}
