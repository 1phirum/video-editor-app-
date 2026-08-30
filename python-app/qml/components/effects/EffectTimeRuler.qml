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

// Time ruler above the keyframe lanes. Shows the panel's visible window, which
// is the whole clip when the whole clip is legible and a span around the
// playhead when it is not - see the window comment in ClipEffectControlsPanel.
Rectangle {
    id: root

    property real startMs: 0
    property real spanMs: 1000
    // The clip's own extent, so scrubbing stays inside the clip even when the
    // window has been zoomed into the middle of it. Zero means "not set", in
    // which case the visible window is the limit.
    property real clipStartMs: 0
    property real clipSpanMs: 0

    signal zoomRequested(real factor, real anchorMs)
    signal panRequested(real deltaMs)

    color: Theme.bgPanel
    clip: true

    readonly property real pxPerMs: width / Math.max(1, root.spanMs)
    readonly property real step: root.niceStep()
    readonly property int stepFields: root.fieldsFor(root.step)
    readonly property real firstTick: Math.ceil(root.startMs / root.step) * root.step
    readonly property int tickCount:
        Math.max(0, Math.floor((root.startMs + root.spanMs - root.firstTick) / root.step) + 1)

    readonly property real limitStartMs:
        root.clipSpanMs > 0 ? root.clipStartMs : root.startMs
    readonly property real limitEndMs: root.clipSpanMs > 0
        ? root.clipStartMs + root.clipSpanMs : root.startMs + root.spanMs

    function xForTime(ms) { return (ms - root.startMs) * root.pxPerMs }
    function timeForX(x) { return root.startMs + x / Math.max(0.000001, root.pxPerMs) }

    // One frame of the source, used to place the sub-second ticks. Fed by the
    // panel from the clip's own rate; 40 ms is only the fallback for a clip
    // whose rate is not known yet.
    property real frameMs: 40

    // The step ladder is derived, not typed out. A fixed table is exactly what
    // broke this ruler: the old one stopped at one hour, and on the multi-hour
    // sources this project is cut from an hourly tick lands every 27 px while a
    // four-field timecode needs 68, so every label overlapped its neighbours
    // into a solid bar. Deriving it means each unit contributes its own natural
    // divisions - a clock reads 15 s and 30 s as round and 10 min as round, so
    // the ladder steps by twelfths and quarters of a unit rather than by tenths
    // of a millisecond count - and it costs nothing to extend.
    readonly property var stepTable: root.stepCandidates()

    // Multiples per unit, coarsest unit last. Sub-second steps are whole frames
    // instead of round millisecond counts, because field 4 of the label is a
    // frame number and a tick halfway between two frames cannot be labelled
    // honestly.
    function stepCandidates() {
        var out = []
        var frame = Math.max(1, root.frameMs)
        var frameMults = [1, 2, 5, 10, 25]
        for (var f = 0; f < frameMults.length; ++f) {
            if (frame * frameMults[f] < 1000)
                out.push(frame * frameMults[f])
        }
        var units = [1000, 60000, 3600000, 86400000]
        var mults = [[1, 2, 5, 10, 15, 30], [1, 2, 5, 10, 15, 30],
                     [1, 2, 3, 6, 12], [1, 2, 5, 10, 30]]
        for (var u = 0; u < units.length; ++u) {
            for (var m = 0; m < mults[u].length; ++m)
                out.push(units[u] * mults[u][m])
        }
        return out
    }

    // Label detail follows the step, as Premiere's rulers do: a ruler stepping
    // in whole minutes has nothing to say in its frames field, and dropping the
    // two fields that are always "00" is what buys back the room the labels
    // needed. 4 = HH:MM:SS:FF, 3 = HH:MM:SS, 2 = HH:MM.
    function fieldsFor(step) {
        if (step < 1000)
            return 4
        if (step < 60000)
            return 3
        return 2
    }

    function labelWidthFor(fields) {
        return fields === 4 ? wide.width : (fields === 3 ? medium.width
                                                         : narrow.width)
    }

    // Does a step leave room for its own label? Measured rather than assumed:
    // the previous 82 px constant was a guess at one font size and one format,
    // and it was wrong for both once the format started varying.
    function stepFits(step) {
        return step * root.pxPerMs
                >= root.labelWidthFor(root.fieldsFor(step)) + 14
    }

    // Smallest derived step whose label fits. Past the coarsest entry the walk
    // keeps doubling instead of giving up, so the ruler cannot be handed a span
    // longer than its ladder - the failure the fixed table had at one hour.
    function niceStep() {
        var table = root.stepTable
        for (var i = 0; i < table.length; ++i) {
            if (root.stepFits(table[i]))
                return table[i]
        }
        var step = table[table.length - 1]
        for (var n = 0; n < 48 && !root.stepFits(step); ++n)
            step *= 2
        return step
    }

    TextMetrics {
        id: wide
        font.family: Theme.monoFont
        font.pixelSize: Theme.fsXs
        text: "00:00:00:00"
    }
    TextMetrics {
        id: medium
        font.family: Theme.monoFont
        font.pixelSize: Theme.fsXs
        text: "00:00:00"
    }
    TextMetrics {
        id: narrow
        font.family: Theme.monoFont
        font.pixelSize: Theme.fsXs
        text: "00:00"
    }

    function timecode(milliseconds, fields) {
        var ms = Math.max(0, milliseconds)
        var total = Math.floor(ms / 1000)
        function pad(value) { return value < 10 ? "0" + value : String(value) }
        var out = pad(Math.floor(total / 3600)) + ":"
                + pad(Math.floor(total / 60) % 60)
        if (fields >= 3)
            out += ":" + pad(total % 60)
        if (fields >= 4)
            out += ":" + pad(Math.floor((ms % 1000) / 40))
        return out
    }

    Repeater {
        // Guarded rather than trusted. niceStep() should keep this near
        // width/82, but every term in tickCount comes from a duration and a zoom
        // level, and a zero width or a degenerate span makes that reasoning
        // false without changing the code. 4096 is ~300x more labels than a
        // ruler this wide can show, so a clamp here is a bug report, not a
        // cosmetic limit.
        model: ModelGuard.bound(root.tickCount, 4096, "effectRuler.major")
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
                text: root.timecode(root.firstTick + parent.index * root.step,
                                    root.stepFields)
                color: Theme.textMuted
                font.family: Theme.monoFont
                font.pixelSize: Theme.fsXs
            }
        }
    }

    // Minor ticks — four subdivisions between labels, as in Premiere.
    Repeater {
        model: ModelGuard.bound(root.tickCount * 4, 16384, "effectRuler.minor")
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
        AppCursor.name: "ResizeHorizontal"
        onPressed: (mouse) => root.scrubTo(mouse.x)
        onPositionChanged: (mouse) => {
            if (pressed)
                root.scrubTo(mouse.x)
        }

        // Ctrl or Alt zooms about the pointer, the way Alt+wheel does in
        // Premiere's timeline; a plain wheel pans. Both are reported upwards
        // rather than applied here - the window belongs to the panel, which has
        // to keep the ruler and the lanes on the same span.
        onWheel: (wheel) => {
            var ticks = (wheel.angleDelta.y !== 0 ? wheel.angleDelta.y
                                                  : wheel.angleDelta.x) / 120
            if (ticks === 0)
                return
            if (wheel.modifiers & (Qt.ControlModifier | Qt.AltModifier))
                root.zoomRequested(Math.pow(0.8, ticks), root.timeForX(wheel.x))
            else
                root.panRequested(-ticks * root.spanMs * 0.15)
        }
    }

    // Scrubbing is clamped to the clip, not to the window: dragging to the edge
    // of a zoomed-in view should not be able to park the playhead outside the
    // clip being edited, and the panel scrolls the window to follow instead.
    function scrubTo(x) {
        var target = root.timeForX(x)
        Backend.playheadMs = Math.round(
                    Math.max(Math.max(0, root.limitStartMs),
                             Math.min(root.limitEndMs, target)))
    }
}
