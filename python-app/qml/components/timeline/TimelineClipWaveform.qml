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
// range is split into aligned windows and each one is decoded for just that span.
// They fade in over the sheet, so there is never a frame with nothing under the
// clip.
//
// Like the filmstrip, this layer declares what it wants and draws what has
// arrived - see the prefetch section below.
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
    // Identifies this waveform's prefetch wish. One per clip; empty leaves the
    // layer showing only what the peak cache already holds.
    property string requesterKey: ""

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
    // Columns the provider renders into one window. Part of the cache key, so it
    // is a constant rather than something derived from the drawn size.
    readonly property int windowColumns: 256
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
    // The integer the URL, the wish and the readiness check all use. Three places
    // rounding independently is three chances to disagree about which window is
    // meant, and a disagreement here reads as a window that never arrives.
    readonly property int spanKey: Math.round(root.spanMs)
    // One sheet column covers this much audio. The sheet is the better artefact
    // while it is finer than what a window would give, and it costs nothing.
    readonly property real sheetColumnMs:
        root.mediaDurationMs > 0 ? root.mediaDurationMs / root.sheetWidth : 0
    // 256 columns over `spanMs`, against one sheet column per `sheetColumnMs`:
    // the windows only earn their decode once they are meaningfully finer.
    //
    // With no sheet the comparison has nothing to weigh, and refusing to decode
    // would leave the clip flat: a normal-sized import skips detailed previews so
    // the bin stays instant, which means most sources reach the timeline with no
    // waveform PNG at all. Windows are then the only waveform there is, at any
    // zoom.
    //
    // No longer suspended for the length of a gesture. A window is only shown once
    // its peaks are in memory, so keeping this live through a drag costs a hash
    // lookup - and the waveform stops vanishing the moment the clip is touched.
    readonly property bool useWindows:
        root.waveToken !== "" && root.pixelsPerSecond > 0
        && (root.sourceUrl === ""
            || root.sheetColumnMs <= 0
            || root.spanMs / root.windowColumns < root.sheetColumnMs * 0.5)

    readonly property real windowStartMs:
        Math.floor(root.sourceMsAt(root.windowLeft) / root.spanMs) * root.spanMs
    readonly property int windowNeed: {
        if (!root.useWindows || root.windowWidth <= 0.5)
            return 0
        var endMs = root.sourceMsAt(root.windowRight)
        var count = Math.ceil((endMs - root.windowStartMs) / root.spanMs)
        // A pan never needs more than a screenful plus an edge window; the cap is
        // there so a degenerate zoom cannot ask for thousands of decodes.
        return Math.max(0, Math.min(count + 1, root.windowCeiling))
    }
    readonly property int windowCount: Math.min(root.windowNeed, root.windowCapacity)

    // Same reason as the filmstrip's slotCapacity: the Repeater's model must not
    // follow the window count. Every write to it destroys and re-incubates all of
    // the delegates on the GUI thread, and the count changes on every pan - the
    // autoscroll during a clip drag moves contentX every 16ms. So the capacity
    // only grows, and windows past windowCount are parked with no source.
    readonly property int windowCeiling: 24
    property int windowCapacity: 0

    function growWindows() {
        if (root.windowNeed <= 0)
            return
        const want = Math.min(root.windowCeiling,
                              Math.ceil((root.windowNeed + 1) / 8) * 8)
        if (want > root.windowCapacity)
            root.windowCapacity = want
    }
    onWindowNeedChanged: root.growWindows()
    Component.onCompleted: root.growWindows()

    function windowX(index) {
        var startMs = root.windowStartMs + index * root.spanMs
        return (startMs - root.sourceInMs) / 1000 * root.pixelsPerSecond
    }
    function windowStartFor(index) {
        return Math.max(0, Math.round(root.windowStartMs + index * root.spanMs))
    }
    function windowUrl(index) {
        if (!root.useWindows)
            return ""
        return "image://wave-window/" + root.waveToken + "/"
                + root.windowStartFor(index) + "/" + root.spanKey
                + "/" + root.windowColumns
    }

    // ---- Live decode progress -------------------------------------------
    //
    // Same arrangement as the filmstrip, and for the same reason. Peaks for a
    // window are a real decode of that span of audio; a screenful of them
    // demanded in one frame - which is what a drop or a zoom used to do - means
    // most of the requests lose, and a losing request completes with a transparent
    // pixel that an Image reports as Ready. That is why the waveform came up as
    // one short fragment and why the gaps returned after every zoom.
    //
    // Now the visible window starts go to the prefetcher, which decodes them left
    // to right, one at a time; each one that lands bumps timelinePreviewRevision;
    // and a window's Image is only pointed at peaks the backend already holds. The
    // waveform therefore grows across the clip rather than appearing in pieces.
    readonly property int previewRevision: Backend.timelinePreviewRevision

    function submitWaveWish() {
        if (root.requesterKey === "" || !root.useWindows || root.windowCount <= 0)
            return
        // The worker parks for the length of a gesture, so a wish written on every
        // autoscroll step would be replaced unread.
        if (Backend.timelineInteractionActive)
            return
        var starts = []
        for (var i = 0; i < root.windowCount; ++i)
            starts.push(root.windowStartFor(i))
        if (starts.length > 0)
            Backend.requestWaveformWindows(root.requesterKey, root.waveToken,
                                           starts, root.spanKey,
                                           root.windowColumns)
    }

    readonly property string waveWishKey:
        root.waveToken + "@" + root.spanKey
        + "#" + Math.round(root.windowStartMs) + "+" + root.windowCount
    onWaveWishKeyChanged: waveWishTimer.restart()

    Timer {
        id: waveWishTimer
        interval: 80
        repeat: false
        onTriggered: root.submitWaveWish()
    }

    // Heartbeat. A re-issued identical wish keeps its place, and a spent one is
    // started again - which is how a window that was refused while something else
    // held the decode slots is picked up without tracking it here.
    Timer {
        interval: 700
        repeat: true
        running: root.useWindows && root.requesterKey !== ""
                 && root.windowCount > 0
        onTriggered: root.submitWaveWish()
    }

    Component.onDestruction: {
        if (root.requesterKey !== "")
            Backend.cancelTimelinePreviewRequest(root.requesterKey)
    }

    // A delegate reused for a different clip gets a new key; drop the wish filed
    // under the old one rather than leaving it for the cap to evict.
    property string lastRequesterKey: ""
    onRequesterKeyChanged: {
        if (root.lastRequesterKey !== "")
            Backend.cancelTimelinePreviewRequest(root.lastRequesterKey)
        root.lastRequesterKey = root.requesterKey
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
        model: ModelGuard.bound(root.windowCapacity, root.windowCeiling,
                                "waveform.windows")
        delegate: Item {
            id: waveWindow
            required property int index
            readonly property bool live: index < root.windowCount

            visible: waveWindow.live
            x: root.windowX(waveWindow.index)
            y: 0
            width: root.spanMs / 1000 * root.pixelsPerSecond
            height: root.height

            Image {
                id: peaks
                readonly property real wantStartMs:
                    waveWindow.live ? root.windowStartFor(waveWindow.index) : -1
                readonly property string wantUrl:
                    waveWindow.live ? root.windowUrl(waveWindow.index) : ""
                // Whether those peaks are in memory. The revision is read first so
                // it is captured as a dependency even when the rest short-circuits:
                // a window that lands a moment from now has to re-evaluate this.
                readonly property bool ready: {
                    const revision = root.previewRevision
                    if (revision < 0 || peaks.wantUrl === "")
                        return false
                    return Backend.waveformWindowReady(root.waveToken,
                                                       peaks.wantStartMs,
                                                       root.spanKey,
                                                       root.windowColumns)
                }
                // Held over a zoom for the same reason as a filmstrip tile: the new
                // span's peaks do not exist yet, and blanking the window while they
                // are decoded is exactly the flicker this layer is meant to lose.
                // Slightly the wrong resolution for a moment beats nothing at all.
                property string heldUrl: ""
                onReadyChanged: {
                    if (peaks.ready)
                        peaks.heldUrl = peaks.wantUrl
                }

                anchors.fill: parent
                source: peaks.ready ? peaks.wantUrl : peaks.heldUrl
                // The provider renders `windowColumns` columns into this width;
                // asking for more pixels than the window is drawn at only costs
                // upload.
                sourceSize.width: Math.max(64, Math.min(1024,
                                                        Math.round(parent.width)))
                sourceSize.height: Math.max(16, Math.round(root.height))
                fillMode: Image.Stretch
                asynchronous: true
                // Same reason as the filmstrip tiles: the backend owns the cache
                // and can invalidate it, and Qt's pixmap cache has no way to drop
                // an entry - so a second cache in front of it can only pin stale
                // contents.
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
