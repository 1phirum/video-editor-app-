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

// A clip's audio waveform. The cached PNG covers the whole media file, so the
// portion belonging to this clip is selected with sourceClipRect rather than by
// stretching the full image across the clip and letting the parent clip it.
//
// That matters at high zoom: a 4-minute clip at 400 px/s is ~96000 px wide, far
// past the maximum GPU texture size, and an Image asked to cover it silently
// fails to render. Sampling only the visible window keeps the texture small no
// matter how far the timeline is zoomed in.
Item {
    id: root

    required property string sourceUrl
    required property real pixelsPerSecond
    required property real sourceInMs
    required property real mediaDurationMs
    // Visible window, in this item's own coordinates.
    required property real viewLeft
    required property real viewRight

    // Pixel size of the cached waveform sheet (MediaPreviewGenerator::waveform).
    readonly property int sheetWidth: 1600
    readonly property int sheetHeight: 160

    readonly property real windowLeft: Math.max(0, root.viewLeft)
    readonly property real windowRight: Math.min(root.width, root.viewRight)
    readonly property real windowWidth: Math.max(0, windowRight - windowLeft)

    function sourceMsAt(localX) {
        return root.sourceInMs
                + localX / Math.max(root.pixelsPerSecond, 0.001) * 1000
    }

    function sheetXFor(localX) {
        if (root.mediaDurationMs <= 0)
            return 0
        var fraction = root.sourceMsAt(localX) / root.mediaDurationMs
        return Math.max(0, Math.min(1, fraction)) * root.sheetWidth
    }

    readonly property real sheetLeft: root.sheetXFor(root.windowLeft)
    readonly property real sheetRight: root.sheetXFor(root.windowRight)

    clip: true
    visible: root.sourceUrl !== "" && root.windowWidth > 0.5
             && root.sheetRight - root.sheetLeft > 0.01

    Image {
        x: root.windowLeft
        width: root.windowWidth
        height: root.height
        source: root.sourceUrl
        sourceClipRect: Qt.rect(root.sheetLeft, 0,
                                Math.max(1, root.sheetRight - root.sheetLeft),
                                root.sheetHeight)
        fillMode: Image.Stretch
        asynchronous: true
        // Each pan lands on a different sub-rect, so a cache entry per window
        // would only ever be read once.
        cache: false
        smooth: true
    }
}
