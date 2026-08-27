// qmllint disable
import QtQuick
import CutPro 1.0
import "../../theme"
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

// Time ruler above the keyframe lanes. Spans exactly the selected clip, the way
// Premiere scopes the Effect Controls timeline to the clip you are editing.
Rectangle {
    id: root

    property real startMs: 0
    property real spanMs: 1000

    color: Theme.bgPanel
    clip: true

    readonly property real pxPerMs: width / Math.max(1, root.spanMs)
    readonly property real step: root.niceStep()
    readonly property real firstTick: Math.ceil(root.startMs / root.step) * root.step
    readonly property int tickCount:
        Math.max(0, Math.floor((root.startMs + root.spanMs - root.firstTick) / root.step) + 1)

    function xForTime(ms) { return (ms - root.startMs) * root.pxPerMs }
    function timeForX(x) { return root.startMs + x / Math.max(0.000001, root.pxPerMs) }

    // Smallest round step that leaves room for a timecode label.
    function niceStep() {
        var steps = [200, 500, 1000, 2000, 5000, 10000, 15000, 30000, 60000,
                     120000, 300000, 600000, 900000, 1800000, 3600000]
        for (var i = 0; i < steps.length; ++i) {
            if (steps[i] * root.pxPerMs >= 82)
                return steps[i]
        }
        return steps[steps.length - 1]
    }

    function timecode(milliseconds) {
        var ms = Math.max(0, milliseconds)
        var frames = Math.floor((ms % 1000) / 40)
        var total = Math.floor(ms / 1000)
        function pad(value) { return value < 10 ? "0" + value : String(value) }
        return pad(Math.floor(total / 3600)) + ":" + pad(Math.floor(total / 60) % 60)
               + ":" + pad(total % 60) + ":" + pad(frames)
    }

    Repeater {
        model: root.tickCount
        delegate: Item {
            required property int index
            x: root.xForTime(root.firstTick + index * root.step)
            y: 0
            width: 1
            height: root.height

            Rectangle {
                anchors.bottom: parent.bottom
                width: 1
                height: 7
                color: Theme.textMuted
            }
            Text {
                x: 4
                y: 4
                text: root.timecode(root.firstTick + parent.index * root.step)
                color: Theme.textMuted
                font.family: Theme.monoFont
                font.pixelSize: Theme.fsXs
            }
        }
    }

    // Minor ticks — four subdivisions between labels, as in Premiere.
    Repeater {
        model: Math.max(0, root.tickCount * 4)
        delegate: Rectangle {
            required property int index
            x: root.xForTime(root.firstTick + (index + 1) * root.step / 4
                             - root.step)
            anchors.bottom: parent.bottom
            width: 1
            height: 3
            color: Theme.textMuted
            opacity: 0.5
            visible: x >= 0 && x <= root.width
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    // ---- Playhead marker -------------------------------------------------
    Item {
        id: marker
        x: root.xForTime(Math.max(0, Number(Backend.playheadMs)))
        y: 0
        width: 1
        height: root.height
        visible: marker.x >= -1 && marker.x <= root.width + 1

        Rectangle {
            x: -5
            y: 2
            width: 11
            height: 11
            radius: 2
            color: Theme.accent
        }
        Rectangle {
            x: 0
            y: 2
            width: 1
            height: parent.height - 2
            color: Theme.accent
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.SizeHorCursor
        onPressed: (mouse) => root.scrubTo(mouse.x)
        onPositionChanged: (mouse) => {
            if (pressed)
                root.scrubTo(mouse.x)
        }
    }

    function scrubTo(x) {
        var target = root.timeForX(x)
        Backend.playheadMs = Math.round(
                    Math.max(Math.max(0, root.startMs),
                             Math.min(root.startMs + root.spanMs, target)))
    }
}
