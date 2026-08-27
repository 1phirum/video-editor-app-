pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"

// A clip's video thumbnails, drawn as discrete cells at their true aspect
// instead of one image stretched across the whole clip. The cached preview is a
// horizontal tile sheet (MediaPreviewGenerator::kFilmstrip*), so a single cell
// is shown by sliding the whole sheet behind a clipped slot.
//
// Only slots inside the timeline viewport are built: a 4-minute clip at maximum
// zoom is ~96000px wide, and a delegate per cell across all of it would cost
// hundreds of items to show the handful that are actually on screen.
Item {
    id: root

    required property string sourceUrl
    required property real pixelsPerSecond
    required property real sourceInMs
    required property real sourceDurationMs
    // Cell layout baked into the cached sheet, reported by probeMedia().
    required property int frameCount
    required property real frameAspect
    // Visible window, in this item's own coordinates.
    required property real viewLeft
    required property real viewRight
    // Handle for the backend's on-demand thumbnails, from
    // Backend.timelineTileToken(). When set, each visible slot asks for the frame
    // at its own position instead of taking the nearest cell of the sheet - which
    // is the difference between a zoomed-in eight hour clip showing the same tile
    // for ten minutes and showing what is actually there.
    property string tileToken: ""
    property bool preferTiles: false

    readonly property real cellHeight: Math.max(1, root.height)
    readonly property real cellWidth: Math.max(2, cellHeight * root.frameAspect)
    readonly property int totalSlots: Math.ceil(root.width / cellWidth)
    readonly property int firstSlot: Math.max(0, Math.floor(root.viewLeft / cellWidth))
    readonly property int lastSlot: Math.min(
                                       totalSlots - 1,
                                       Math.floor((root.viewRight - 0.001) / cellWidth))
    readonly property int slotCount: (root.sourceUrl === "" && !root.useTiles)
                                     || root.viewRight <= root.viewLeft
                                     ? 0 : Math.max(0, lastSlot - firstSlot + 1)

    // Source time covered by one drawn cell.
    readonly property real slotSpanMs:
        root.cellWidth / Math.max(root.pixelsPerSecond, 0.001) * 1000
    // Source time covered by one cell of the cached sheet.
    readonly property real sheetSpanMs:
        root.frameCount > 0 && root.sourceDurationMs > 0
        ? root.sourceDurationMs / root.frameCount : 0
    // The sheet is used while it is at least as detailed as the current zoom.
    // Past that point it starts repeating a tile across several cells, and the
    // on-demand thumbnails are what a frame-accurate strip needs.
    //
    // Suspended outright for the length of a drag, trim or drop. A tile on an
    // 8 hour AV1 source costs a seek and a decode, and asking for a screenful of
    // them while the pointer is moving is what left the window unable to repaint
    // and showing "Not Responding". The cached sheet below stays visible, so the
    // strip does not go blank - it just stops asking for anything new until the
    // gesture is over.
    readonly property bool useTiles:
        root.preferTiles && root.tileToken !== ""
        && !Backend.timelineInteractionActive
        && (root.sheetSpanMs <= 0 || root.slotSpanMs * 1.25 < root.sheetSpanMs)
    // Requests are snapped to a grid one cell wide, so panning reuses URLs
    // instead of asking for a slightly different position on every pixel of
    // scroll. The backend snaps again, to the keyframe it decodes from.
    readonly property real tileGridMs: Math.max(200, Math.round(root.slotSpanMs))

    function tileUrlForSlot(slot) {
        if (!root.useTiles)
            return ""
        var timeMs = root.sourceInMs + (slot + 0.5) * root.slotSpanMs
        var bucket = Math.max(0, Math.round(timeMs / root.tileGridMs)
                                 * root.tileGridMs)
        return "image://timeline-tile/" + root.tileToken + "/" + bucket
    }

    // Source time at a slot's midpoint -> index of the cell that covers it.
    // filmstrip() samples at frameCount/duration fps, so cell j owns
    // [j * duration / frameCount, (j + 1) * duration / frameCount).
    function cellForSlot(slot) {
        if (root.frameCount <= 1 || root.sourceDurationMs <= 0)
            return 0
        var timeMs = root.sourceInMs
                + (slot + 0.5) * root.cellWidth
                  / Math.max(root.pixelsPerSecond, 0.001) * 1000
        var index = Math.floor(timeMs / root.sourceDurationMs * root.frameCount)
        return Math.max(0, Math.min(root.frameCount - 1, index))
    }

    clip: true

    Repeater {
        model: root.slotCount

        delegate: Item {
            id: slot
            required property int index
            readonly property int slotIndex: root.firstSlot + index

            x: slotIndex * root.cellWidth
            width: root.cellWidth
            height: root.cellHeight
            clip: true

            Image {
                x: -root.cellForSlot(slot.slotIndex) * root.cellWidth
                width: root.cellWidth * Math.max(1, root.frameCount)
                height: root.cellHeight
                source: root.sourceUrl
                // The sheet scales uniformly: a slot's aspect matches a cell's,
                // so Stretch here does not distort the frame.
                fillMode: Image.Stretch
                asynchronous: true
                cache: true
                smooth: true
            }

            // The frame this slot actually covers, fetched from the backend and
            // faded in over the sheet cell. The sheet is the placeholder: it is
            // already in memory, so a slot is never empty while the exact
            // thumbnail is being decoded.
            Image {
                anchors.fill: parent
                visible: root.useTiles
                source: root.useTiles ? root.tileUrlForSlot(slot.slotIndex) : ""
                sourceSize.height: Math.round(root.cellHeight)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                // Not cached by Qt, on purpose. A tile request that is refused -
                // by the decode governor during a gesture, or by the failure
                // registry's cool-off - completes with a transparent pixel so it
                // does not spam warnings. Qt's pixmap cache cannot tell that
                // apart from a real frame, and it is keyed on the URL with no way
                // to invalidate an entry, so caching it here would pin every
                // skipped tile blank for the rest of the session. The backend
                // already caches: TimelineTileCache holds the decoded frames and
                // PreviewFailureRegistry decides what is worth retrying, and both
                // can be invalidated. This just stops a second, dumber cache from
                // sitting in front of them.
                cache: false
                smooth: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: 90; easing.type: Easing.OutQuad }
                }
            }

            // Cell divider, so adjacent thumbnails read as separate frames.
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Qt.rgba(0, 0, 0, 0.28)
                visible: slot.slotIndex < root.totalSlots - 1
            }
        }
    }
}
