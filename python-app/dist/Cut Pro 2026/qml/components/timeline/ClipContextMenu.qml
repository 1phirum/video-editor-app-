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
    property var mediaData: ({})
    readonly property var effects: clipData && clipData.effects ? clipData.effects : ({})
    readonly property bool hasEditableClip: Boolean(clipData && clipData.id)
                                            && clipData.kind !== "subtitle"
    readonly property bool hasVideo: clipData && clipData.kind !== "audio"
    // Video clips carry their audio in the linked source media. Keep the
    // command visible for those clips even when metadata probing was delayed.
    readonly property bool hasAudio: clipData.kind === "video"
                                     || (mediaData && Number(mediaData.channels || 0) > 0)
    signal splitRequested(string clipId)
    signal deleteRequested(string clipId, bool ripple)
    signal vocalRemovalRequested(string clipId)

    implicitWidth: 260
    delegate: DarkMenuItem {}
    background: Rectangle {
        color: Theme.bgPanel
        border.color: Theme.border
        radius: Theme.radiusSm
    }

    DarkMenuItem {
        text: "Open Effect Controls"
        onTriggered: Backend.requestEffectControls(root.clipData.id)
    }
    DarkMenuItem {
        text: "Browse Effects..."
        enabled: root.hasEditableClip
        onTriggered: Backend.requestEffectsBrowser(root.clipData.id)
    }
    MenuSeparator {}
    DarkMenuItem {
        visible: root.hasAudio
        text: root.effects.vocalRemoval
              ? "Disable Remove Vocal"
              : "Remove Vocals (AI)"
        onTriggered: root.vocalRemovalRequested(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasAudio
        text: "Audio Cleanup Controls..."
        onTriggered: Backend.requestEffectControls(root.clipData.id)
    }
    DarkMenuItem {
        visible: root.hasVideo
        text: root.effects.horizontalFlip ? "Disable Horizontal Flip"
                                          : "Flip Horizontal"
        onTriggered: Backend.setClipEffectSetting(
                         root.clipData.id, "horizontalFlip",
                         !Boolean(root.effects.horizontalFlip))
    }
    DarkMenuItem {
        visible: root.hasVideo
        text: root.effects.verticalFlip ? "Disable Vertical Flip"
                                        : "Flip Vertical"
        onTriggered: Backend.setClipEffectSetting(
                         root.clipData.id, "verticalFlip",
                         !Boolean(root.effects.verticalFlip))
    }
    DarkMenuItem {
        text: "Reset Clip Effects"
        enabled: Boolean(root.clipData && root.clipData.id)
                 && (root.clipData.effects !== undefined
                     || Boolean(root.clipData.effectStack
                                && root.clipData.effectStack.length > 0))
        onTriggered: Backend.resetClipEffectSettings(root.clipData.id)
    }
    MenuSeparator {}
    DarkMenuItem {
        text: "Split at Playhead"
        enabled: Backend.playheadMs > Number(root.clipData.startMs || 0)
                 && Backend.playheadMs < Number(root.clipData.startMs || 0)
                                        + Number(root.clipData.durationMs || 0)
        onTriggered: root.splitRequested(root.clipData.id)
    }
    DarkMenuItem {
        text: "Delete Clip"
        onTriggered: root.deleteRequested(root.clipData.id, false)
    }
    DarkMenuItem {
        text: "Ripple Delete"
        onTriggered: root.deleteRequested(root.clipData.id, true)
    }
}
