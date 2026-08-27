pragma ComponentBehavior: Bound
// qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"

Item {
    id: root
    readonly property var clipData: Backend.selectedClip
    readonly property var effects: clipData && clipData.effects ? clipData.effects : ({})
    readonly property var mediaData: mediaForClip(clipData)
    readonly property bool hasClip: Boolean(clipData && clipData.id)
    readonly property bool hasVisual: hasClip && clipData.kind !== "audio"
                                      && clipData.kind !== "subtitle"
    readonly property bool hasAudio: Boolean(mediaData)
                                     && Number(mediaData.channels || 0) > 0
    readonly property var effectStack: clipData && clipData.effectStack
                                       ? clipData.effectStack : []

    function mediaForClip(clip) {
        if (!clip || !clip.mediaId)
            return null
        for (var i = 0; i < Backend.media.length; ++i) {
            if (Backend.media[i].id === clip.mediaId)
                return Backend.media[i]
        }
        return null
    }
    function effectValue(key, fallback) {
        var current = effects[key]
        return current === undefined || current === null ? fallback : current
    }
    function setEffect(key, value) {
        if (clipData && clipData.id)
            Backend.setClipEffectSetting(clipData.id, key, value)
    }
    function definitionForId(definitionId) {
        for (var i = 0; i < Backend.effectDefinitions.length; ++i) {
            if (Backend.effectDefinitions[i].id === definitionId)
                return Backend.effectDefinitions[i]
        }
        return ({ name: "Missing effect", parameters: [] })
    }

    Loader {
        anchors.fill: parent
        active: root.hasClip && root.clipData.kind === "subtitle"
        sourceComponent: SubtitleBlurControls {}
    }

    ColumnLayout {
        anchors.fill: parent
        visible: !root.hasClip
        Item { Layout.fillHeight: true }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            text: "Select a timeline clip or drop an effect onto it."
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        Item { Layout.fillHeight: true }
    }

    SplitView {
        anchors.fill: parent
        visible: root.hasClip && root.clipData.kind !== "subtitle"
        clip: true
        orientation: Qt.Horizontal

        ScrollView {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 270
            SplitView.preferredWidth: Math.max(310, parent.width * 0.58)
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                color: Theme.bgPanel
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 10
                    Text {
                        Layout.fillWidth: true
                        text: root.hasClip
                              ? (root.clipData.name
                                 || (root.mediaData ? root.mediaData.name : "Clip"))
                              : "Clip"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsMd
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: root.hasClip ? String(root.clipData.track || "") : ""
                        color: Theme.accent
                        font.pixelSize: Theme.fsXs
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
            }

            LumetriSection {
                title: "Motion"
                visible: root.hasVisual
                LumetriSlider { label: "Position X"; value: root.effectValue("positionX", 0); onValueCommitted: value => root.setEffect("positionX", value) }
                LumetriSlider { label: "Position Y"; value: root.effectValue("positionY", 0); onValueCommitted: value => root.setEffect("positionY", value) }
                LumetriSlider { label: "Rotation"; from: -180; to: 180; value: root.effectValue("rotation", 0); suffix: "deg"; onValueCommitted: value => root.setEffect("rotation", value) }
                KeyframableSlider { clipId: root.clipData.id; effectKey: "scale"; label: "Scale"; from: 10; to: 400; value: root.effectValue("scale", 100); suffix: "%"; onValueCommitted: value => root.setEffect("scale", value) }
                CaptionSwitch { text: "Uniform Scale"; checked: Boolean(root.effectValue("uniformScale", true)); onToggled: root.setEffect("uniformScale", checked) }
                KeyframableSlider { visible: !Boolean(root.effectValue("uniformScale", true)); clipId: root.clipData.id; effectKey: "scaleWidth"; label: "Scale Width"; from: 10; to: 600; value: root.effectValue("scaleWidth", 100); suffix: "%"; onValueCommitted: value => root.setEffect("scaleWidth", value) }
                KeyframableSlider { visible: !Boolean(root.effectValue("uniformScale", true)); clipId: root.clipData.id; effectKey: "scaleHeight"; label: "Scale Height"; from: 10; to: 600; value: root.effectValue("scaleHeight", 100); suffix: "%"; onValueCommitted: value => root.setEffect("scaleHeight", value) }
                KeyframableSlider { clipId: root.clipData.id; effectKey: "anchorPointX"; label: "Anchor X"; from: 0; to: 1; decimals: 2; value: root.effectValue("anchorPointX", 0.5); onValueCommitted: value => root.setEffect("anchorPointX", value) }
                KeyframableSlider { clipId: root.clipData.id; effectKey: "anchorPointY"; label: "Anchor Y"; from: 0; to: 1; decimals: 2; value: root.effectValue("anchorPointY", 0.5); onValueCommitted: value => root.setEffect("anchorPointY", value) }
                KeyframableSlider { clipId: root.clipData.id; effectKey: "antiFlicker"; label: "Anti-flicker"; from: 0; to: 1; decimals: 2; value: root.effectValue("antiFlicker", 0); onValueCommitted: value => root.setEffect("antiFlicker", value) }
                CaptionSwitch { text: "Flip Horizontal"; checked: Boolean(root.effectValue("horizontalFlip", false)); onToggled: root.setEffect("horizontalFlip", checked) }
                CaptionSwitch { text: "Flip Vertical"; checked: Boolean(root.effectValue("verticalFlip", false)); onToggled: root.setEffect("verticalFlip", checked) }
            }

            LumetriSection {
                title: "Crop"
                expanded: false
                visible: root.hasVisual
                LumetriSlider { label: "Left"; from: 0; to: 49; value: root.effectValue("cropLeft", 0); suffix: "%"; onValueCommitted: value => root.setEffect("cropLeft", value) }
                LumetriSlider { label: "Right"; from: 0; to: 49; value: root.effectValue("cropRight", 0); suffix: "%"; onValueCommitted: value => root.setEffect("cropRight", value) }
                LumetriSlider { label: "Top"; from: 0; to: 49; value: root.effectValue("cropTop", 0); suffix: "%"; onValueCommitted: value => root.setEffect("cropTop", value) }
                LumetriSlider { label: "Bottom"; from: 0; to: 49; value: root.effectValue("cropBottom", 0); suffix: "%"; onValueCommitted: value => root.setEffect("cropBottom", value) }
            }

            LumetriSection {
                title: "Opacity & Blur"
                expanded: false
                visible: root.hasVisual
                LumetriSlider { label: "Opacity"; from: 0; to: 100; value: root.effectValue("opacity", 100); suffix: "%"; onValueCommitted: value => root.setEffect("opacity", value) }
                LumetriSlider { label: "Gaussian Blur"; from: 0; to: 100; value: root.effectValue("blur", 0); onValueCommitted: value => root.setEffect("blur", value) }
                MaskToolbar { clipId: root.clipData.id; onMaskRequested: type => root.setEffect("maskType", type) }
                LumetriSlider { label: "Mask Feather"; from: 0; to: 250; value: root.effectValue("maskFeather", 0); suffix: "px"; onValueCommitted: value => root.setEffect("maskFeather", value) }
                LumetriSlider { label: "Mask Opacity"; from: 0; to: 100; value: root.effectValue("maskOpacity", 100); suffix: "%"; onValueCommitted: value => root.setEffect("maskOpacity", value) }
                LumetriSlider { label: "Mask Expansion"; from: -250; to: 250; value: root.effectValue("maskExpansion", 0); suffix: "px"; onValueCommitted: value => root.setEffect("maskExpansion", value) }
                CaptionSwitch { text: "Invert Mask"; checked: Boolean(root.effectValue("maskInverted", false)); onToggled: root.setEffect("maskInverted", checked) }
                BlendModeComboBox { value: String(root.effectValue("blendMode", "normal")); onModeSelected: value => root.setEffect("blendMode", value) }
            }

            LumetriSection {
                title: "Time Remapping"
                expanded: false
                visible: root.hasVisual
                KeyframableSlider { clipId: root.clipData.id; effectKey: "speed"; label: "Speed"; from: 1; to: 10000; value: root.effectValue("speed", 100); suffix: "%"; onValueCommitted: value => root.setEffect("speed", value) }
            }

            LumetriSection {
                title: "Audio"
                expanded: true
                visible: root.hasAudio
                LumetriSlider { label: "Volume"; from: -60; to: 12; decimals: 1; value: root.effectValue("volumeDb", 0); suffix: "dB"; onValueCommitted: value => root.setEffect("volumeDb", value) }
                LumetriSlider { label: "Pan"; from: -1; to: 1; decimals: 2; value: root.effectValue("pan", 0); onValueCommitted: value => root.setEffect("pan", value) }
                CaptionSwitch { text: "Bypass Volume"; checked: Boolean(root.effectValue("volumeBypass", false)); onToggled: root.setEffect("volumeBypass", checked) }
                CaptionSwitch { text: "Remove Vocals (AI)"; checked: Boolean(root.effectValue("vocalRemoval", false)); enabled: root.mediaData !== null && Number(root.mediaData.channels || 0) >= 2; onToggled: root.setEffect("vocalRemoval", checked) }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    text: ""
                    visible: text !== ""
                    color: String(root.effectValue("demucsPath", "")) !== ""
                           ? "#78c98a" : Theme.textMuted
                    font.pixelSize: Theme.fsXs
                    wrapMode: Text.WordWrap
                }
            }

            LumetriSection {
                title: "Channel Volume"
                expanded: false
                visible: root.hasAudio
                LumetriSlider { label: "Left"; from: -60; to: 15; decimals: 1; value: root.effectValue("channelVolumeLeft", 0); suffix: "dB"; onValueCommitted: value => root.setEffect("channelVolumeLeft", value) }
                LumetriSlider { label: "Right"; from: -60; to: 15; decimals: 1; value: root.effectValue("channelVolumeRight", 0); suffix: "dB"; onValueCommitted: value => root.setEffect("channelVolumeRight", value) }
            }

            LumetriSection {
                title: "Panner"
                expanded: false
                visible: root.hasAudio
                LumetriSlider { label: "Balance"; from: -100; to: 100; value: root.effectValue("balance", 0); suffix: "%"; onValueCommitted: value => root.setEffect("balance", value) }
            }

            LumetriSection {
                title: "Audio Cleanup"
                expanded: false
                visible: root.hasAudio
                LumetriSlider { label: "Noise Reduction"; from: 0; to: 100; value: root.effectValue("noiseReduction", 0); onValueCommitted: value => root.setEffect("noiseReduction", value) }
                LumetriSlider { label: "High Pass"; from: 0; to: 1000; value: root.effectValue("highPassHz", 0); suffix: "Hz"; onValueCommitted: value => root.setEffect("highPassHz", value) }
                LumetriSlider { label: "Low Pass"; from: 0; to: 22000; value: root.effectValue("lowPassHz", 0); suffix: "Hz"; onValueCommitted: value => root.setEffect("lowPassHz", value) }
                CaptionSwitch { text: "Compressor"; checked: Boolean(root.effectValue("compressor", false)); onToggled: root.setEffect("compressor", checked) }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                color: Theme.bgPanel
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    Text {
                        Layout.fillWidth: true
                        text: "Applied Effects"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fsSm
                        font.weight: Font.DemiBold
                    }
                    IconButton {
                        iconName: "plus"
                        boxSize: 24
                        glyphSize: 13
                        onClicked: Backend.requestEffectsBrowser(root.clipData.id)
                        ToolTip.visible: hovered
                        ToolTip.text: "Browse effects"
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.border
                }
            }

            Repeater {
                model: root.effectStack
                delegate: EffectStackSection {
                    required property int index
                    required property var modelData
                    clipId: root.clipData.id
                    instanceData: modelData
                    definitionData: root.definitionForId(modelData.definitionId)
                    stackIndex: index
                    stackCount: root.effectStack.length
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.margins: 12
                visible: root.effectStack.length === 0
                text: "No applied effects"
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 12
                Item { Layout.fillWidth: true }
                LumetriActionButton {
                    text: "Reset All"
                    enabled: root.hasClip
                             && (root.clipData.effects !== undefined
                                 || root.effectStack.length > 0)
                    onClicked: Backend.resetClipEffectSettings(root.clipData.id)
                }
            }
        }

        }

        EffectKeyframeTimeline {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 210
            SplitView.preferredWidth: Math.max(250, parent.width * 0.42)
            clipId: root.clipData ? String(root.clipData.id || "") : ""
        }
    }
}
