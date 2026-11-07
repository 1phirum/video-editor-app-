// qmllint disable
import QtQuick
import QtQuick.Controls
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"

Menu {
    id: root
    property var clipData: ({})
    property var targetIds: []
    readonly property var selectedClips: targetIds.map(function(id) { return Backend.clipById(String(id)) })
    property var mediaData: ({})
    // Set from the caller right before popup(): a Q_INVOKABLE result would go
    // stale on this long-lived Menu instance, so the state is pushed in instead.
    property bool clipboardHasClips: false
    readonly property var effects: clipData && clipData.effects ? clipData.effects : ({})
    readonly property bool hasEditableClip: Backend.editableClipIds(targetIds, "effects").length > 0
    readonly property bool hasVideo: Backend.editableClipIds(targetIds, "video").length > 0
    readonly property bool hasAudio: Backend.editableClipIds(targetIds, "audio").length > 0
    readonly property bool audioExtracted: selectedClips.some(function(clip) {
        return clip.separateAudio === true || (clip.kind === "audio"
            && (clip.extractedFromClipId || clip.linkedRole === "audio"))
    })
    readonly property bool clipEnabled: !selectedClips.some(function(clip) { return clip.enabled === false })
    readonly property bool hasLink: selectedClips.some(function(clip) { return Boolean(clip.linkGroupId) })
    readonly property bool hasGroup: selectedClips.some(function(clip) { return Boolean(clip.groupId) })

    signal splitRequested(string clipId)
    signal deleteRequested(string clipId, bool ripple)
    signal vocalRemovalRequested(string clipId)
    signal extractAudioRequested(string clipId)
    signal restoreAudioRequested(string clipId)
    signal removeSilenceRequested(string clipId)
    signal voiceChangerRequested(string clipId)
    signal noiseRemovalRequested(string clipId)
    signal enableToggleRequested(string clipId)
    signal cutRequested(string clipId)
    signal copyRequested(string clipId)
    signal pasteRequested()
    signal pasteAttributesRequested(string clipId)
    signal linkRequested(string clipId)
    signal unlinkRequested(string clipId)
    signal groupRequested(string clipId)
    signal ungroupRequested(string clipId)
    signal labelRequested(string clipId, string color)
    signal speedDurationRequested(string clipId)
    signal sceneDetectRequested(string clipId)
    signal fieldOptionsRequested(string clipId)
    signal audioGainRequested(string clipId)

    implicitWidth: 260
    delegate: DarkMenuItem {}
    background: Rectangle {
        color: Theme.menuBg
        border.color: Theme.menuBorder
        radius: Theme.radiusSm
    }

    DarkMenuItem {
        text: qsTr("Open Effect Controls")
        onTriggered: Backend.requestEffectControls(root.clipData.id)
    }
    DarkMenuItem {
        text: qsTr("Browse Effects...")
        enabled: root.hasEditableClip
        onTriggered: Backend.requestEffectsBrowser(root.clipData.id)
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        text: root.clipEnabled ? qsTr("Disable Clip") : qsTr("Enable Clip")
        onTriggered: root.enableToggleRequested(root.clipData.id)
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        text: qsTr("Cut")
        onTriggered: root.cutRequested(root.clipData.id)
    }
    DarkMenuItem {
        text: qsTr("Copy")
        onTriggered: root.copyRequested(root.clipData.id)
    }
    DarkMenuItem {
        text: qsTr("Paste")
        enabled: root.clipboardHasClips
        onTriggered: root.pasteRequested()
    }
    DarkMenuItem {
        text: qsTr("Paste Attributes")
        enabled: root.clipboardHasClips && root.hasEditableClip
        onTriggered: root.pasteAttributesRequested(root.clipData.id)
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        text: qsTr("Speed / Duration...")
        enabled: root.hasEditableClip
        onTriggered: root.speedDurationRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasVideo
        enabled: Backend.editableClipIds(root.targetIds, "movingVideo").length > 0
        text: qsTr("Scene Edit Detection...")
        onTriggered: root.sceneDetectRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasAudio
        text: qsTr("Audio Gain...")
        onTriggered: root.audioGainRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasVideo
        text: qsTr("Field Options...")
        onTriggered: root.fieldOptionsRequested(root.clipData.id)
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        visible: root.selectedClips.some(function(clip) {
            return clip.kind === "video" && clip.separateAudio !== true
        })
        text: qsTr("Extract Audio")
        onTriggered: root.extractAudioRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.audioExtracted
        text: qsTr("Restore Clip Audio")
        onTriggered: root.restoreAudioRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasAudio
        text: root.effects.vocalRemoval
              ? qsTr("Disable Remove Vocal")
              : qsTr("Remove Vocals (AI)")
        onTriggered: root.vocalRemovalRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasAudio
        text: qsTr("Audio Cleanup Controls...")
        onTriggered: Backend.requestEffectControls(root.clipData.id)
    }
    DarkMenuItem {
        objectName: "removeNoiseMenuItem"
        visible: root.hasAudio
        text: qsTr("Remove Noise...")
        onTriggered: root.noiseRemovalRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasAudio
        text: qsTr("Voice Changer...")
        onTriggered: root.voiceChangerRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasAudio
        text: qsTr("Remove Silences...")
        onTriggered: root.removeSilenceRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasVideo
        text: root.effects.horizontalFlip ? qsTr("Disable Horizontal Flip")
                                          : qsTr("Flip Horizontal")
        onTriggered: Backend.applyClipSelectionCommand(root.targetIds, "setting",
                         {key: "horizontalFlip", value: !Boolean(root.effects.horizontalFlip)})
    }
    DarkMenuItem {
        visible: root.hasVideo
        text: root.effects.verticalFlip ? qsTr("Disable Vertical Flip")
                                        : qsTr("Flip Vertical")
        onTriggered: Backend.applyClipSelectionCommand(root.targetIds, "setting",
                         {key: "verticalFlip", value: !Boolean(root.effects.verticalFlip)})
    }
    DarkMenuItem {
        text: qsTr("Reset Clip Effects")
        enabled: root.selectedClips.some(function(clip) {
            return clip.effects !== undefined || (clip.effectStack && clip.effectStack.length > 0)
        })
        onTriggered: Backend.applyClipSelectionCommand(root.targetIds, "reset", {})
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        text: root.hasLink ? qsTr("Unlink") : qsTr("Link")
        onTriggered: root.hasLink ? root.unlinkRequested(root.clipData.id)
                                  : root.linkRequested(root.clipData.id)
    }
    DarkMenuItem {
        text: qsTr("Group")
        onTriggered: root.groupRequested(root.clipData.id)
    }
    DarkMenuItem {
        text: qsTr("Ungroup")
        enabled: root.hasGroup
        onTriggered: root.ungroupRequested(root.clipData.id)
    }
    Menu {
        id: labelMenu
        title: qsTr("Label")
        implicitWidth: 190
        background: Rectangle {
            color: Theme.menuBg
            border.color: Theme.menuBorder
            radius: Theme.radiusSm
        }
        // Premiere-style rows: a colour swatch next to the name, and the label
        // currently on the clip is picked out in bold with a lit swatch border.
        Repeater {
            model: [
                { name: qsTr("None"),      swatch: "" },
                { name: qsTr("Violet"),    swatch: "#9b7cff" },
                { name: qsTr("Iris"),      swatch: "#5a6ee0" },
                { name: qsTr("Caribbean"), swatch: "#00a99d" },
                { name: qsTr("Cerulean"),  swatch: "#3d9be9" },
                { name: qsTr("Forest"),    swatch: "#3f8f4f" },
                { name: qsTr("Rose"),      swatch: "#e0607e" },
                { name: qsTr("Mango"),     swatch: "#e0922f" }
            ]
            MenuItem {
                id: labelItem
                required property var modelData
                implicitWidth: 190
                implicitHeight: 32
                leftPadding: 12
                rightPadding: 12
                readonly property bool current:
                    String(root.clipData.label || "") === modelData.swatch
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                contentItem: Row {
                    spacing: 10
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 13
                        height: 13
                        radius: 3
                        color: modelData.swatch !== "" ? modelData.swatch
                                                       : "transparent"
                        border.width: labelItem.current ? 2 : 1
                        border.color: labelItem.current
                                      ? Theme.menuText
                                      : (modelData.swatch !== ""
                                         ? Qt.darker(modelData.swatch, 1.4)
                                         : Theme.menuBorder)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        color: Theme.menuText
                        font.pixelSize: Theme.fsMd
                        font.weight: labelItem.current ? Font.DemiBold
                                                       : Font.Normal
                    }
                }
                background: Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    radius: Theme.radiusSm
                    color: labelItem.highlighted ? Theme.menuHighlight : "transparent"
                }
                onTriggered: root.labelRequested(root.clipData.id,
                                                 modelData.swatch)
            }
        }
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        text: qsTr("Split at Playhead")
        enabled: root.selectedClips.some(function(clip) {
            return Backend.playheadMs > Number(clip.startMs || 0)
                && Backend.playheadMs < Number(clip.startMs || 0) + Number(clip.durationMs || 0)
        })
        onTriggered: root.splitRequested(root.clipData.id)
    }
    DarkMenuItem {
        text: qsTr("Delete Clip")
        onTriggered: root.deleteRequested(root.clipData.id, false)
    }
    DarkMenuItem {
        text: qsTr("Ripple Delete")
        onTriggered: root.deleteRequested(root.clipData.id, true)
    }
}
