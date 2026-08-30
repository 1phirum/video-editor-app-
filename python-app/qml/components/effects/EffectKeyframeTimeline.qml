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
// and the row separators run straight across the whole panel. The lanes show the
// panel's visible window, which is not always the whole clip — keyframe times
// are absolute sequence ms either way.
Item {
    id: root

    property var rows: []
    property string clipId: ""
    property string clipLabel: ""
    property real startMs: 0
    property real spanMs: 1000
    // The clip's own extent. Separate from the window because an edit has to be
    // clamped to the clip while only the view is clamped to the window; when it
    // is unset (zero span) the window is the only bound there is.
    property real clipStartMs: 0
    property real clipSpanMs: 0
    // Bumped on keyframesChanged: keyframesFor() is a plain call, so the
    // bindings below need a tracked property to depend on.
    property int revision: 0

    signal zoomRequested(real factor, real anchorMs)
    signal panRequested(real deltaMs)

    readonly property real pxPerMs: root.width / Math.max(1, root.spanMs)

    readonly property real limitStartMs:
        root.clipSpanMs > 0 ? root.clipStartMs : root.startMs
    readonly property real limitEndMs: root.clipSpanMs > 0
        ? root.clipStartMs + root.clipSpanMs : root.startMs + root.spanMs

    function xForTime(ms) { return (ms - root.startMs) * root.pxPerMs }
    function timeForX(x) {
        return root.startMs + x / Math.max(0.000001, root.pxPerMs)
    }
    // Clamped to the clip rather than to the window: a keyframe dragged towards
    // the edge of a zoomed-in view belongs where the pointer put it, and pinning
    // it to the edge of the view would silently move every keyframe the user
    // scrolled past.
    function clampTime(ms) {
        return Math.max(root.limitStartMs, Math.min(root.limitEndMs, ms))
    }

    // Ctrl or Alt zooms about the pointer and Shift pans, both delegated to the
    // panel because the ruler has to stay on the same span. Plain wheel is left
    // alone on purpose - it belongs to the Flickable these lanes sit in, which
    // is how the parameter list scrolls.
    function wheelZoom(event) {
        var ticks = event.angleDelta.y / 120
        if (ticks !== 0)
            root.zoomRequested(Math.pow(0.8, ticks), root.timeForX(event.x))
    }

    WheelHandler {
        acceptedModifiers: Qt.ControlModifier
        onWheel: (event) => root.wheelZoom(event)
    }
    WheelHandler {
        acceptedModifiers: Qt.AltModifier
        onWheel: (event) => root.wheelZoom(event)
    }
    WheelHandler {
        acceptedModifiers: Qt.ShiftModifier
        onWheel: (event) => {
            var ticks = event.angleDelta.y / 120
            if (ticks !== 0)
                root.panRequested(-ticks * root.spanMs * 0.15)
        }
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
            // Recorded rather than clamped. rows is the parameter list of one
            // effect stack, so this cannot be capped without hiding parameters
            // the user needs - but if it is ever the model that explodes, the
            // count is the evidence, and note() costs one integer store.
            onCountChanged: ModelGuard.note("keyframes.lanes", count)
            delegate: Item {
                id: lane
                required property var modelData
                width: laneColumn.width
                height: Theme.ecRowHeight

                readonly property string kind: String(lane.modelData.kind || "")
                readonly property bool isBand: lane.kind === "band"
                readonly property string channel:
                    String(lane.modelData.channel || lane.modelData.id || "")
                readonly property bool keyframable:
                    lane.kind === "param" && lane.modelData.kf === true
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
                // Placed by time, not stretched to the lane: once the lanes show
                // a window, a bar pinned to the lane edges claims the whole
                // width whatever is on screen, which would draw a clip that ends
                // at 25 h as if it filled a ten-minute view. The ends are
                // clamped to the lane so a zoomed-in view does not hand the
                // scene graph a rectangle a hundred thousand pixels wide.
                Rectangle {
                    id: clipBar
                    readonly property real leftEdge:
                        Math.max(1, root.xForTime(root.limitStartMs))
                    readonly property real rightEdge:
                        Math.min(parent.width - 1, root.xForTime(root.limitEndMs))
                    visible: lane.isBand && root.clipId !== ""
                             && clipBar.rightEdge > clipBar.leftEdge
                    x: clipBar.leftEdge
                    width: Math.max(0, clipBar.rightEdge - clipBar.leftEdge)
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

                // Faint span between the first and last keyframe, clamped to the
                // lane for the same reason the clip bar is: at a two-second
                // window a span across a whole clip is kilometres of rectangle.
                Rectangle {
                    id: spanLine
                    readonly property bool spans: lane.frames.length > 1
                    readonly property real leftEdge: spanLine.spans
                        ? Math.max(0, root.xForTime(
                                       Number(lane.frames[0].timeMs))) : 0
                    readonly property real rightEdge: spanLine.spans
                        ? Math.min(parent.width, root.xForTime(Number(
                              lane.frames[lane.frames.length - 1].timeMs))) : 0
                    visible: spanLine.spans && spanLine.rightEdge > spanLine.leftEdge
                    anchors.verticalCenter: parent.verticalCenter
                    x: spanLine.leftEdge
                    width: Math.max(0, spanLine.rightEdge - spanLine.leftEdge)
                    height: 1
                    color: Theme.accent
                    opacity: 0.35
                }

                // ---- Keyframes -------------------------------------------
                Repeater {
                    model: lane.frames
                    // Multiplied by the lane count above, so this is the pair
                    // that decides how many diamonds the panel instantiates.
                    onCountChanged: ModelGuard.note("keyframes.lane", count)
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
                        // Off-window keyframes stay instantiated - the model is
                        // the engine's list and its indices are what the drag
                        // and delete paths use - but they stop being drawn, and
                        // an invisible item takes no input either, so a
                        // zoomed-in view is not paved with hit targets sitting
                        // outside it. Half a diamond of slack keeps the one
                        // straddling the edge visible.
                        visible: kf.x > -kf.width && kf.x < root.width

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
                            AppCursor.name: "ResizeHorizontal"
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
