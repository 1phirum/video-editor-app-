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

// Keyframe lanes for the Effect Controls panel. Driven by the same flat row
// model as the parameter tree on the left, so lane N always lines up with row N
// and the row separators run straight across the whole panel. The lanes span
// exactly the selected clip — keyframe times are absolute sequence ms.
Item {
    id: root

    property var rows: []
    property string clipId: ""
    property string clipLabel: ""
    property real startMs: 0
    property real spanMs: 1000
    // Bumped on keyframesChanged: keyframesFor() is a plain call, so the
    // bindings below need a tracked property to depend on.
    property int revision: 0

    readonly property real pxPerMs: root.width / Math.max(1, root.spanMs)

    function xForTime(ms) { return (ms - root.startMs) * root.pxPerMs }
    function timeForX(x) {
        return root.startMs + x / Math.max(0.000001, root.pxPerMs)
    }
    function clampTime(ms) {
        return Math.max(root.startMs, Math.min(root.startMs + root.spanMs, ms))
    }

    implicitHeight: laneColumn.height
    clip: true

    Column {
        id: laneColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        Repeater {
            model: root.rows
            delegate: Item {
                id: lane
                required property var modelData
                width: laneColumn.width
                height: Theme.ecRowHeight

                readonly property string kind: String(lane.modelData.kind || "")
                readonly property bool isBand: lane.kind === "band"
                readonly property string channel: String(lane.modelData.id || "")
                readonly property bool keyframable:
                    lane.kind === "param" && lane.modelData.kf === true
                    && String(lane.modelData.instanceId || "") === ""
                readonly property var frames:
                    (root.revision >= 0 && lane.keyframable && root.clipId !== "")
                    ? Backend.keyframeEngine.keyframesFor(root.clipId, lane.channel)
                    : []

                // Band stripe continues across the lanes.
                Rectangle {
                    anchors.fill: parent
                    visible: lane.isBand
                    color: Theme.ecBand
                }

                // The clip itself, the way Premiere draws it on the band row.
                Rectangle {
                    id: clipBar
                    visible: lane.isBand && root.clipId !== ""
                    x: 1
                    width: Math.max(0, parent.width - 2)
                    height: parent.height - 9
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 2
                    color: Theme.ecClipBand

                    Text {
                        x: 5
                        width: Math.max(0, clipBar.width - 10)
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.clipLabel
                        elide: Text.ElideRight
                        color: "#dbeef3"
                        font.pixelSize: Theme.fsXs
                    }
                }

                // Double-click an animated lane to add a keyframe without
                // changing the curve, as in Premiere.
                MouseArea {
                    anchors.fill: parent
                    enabled: lane.frames.length > 0
                    onDoubleClicked: (mouse) => {
                        var time = Math.round(root.clampTime(root.timeForX(mouse.x)))
                        Backend.keyframeEngine.addKeyframe(
                                    root.clipId, lane.channel, time,
                                    Backend.keyframeEngine.interpolatedValue(
                                        root.clipId, lane.channel, time))
                    }
                }

                // Faint span between the first and last keyframe.
                Rectangle {
                    id: spanLine
                    visible: lane.frames.length > 1
                    anchors.verticalCenter: parent.verticalCenter
                    x: lane.frames.length > 1
                       ? root.xForTime(Number(lane.frames[0].timeMs)) : 0
                    width: lane.frames.length > 1
                           ? Math.max(0, root.xForTime(Number(
                                 lane.frames[lane.frames.length - 1].timeMs)) - spanLine.x)
                           : 0
                    height: 1
                    color: Theme.accent
                    opacity: 0.35
                }

                // ---- Keyframes -------------------------------------------
                Repeater {
                    model: lane.frames
                    delegate: Item {
                        id: kf
                        required property var modelData
                        readonly property real baseTime: Number(kf.modelData.timeMs)
                        property real dragMs: 0
                        readonly property real shownTime:
                            root.clampTime(kf.baseTime + kf.dragMs)

                        x: root.xForTime(kf.shownTime) - width / 2
                        // Repeater delegates can briefly have no parent while
                        // they are being instantiated. The enclosing lane is
                        // stable and provides the intended vertical center.
                        anchors.verticalCenter: lane.verticalCenter
                        width: 13
                        height: 13

                        Rectangle {
                            anchors.centerIn: parent
                            width: 9
                            height: 9
                            rotation: 45
                            color: Theme.accent
                            border.width: 1
                            border.color: grab.pressed || grab.containsMouse
                                          ? "#ffffff" : Qt.darker(Theme.accent, 1.7)
                        }

                        MouseArea {
                            id: grab
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.SizeHorCursor
                            property real pressX: 0
                            property bool moved: false

                            onPressed: (mouse) => {
                                grab.pressX = grab.mapToItem(lane, mouse.x, 0).x
                                grab.moved = false
                            }
                            onPositionChanged: (mouse) => {
                                if (!grab.pressed)
                                    return
                                var dx = grab.mapToItem(lane, mouse.x, 0).x - grab.pressX
                                if (!grab.moved && Math.abs(dx) < 3)
                                    return
                                grab.moved = true
                                kf.dragMs = dx / Math.max(0.000001, root.pxPerMs)
                            }
                            onReleased: (mouse) => {
                                var from = Math.round(kf.baseTime)
                                if (grab.moved) {
                                    var target = Math.round(kf.shownTime)
                                    kf.dragMs = 0
                                    if (target !== from) {
                                        Backend.keyframeEngine.removeKeyframe(
                                                    root.clipId, lane.channel, from)
                                        Backend.keyframeEngine.addKeyframe(
                                                    root.clipId, lane.channel, target,
                                                    kf.modelData.value)
                                    }
                                } else if ((mouse.modifiers & Qt.AltModifier) !== 0) {
                                    Backend.keyframeEngine.removeKeyframe(
                                                root.clipId, lane.channel, from)
                                } else {
                                    Backend.playheadMs = from
                                }
                            }
                            onCanceled: kf.dragMs = 0
                            ToolTip.visible: grab.containsMouse && !grab.pressed
                            ToolTip.text: "Drag to move · Alt+click to delete"
                        }
                    }
                }

                // ---- Row separator ---------------------------------------
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: lane.isBand ? Theme.border : Theme.ecRowLine
                }
            }
        }
    }

    // ---- Playhead --------------------------------------------------------
    Rectangle {
        id: playhead
        x: root.xForTime(Math.max(0, Number(Backend.playheadMs)))
        y: 0
        width: 1
        height: root.height
        color: Theme.accent
        visible: playhead.x >= -1 && playhead.x <= root.width + 1
    }

    Connections {
        target: Backend.keyframeEngine
        function onKeyframesChanged(clipId) {
            if (String(clipId) === root.clipId)
                root.revision += 1
        }
    }
}
