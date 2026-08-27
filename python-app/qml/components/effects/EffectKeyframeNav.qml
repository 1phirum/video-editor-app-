pragma ComponentBehavior: Bound
// qmllint disable
import QtQuick
import QtQuick.Controls
import CutPro 1.0
import "../../theme"
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

// Premiere's keyframe navigator: ◀ ◆ ▶ — jump to the previous keyframe, add or
// remove one at the playhead, jump to the next.
Row {
    id: root

    property string clipId: ""
    property string effectKey: ""
    property real currentValue: 0
    // Bumped whenever the engine reports a change so the bindings below
    // re-evaluate (keyframesFor() is a plain call, not a tracked property).
    property int revision: 0

    readonly property int playhead: Math.max(0, Number(Backend.playheadMs))
    readonly property real matchedTime:
        root.revision >= 0 ? root.keyframeTimeNear(root.playhead) : -1
    readonly property bool onKeyframe: matchedTime >= 0

    spacing: 0

    // Half a frame at 25 fps: the playhead lands close enough to count as "on"
    // the keyframe after seeking to it.
    function keyframeTimeNear(timeMs) {
        if (root.clipId === "" || root.effectKey === "")
            return -1
        var frames = Backend.keyframeEngine.keyframesFor(root.clipId,
                                                         root.effectKey)
        for (var i = 0; i < frames.length; ++i) {
            var time = Number(frames[i].timeMs)
            if (Math.abs(time - timeMs) <= 20)
                return time
        }
        return -1
    }

    IconButton {
        boxSize: 18
        glyphSize: 11
        iconName: "chevron-left"
        restColor: Theme.textSecondary
        enabled: root.revision >= 0
                 && Backend.keyframeEngine.previousTime(root.clipId, root.effectKey,
                                                        root.playhead) >= 0
        opacity: enabled ? 1 : 0.35
        onClicked: {
            var time = Backend.keyframeEngine.previousTime(root.clipId,
                                                           root.effectKey,
                                                           root.playhead)
            if (time >= 0)
                Backend.playheadMs = time
        }
        ToolTip.visible: hovered
        ToolTip.text: "Go to previous keyframe"
    }

    Item {
        width: 20
        height: 18

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            rotation: 45
            color: root.onKeyframe ? Theme.accent : "transparent"
            border.width: 1
            border.color: root.onKeyframe ? Theme.accent
                                          : diamondHover.hovered ? Theme.textPrimary
                                          : Theme.textSecondary
        }
        HoverHandler {
            id: diamondHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: {
                if (root.onKeyframe)
                    Backend.keyframeEngine.removeKeyframe(root.clipId,
                                                          root.effectKey,
                                                          root.matchedTime)
                else
                    Backend.keyframeEngine.addKeyframe(root.clipId,
                                                       root.effectKey,
                                                       root.playhead,
                                                       root.currentValue)
            }
        }
        ToolTip.visible: diamondHover.hovered
        ToolTip.text: root.onKeyframe ? "Remove keyframe" : "Add keyframe"
    }

    IconButton {
        boxSize: 18
        glyphSize: 11
        iconName: "chevron-right"
        restColor: Theme.textSecondary
        enabled: root.revision >= 0
                 && Backend.keyframeEngine.nextTime(root.clipId, root.effectKey,
                                                    root.playhead) >= 0
        opacity: enabled ? 1 : 0.35
        onClicked: {
            var time = Backend.keyframeEngine.nextTime(root.clipId,
                                                       root.effectKey,
                                                       root.playhead)
            if (time >= 0)
                Backend.playheadMs = time
        }
        ToolTip.visible: hovered
        ToolTip.text: "Go to next keyframe"
    }

    Connections {
        target: Backend.keyframeEngine
        function onKeyframesChanged(clipId) {
            if (String(clipId) === root.clipId)
                root.revision += 1
        }
    }
}
