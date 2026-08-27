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

// A clip's audio waveform, in two layers.
//
// The bottom layer is the cached whole-file sheet. It is instant - it is one PNG
// already on disk - and it is correct as long as the clip is drawn at roughly the
// resolution the sheet was built at. The portion belonging to this clip is
// selected with sourceClipRect rather than by stretching the full image across
// the clip and letting the parent clip it: a 4-minute clip at 400 px/s is
// ~96000 px wide, far past the maximum GPU texture size, and an Image asked to
// cover that silently fails to render.
//
// The top layer is what CapCut has and a stretched sheet cannot give: windows
// decoded for the span actually on screen. The sheet is 1600 columns over the
// whole file, so on an eight hour source one column covers 18 seconds - a screen
// showing 20 seconds of audio gets a single column smeared across it, which
// reads as a flat block. Once a window is finer than a sheet column, the visible
// range is split into aligned windows and each is fetched from the wave-window
// provider, which decodes just that span. They fade in over the sheet, so there
// is never a frame with nothing under the clip.
Item {
    id: root

    required property string sourceUrl
    required property real pixelsPerSecond
    required property real sourceInMs
    required property real mediaDurationMs
    // Visible window, in this item's own coordinates.
    required property real viewLeft
    required property real viewRight

    // Handle for the on-demand provider. Empty disables the window layer, which
    // is what a build without FFmpeg linkage - or a clip with no audio - gets.
    property string waveToken: ""

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

    // ---- Window layer ----------------------------------------------------

    // Drawn width of one window. Wide enough that a screenful is a handful of
    // requests, narrow enough that each is a short decode.
    readonly property real windowPixels: 512
    // Time covered by one window at the current zoom, before quantisation.
    readonly property real rawSpanMs:
        root.windowPixels / Math.max(root.pixelsPerSecond, 0.001) * 1000
    // Spans come from a fixed ladder rather than from the exact zoom, so panning
    // and small zoom changes keep asking for the same windows and keep hitting
    // the peak cache. Without this every pixel of zoom would be a new cache key.
    readonly property var spanLadder: [250, 500, 1000, 2000, 5000, 10000, 20000,
                                       40000, 80000, 160000, 320000, 640000,
                                       1280000, 2560000]
    readonly property real spanMs: {
        for (var i = 0; i < root.spanLadder.length; ++i) {
            if (root.spanLadder[i] >= root.rawSpanMs)
                return root.spanLadder[i]
        }
        return root.spanLadder[root.spanLadder.length - 1]
    }
    // One sheet column covers this much audio. The sheet is the better artefact
    // while it is finer than what a window would give, and it costs nothing.
    readonly property real sheetColumnMs:
        root.mediaDurationMs > 0 ? root.mediaDurationMs / root.sheetWidth : 0
    // 256 columns over `spanMs`, against one sheet column per `sheetColumnMs`:
    // the windows only earn their decode once they are meaningfully finer.
    readonly property bool useWindows:
        root.waveToken !== "" && root.pixelsPerSecond > 0
        && !Backend.timelineInteractionActive
        && (root.sheetColumnMs <= 0
            || root.spanMs / 256 < root.sheetColumnMs * 0.5)

    readonly property real windowStartMs:
        Math.floor(root.sourceMsAt(root.windowLeft) / root.spanMs) * root.spanMs
    readonly property int windowCount: {
        if (!root.useWindows || root.windowWidth <= 0.5)
            return 0
        var endMs = root.sourceMsAt(root.windowRight)
        var count = Math.ceil((endMs - root.windowStartMs) / root.spanMs)
        // A pan never needs more than a screenful plus an edge window; the cap is
        // there so a degenerate zoom cannot ask for thousands of decodes.
        return Math.max(0, Math.min(count + 1, 24))
    }

    function windowX(index) {
        var startMs = root.windowStartMs + index * root.spanMs
        return (startMs - root.sourceInMs) / 1000 * root.pixelsPerSecond
    }
    function windowUrl(index) {
        if (!root.useWindows)
            return ""
        var startMs = Math.max(0, Math.round(root.windowStartMs
                                             + index * root.spanMs))
        return "image://wave-window/" + root.waveToken + "/" + startMs
                + "/" + Math.round(root.spanMs) + "/256"
    }

    clip: true
    visible: (root.sourceUrl !== "" || root.useWindows)
             && root.windowWidth > 0.5

    // Sheet: instant, and the placeholder the windows fade in over.
    Image {
        x: root.windowLeft
        width: root.windowWidth
        height: root.height
        visible: root.sourceUrl !== ""
                 && root.sheetRight - root.sheetLeft > 0.01
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

    Repeater {
        model: root.windowCount
        delegate: Item {
            id: waveWindow
            required property int index
            x: root.windowX(waveWindow.index)
            y: 0
            width: root.spanMs / 1000 * root.pixelsPerSecond
            height: root.height

            Image {
                anchors.fill: parent
                source: root.windowUrl(waveWindow.index)
                // The provider renders 256 columns into this width; asking for
                // more pixels than the window is drawn at only costs upload.
                sourceSize.width: Math.max(64, Math.min(1024,
                                                        Math.round(parent.width)))
                sourceSize.height: Math.max(16, Math.round(root.height))
                fillMode: Image.Stretch
                asynchronous: true
                // Same reason as the filmstrip tiles: a window refused during a
                // gesture completes with a transparent pixel, and Qt's pixmap
                // cache would remember that as the window's contents forever.
                cache: false
                smooth: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: 90; easing.type: Easing.OutQuad }
                }
            }
        }
    }
}
