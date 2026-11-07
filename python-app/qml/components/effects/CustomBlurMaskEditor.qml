import QtQuick
import CutPro 1.0
import "../../theme"
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

// The bounding box for one custom blur instance: drag inside it to move, grab
// an edge or a corner to resize, hold Shift to keep the ratio, drag on bare
// picture to draw a new one.
//
// A drag works in this item's own pixels (`draft*`) and commits fractions of
// the frame on release. Those fractions are what CustomBlurRegion paints and
// what the export's crop reads, so this item has to be anchored to the picture
// rectangle - anchored to the whole stage, every letterboxed sequence puts the
// box somewhere the blur is not.
Item {
    id: root

    property bool active: false
    property real maskX: 0.30
    property real maskY: 0.35
    property real maskWidth: 0.40
    property real maskHeight: 0.30
    property real draftX: 0
    property real draftY: 0
    property real draftWidth: 0
    property real draftHeight: 0
    property bool dragging: false
    signal maskCommitted(real x, real y, real width, real height)

    visible: active

    // Backend::setCustomBlurMask clamps what it is handed: x and y to 0.98,
    // width and height to 0.02 at least, and the width down to 1 - x behind
    // it. Holding the draft to the same floor is what keeps the box still when
    // it is let go - a box pushed against the right edge came back as a 2%
    // sliver, because x was clamped to 0.98 first and the width was then
    // truncated to what was left of the frame.
    readonly property real minBoxWidth: Math.max(16, 0.02 * width)
    readonly property real minBoxHeight: Math.max(16, 0.02 * height)

    // Corners plus edge midpoints, built out of constants so the Repeater
    // creates its delegates once. The old model was an array literal that read
    // parent.width and parent.height, which is a binding: every pixel of a
    // resize threw all four handles away and built new ones, and they flickered
    // for the length of the drag.
    readonly property var handleSpots: [
        { "fx": 0.0, "fy": 0.0 }, { "fx": 0.5, "fy": 0.0 },
        { "fx": 1.0, "fy": 0.0 }, { "fx": 0.0, "fy": 0.5 },
        { "fx": 1.0, "fy": 0.5 }, { "fx": 0.0, "fy": 1.0 },
        { "fx": 0.5, "fy": 1.0 }, { "fx": 1.0, "fy": 1.0 }
    ]

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    // The one place the draft is written, so every gesture obeys the same
    // limits: never smaller than the minimum, never outside the frame, and
    // always on whole pixels - a 2px border and 9px handles sitting on half
    // pixels shimmer against the picture for as long as the drag lasts.
    function setDraft(x, y, w, h) {
        w = clamp(w, minBoxWidth, Math.max(minBoxWidth, width))
        h = clamp(h, minBoxHeight, Math.max(minBoxHeight, height))
        draftWidth = Math.round(w)
        draftHeight = Math.round(h)
        draftX = clamp(Math.round(x), 0, Math.max(0, width - draftWidth))
        draftY = clamp(Math.round(y), 0, Math.max(0, height - draftHeight))
    }

    // The draft follows the model, but only when the two actually disagree. A
    // commit is fractions of the frame out and pixels back, so the echo of one we
    // just made lands a fraction of a pixel from where the box already is -
    // and rewriting the draft from a value it already holds is what let a late
    // echo (one arriving on the next width change rather than on the commit)
    // pull the box back to an earlier size on its own.
    function syncFromMask() {
        if (dragging || width <= 0 || height <= 0)
            return
        var wantX = maskX * width
        var wantY = maskY * height
        var wantW = maskWidth * width
        var wantH = maskHeight * height
        if (Math.abs(wantX - draftX) <= 1 && Math.abs(wantY - draftY) <= 1
                && Math.abs(wantW - draftWidth) <= 1
                && Math.abs(wantH - draftHeight) <= 1)
            return
        setDraft(wantX, wantY, wantW, wantH)
    }

    function commitDraft() {
        if (width <= 0 || height <= 0)
            return
        maskCommitted(draftX / width, draftY / height,
                      draftWidth / width, draftHeight / height)
    }

    onMaskXChanged: syncFromMask()
    onMaskYChanged: syncFromMask()
    onMaskWidthChanged: syncFromMask()
    onMaskHeightChanged: syncFromMask()
    onWidthChanged: syncFromMask()
    onHeightChanged: syncFromMask()
    onActiveChanged: if (active) syncFromMask()
    Component.onCompleted: syncFromMask()

    Rectangle {
        id: box
        x: root.draftX
        y: root.draftY
        width: root.draftWidth
        height: root.draftHeight
        color: "transparent"
        border.width: 2
        border.color: Theme.accent

        Repeater {
            model: root.handleSpots
            delegate: Rectangle {
                required property var modelData
                readonly property bool midpoint: modelData.fx === 0.5
                                                 || modelData.fy === 0.5
                width: 9
                height: 9
                x: Math.round(box.width * modelData.fx - width / 2)
                y: Math.round(box.height * modelData.fy - height / 2)
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.accent
                // The midpoints are the affordance for the edge grabs the hint
                // bar promises. On a small box they would sit on top of the
                // corners, so they wait until there is room for them.
                visible: !midpoint || (box.width >= 44 && box.height >= 44)
            }
        }
    }

    MouseArea {
        id: pointer
        objectName: "customBlurPointer"
        anchors.fill: parent
        enabled: root.active
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        preventStealing: true

        // Grab band, sized from the box: wide enough on a normal box that the
        // edges are easy to hit, narrowing on a small one so its middle is still
        // there to drag.
        readonly property real grab:
            Math.max(7, Math.min(14, Math.min(root.draftWidth,
                                              root.draftHeight) / 3))

        property string editMode: "move"
        property bool drawing: false
        property real pressX: 0
        property real pressY: 0
        property real pressLeft: 0
        property real pressTop: 0
        property real pressRight: 0
        property real pressBottom: 0
        property real pressAspect: 1

        function hitMode(px, py) {
            var left = root.draftX
            var right = root.draftX + root.draftWidth
            var top = root.draftY
            var bottom = root.draftY + root.draftHeight
            var nearLeft = Math.abs(px - left) <= grab
            var nearRight = Math.abs(px - right) <= grab
            var nearTop = Math.abs(py - top) <= grab
            var nearBottom = Math.abs(py - bottom) <= grab
            // The band runs a little past each end, so the pointer does not fall
            // into a dead spot between a corner and the edge next to it.
            var inRows = py >= top - grab && py <= bottom + grab
            var inCols = px >= left - grab && px <= right + grab
            if (nearLeft && nearTop) return "topLeft"
            if (nearRight && nearTop) return "topRight"
            if (nearLeft && nearBottom) return "bottomLeft"
            if (nearRight && nearBottom) return "bottomRight"
            if (nearLeft && inRows) return "left"
            if (nearRight && inRows) return "right"
            if (nearTop && inCols) return "top"
            if (nearBottom && inCols) return "bottom"
            if (px >= left && px <= right && py >= top && py <= bottom)
                return "move"
            return "draw"
        }

        function cursorFor(mode) {
            if (mode === "move") return "CrossArrow"
            if (mode === "left" || mode === "right") return "ScaleHorizontal"
            if (mode === "top" || mode === "bottom") return "ScaleVertical"
            if (mode === "topLeft" || mode === "bottomRight") return "ScaleTLBR"
            if (mode === "topRight" || mode === "bottomLeft") return "ScaleTRBL"
            return "Precise"
        }

        // While pressed the cursor is whatever the drag is doing; otherwise it
        // answers to where the pointer actually is, and nothing at all when the
        // pointer is elsewhere, so the app default shows through.
        AppCursor.name: pressed ? cursorFor(editMode)
                                : containsMouse
                                  ? cursorFor(hitMode(mouseX, mouseY)) : ""

        function applyMove(px, py) {
            // Size comes from the press, not from the live draft, so a box slid
            // into a frame edge stops there instead of being shaved down by the
            // clamp on the way.
            root.setDraft(pressLeft + (px - pressX), pressTop + (py - pressY),
                          pressRight - pressLeft, pressBottom - pressTop)
        }

        function applyDraw(px, py, keepAspect) {
            var toRight = px >= pressX
            var toBottom = py >= pressY
            var w = Math.abs(px - pressX)
            var h = Math.abs(py - pressY)
            if (keepAspect) {
                // Square, and only as large as the room left on the side the
                // drag is heading for.
                var side = Math.min(Math.max(w, h),
                                    toRight ? root.width - pressX : pressX,
                                    toBottom ? root.height - pressY : pressY)
                w = side
                h = side
            }
            root.setDraft(toRight ? pressX : pressX - w,
                          toBottom ? pressY : pressY - h, w, h)
        }

        function applyResize(px, py, keepAspect) {
            var wantLeft = editMode === "left" || editMode === "topLeft"
                           || editMode === "bottomLeft"
            var wantRight = editMode === "right" || editMode === "topRight"
                            || editMode === "bottomRight"
            var wantTop = editMode === "top" || editMode === "topLeft"
                          || editMode === "topRight"
            var wantBottom = editMode === "bottom" || editMode === "bottomLeft"
                             || editMode === "bottomRight"
            // Every edge is measured from where it was at press, so a resize
            // cannot accumulate its own rounding as the pointer moves.
            var left = pressLeft
            var right = pressRight
            var top = pressTop
            var bottom = pressBottom

            if (!keepAspect) {
                // Each grabbed edge moves alone and stops a minimum short of the
                // one opposite, which stays where it is.
                if (wantLeft)
                    left = root.clamp(px, 0, right - root.minBoxWidth)
                if (wantRight)
                    right = root.clamp(px, left + root.minBoxWidth, root.width)
                if (wantTop)
                    top = root.clamp(py, 0, bottom - root.minBoxHeight)
                if (wantBottom)
                    bottom = root.clamp(py, top + root.minBoxHeight, root.height)
                root.setDraft(left, top, right - left, bottom - top)
                return
            }

            // Shift keeps the ratio the box had when the drag started. A corner
            // pivots on the corner opposite and follows whichever axis the
            // pointer pulled further, so the box tracks the cursor instead of
            // snapping between the two answers. An edge drives its own axis and
            // the other one grows about the box's centre line.
            var aspect = pressAspect > 0 ? pressAspect : 1
            var anchorX = wantLeft ? right : left
            var anchorY = wantTop ? bottom : top
            var w = 0
            var h = 0
            if ((wantLeft || wantRight) && (wantTop || wantBottom)) {
                w = Math.abs(px - anchorX)
                h = Math.abs(py - anchorY)
                if (w >= h * aspect)
                    h = w / aspect
                else
                    w = h * aspect
            } else if (wantLeft || wantRight) {
                w = Math.abs(px - anchorX)
                h = w / aspect
            } else {
                h = Math.abs(py - anchorY)
                w = h * aspect
            }
            // Fit the size to the frame with the ratio still intact, before it
            // is placed. setDraft clamps each axis on its own, so handing it an
            // oversized box would let it squash one side and break the ratio the
            // modifier is there to hold.
            w = root.clamp(w, root.minBoxWidth, root.width)
            h = w / aspect
            if (h > root.height) { h = root.height; w = h * aspect }
            if (h < root.minBoxHeight) { h = root.minBoxHeight; w = h * aspect }
            if (w > root.width) { w = root.width; h = w / aspect }

            // A grabbed side sits on the anchor; the free axis of an edge drag
            // opens either way from the centre line it had at press.
            var x = wantLeft ? anchorX - w
                             : (wantRight ? anchorX
                                          : (pressLeft + pressRight) / 2 - w / 2)
            var y = wantTop ? anchorY - h
                            : (wantBottom ? anchorY
                                          : (pressTop + pressBottom) / 2 - h / 2)
            root.setDraft(x, y, w, h)
        }

        onPressed: mouse => {
            // The draft is deliberately not re-read from the model here. It is
            // already where the model put it, and pulling it in again at press
            // meant any staleness in the model - Backend.clipById() is not a
            // tracked read, so the editor's copy could sit one commit behind -
            // snapped the box back to an older size the instant a handle was
            // grabbed, which read as the box resizing itself.
            editMode = hitMode(mouse.x, mouse.y)
            drawing = false
            pressX = mouse.x
            pressY = mouse.y
            pressLeft = root.draftX
            pressTop = root.draftY
            pressRight = root.draftX + root.draftWidth
            pressBottom = root.draftY + root.draftHeight
            pressAspect = root.draftHeight > 0
                          ? root.draftWidth / root.draftHeight : 1
            root.dragging = true
            // Nothing is written here. This used to collapse the box to a
            // zero-size rect at the click point whenever the press landed
            // outside the grab band, so a slightly-missed corner threw the mask
            // away and the release invented a 96x64 box under the pointer.
        }

        onPositionChanged: mouse => {
            if (!pressed)
                return
            var px = root.clamp(mouse.x, 0, root.width)
            var py = root.clamp(mouse.y, 0, root.height)
            var keepAspect = (mouse.modifiers & Qt.ShiftModifier) !== 0
            if (editMode === "draw") {
                // A press on bare picture only starts a new box once the pointer
                // has travelled. Under that it is a misplaced click, and the mask
                // it missed stays where it is.
                if (!drawing && Math.abs(px - pressX) < 4
                        && Math.abs(py - pressY) < 4)
                    return
                drawing = true
                applyDraw(px, py, keepAspect)
            } else if (editMode === "move") {
                applyMove(px, py)
            } else {
                applyResize(px, py, keepAspect)
            }
        }

        onReleased: {
            root.dragging = false
            // A press that moved nothing is not an edit: setCustomBlurMask calls
            // rememberState(), so committing here would push an empty step onto
            // the undo stack for every stray click on the picture.
            if (root.draftX === pressLeft && root.draftY === pressTop
                    && root.draftWidth === pressRight - pressLeft
                    && root.draftHeight === pressBottom - pressTop) {
                root.syncFromMask()
                return
            }
            root.commitDraft()
        }

        // Losing the grab - a popup opening, the window deactivating - used to
        // leave dragging true for the rest of the session, and syncFromMask()
        // early-returns while it is set, so the box stopped following the mask
        // for good.
        onCanceled: {
            root.dragging = false
            root.syncFromMask()
        }
    }
}
