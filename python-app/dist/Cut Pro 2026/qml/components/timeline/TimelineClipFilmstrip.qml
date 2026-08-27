pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
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

    readonly property real cellHeight: Math.max(1, root.height)
    readonly property real cellWidth: Math.max(2, cellHeight * root.frameAspect)
    readonly property int totalSlots: Math.ceil(root.width / cellWidth)
    readonly property int firstSlot: Math.max(0, Math.floor(root.viewLeft / cellWidth))
    readonly property int lastSlot: Math.min(
                                       totalSlots - 1,
                                       Math.floor((root.viewRight - 0.001) / cellWidth))
    readonly property int slotCount: root.sourceUrl === ""
                                     || root.viewRight <= root.viewLeft
                                     ? 0 : Math.max(0, lastSlot - firstSlot + 1)

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
