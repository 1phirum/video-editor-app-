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
    // Backend.timelineTileToken(). When set, each visible slot shows the frame at
    // its own position instead of the nearest cell of the sheet - which is the
    // difference between a zoomed-in eight hour clip showing the same tile for ten
    // minutes and showing what is actually there.
    property string tileToken: ""
    property bool preferTiles: false
    // Identifies this strip's prefetch wish. One per clip; empty disables the
    // prefetcher, which leaves the strip showing whatever the cache already holds.
    property string requesterKey: ""

    readonly property real cellHeight: Math.max(1, root.height)
    // Floored at 6px: a clip collapsed to a couple of pixels tall would ask for a
    // slot every 2px, which is thousands of delegates across a wide clip.
    readonly property real cellWidth: Math.max(6, cellHeight * root.frameAspect)
    readonly property int totalSlots: Math.ceil(root.width / cellWidth)
    readonly property int firstSlot: Math.max(0, Math.floor(root.viewLeft / cellWidth))
    readonly property int lastSlot: Math.min(
                                       totalSlots - 1,
                                       Math.floor((root.viewRight - 0.001) / cellWidth))
    // Slots the visible slice wants right now. This is *not* the Repeater's model
    // - see slotCapacity.
    readonly property int slotNeed: (root.sourceUrl === "" && !root.useTiles)
                                    || root.viewRight <= root.viewLeft
                                    ? 0 : Math.max(0, lastSlot - firstSlot + 1)
    readonly property int slotCount: Math.min(root.slotNeed, root.slotCapacity)

    // Writing a Repeater's model destroys every delegate and builds them all
    // again, and QQuickRepeater::clear() forces each half-finished asynchronous
    // incubation to complete synchronously on the GUI thread. slotNeed changes
    // every time the view crosses a cell boundary - which, while a drag's
    // autoscroll timer moves contentX every 16ms, is several times a second, on
    // every clip at once. That storm is what leaves the window unable to finish a
    // frame and shows "Not Responding" after a drop, and it is worst right after
    // the cache is cleared because each rebuilt slot then queues a real decode
    // instead of hitting Qt's pixmap cache.
    //
    // So the model is a capacity that only ever grows, in steps, and slots past
    // slotCount are parked: hidden, holding no image source, costing one Item.
    // Panning and zooming then move existing slots instead of rebuilding them.
    readonly property int slotCeiling: 96
    property int slotCapacity: 0

    function growSlots() {
        if (root.slotNeed <= 0)
            return
        const want = Math.min(root.slotCeiling,
                              Math.ceil((root.slotNeed + 2) / 16) * 16)
        if (want > root.slotCapacity)
            root.slotCapacity = want
    }
    onSlotNeedChanged: root.growSlots()
    Component.onCompleted: root.growSlots()

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
    // Not suspended during a gesture any more. It used to be, because a slot
    // pointing at an uncached tile meant a seek and a decode, and a screenful of
    // those while the pointer moved is what left the window unable to repaint. A
    // slot now only ever points at a frame that is already in memory, so keeping
    // the strip live through a drag costs a hash lookup per cell - and the strip
    // no longer empties itself the moment the user touches it.
    readonly property bool useTiles:
        root.preferTiles && root.tileToken !== ""
        && (root.sheetSpanMs <= 0 || root.slotSpanMs * 1.25 < root.sheetSpanMs)
    // Requests are snapped to a grid one cell wide, so panning reuses URLs
    // instead of asking for a slightly different position on every pixel of
    // scroll. The backend snaps again, to the keyframe it decodes from.
    readonly property real tileGridMs: Math.max(200, Math.round(root.slotSpanMs))

    // ---- Live decode progress -------------------------------------------
    //
    // The strip does not fetch thumbnails. It declares which ones it wants and
    // then draws the ones that have arrived.
    //
    // Fetching them directly is what froze the window on a drop: a fresh drop has
    // no cached sheet yet - a long source is filmstripped by the deferred job,
    // which has not finished - so every visible slot wanted a tile immediately,
    // around thirty real seeks and decodes demanded in a single frame. The
    // requests that lost that race were completed with a transparent pixel, which
    // an Image cannot tell apart from a frame, so those cells stayed blank until
    // something unrelated invalidated them. Hence "a small chunk of images", and
    // hence the gaps coming back after every zoom.
    //
    // Now: one wish per clip goes to the prefetcher, which decodes the positions
    // in order, one at a time, round-robin between clips; each one that lands
    // bumps timelinePreviewRevision; a slot points its Image at a tile only once
    // the backend confirms it is in memory. Nothing the strip asks for can block,
    // so the strip cannot stall the window - and it visibly fills left to right
    // as the worker gets through the list, which is the CapCut behaviour.
    readonly property int previewRevision: Backend.timelinePreviewRevision

    function tileBucketForSlot(slot) {
        var timeMs = root.sourceInMs + (slot + 0.5) * root.slotSpanMs
        return Math.max(0, Math.round(timeMs / root.tileGridMs) * root.tileGridMs)
    }

    function tileUrlForSlot(slot) {
        if (!root.useTiles)
            return ""
        return "image://timeline-tile/" + root.tileToken + "/"
                + root.tileBucketForSlot(slot)
    }

    // The buckets the visible slice wants, in the order they should be decoded.
    // Built from the same expression the URLs use, so the position the worker
    // decodes and the position the slot later looks up are the same number.
    function submitTileWish() {
        if (root.requesterKey === "" || !root.useTiles || root.slotCount <= 0)
            return
        // Suppressed for the length of a gesture: the worker parks then anyway,
        // and a wish written on every autoscroll step would be thrown away
        // unread.
        if (Backend.timelineInteractionActive)
            return
        var buckets = []
        for (var i = 0; i < root.slotCount; ++i) {
            var slotIndex = root.firstSlot + i
            if (slotIndex >= root.totalSlots)
                break
            buckets.push(root.tileBucketForSlot(slotIndex))
        }
        if (buckets.length > 0)
            Backend.requestTimelineTiles(root.requesterKey, root.tileToken,
                                         buckets)
    }

    // What the wish is for. Anything that moves a slot or changes its position in
    // the source rewrites it.
    readonly property string tileWishKey:
        root.tileToken + "@" + Math.round(root.tileGridMs)
        + "#" + root.firstSlot + "+" + root.slotCount
        + "^" + Math.round(root.sourceInMs)
    onTileWishKeyChanged: wishTimer.restart()

    // Coalesces a pan into one wish. An autoscroll crosses a cell boundary
    // several times a second on every clip at once, and each wish allocates a
    // list.
    Timer {
        id: wishTimer
        interval: 80
        repeat: false
        onTriggered: root.submitTileWish()
    }

    // Heartbeat, and the only thing that guarantees the strip finishes. A wish
    // that is re-issued unchanged keeps its place in the queue, and once it is
    // spent the next re-issue starts a fresh sweep - so a bucket some gate
    // refused, or one whose source was busy at the time, gets another chance
    // without the strip having to track which ones are missing.
    Timer {
        interval: 700
        repeat: true
        running: root.useTiles && root.requesterKey !== "" && root.slotCount > 0
        onTriggered: root.submitTileWish()
    }

    Component.onDestruction: {
        if (root.requesterKey !== "")
            Backend.cancelTimelinePreviewRequest(root.requesterKey)
    }

    // A delegate reused for a different clip gets a new key, and the wish filed
    // under the old one would otherwise sit in the prefetcher until the cap
    // evicted it.
    property string lastRequesterKey: ""
    onRequesterKeyChanged: {
        if (root.lastRequesterKey !== "")
            Backend.cancelTimelinePreviewRequest(root.lastRequesterKey)
        root.lastRequesterKey = root.requesterKey
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
        // The cap is the declared ceiling, so a clamp here can only mean
        // growSlots() stopped honouring it - which is worth a log line, because
        // this Repeater is per-clip and a timeline has many.
        model: ModelGuard.bound(root.slotCapacity, root.slotCeiling,
                                "filmstrip.slots")

        delegate: Item {
            id: slot
            required property int index
            readonly property int slotIndex: root.firstSlot + index
            // Parked slots draw nothing and ask for nothing, so a capacity that
            // outruns the visible slice costs an Item and no decode.
            readonly property bool live: index < root.slotCount
                                         && slotIndex < root.totalSlots

            visible: slot.live
            x: slotIndex * root.cellWidth
            width: root.cellWidth
            height: root.cellHeight
            clip: true

            Image {
                x: slot.live ? -root.cellForSlot(slot.slotIndex) * root.cellWidth : 0
                width: root.cellWidth * Math.max(1, root.frameCount)
                height: root.cellHeight
                source: slot.live ? root.sourceUrl : ""
                // The sheet scales uniformly: a slot's aspect matches a cell's,
                // so Stretch here does not distort the frame.
                fillMode: Image.Stretch
                asynchronous: true
                cache: true
                smooth: true
            }

            // The frame this slot actually covers, faded in over the sheet cell.
            // The sheet is the placeholder: it is already in memory, so a slot is
            // never empty while the exact thumbnail is being decoded.
            Image {
                id: tile
                readonly property real wantBucketMs:
                    root.useTiles && slot.live
                    ? root.tileBucketForSlot(slot.slotIndex) : -1
                readonly property string wantUrl:
                    root.useTiles && slot.live
                    ? root.tileUrlForSlot(slot.slotIndex) : ""
                // Whether the backend holds that frame in memory right now. Read
                // first so the revision is captured as a dependency even when the
                // rest short-circuits: a tile that lands a moment later has to
                // re-evaluate this, and that is what makes the strip grow.
                readonly property bool ready: {
                    const revision = root.previewRevision
                    if (revision < 0 || tile.wantUrl === "")
                        return false
                    return Backend.timelineTileReady(root.tileToken,
                                                     tile.wantBucketMs)
                }
                // The last frame that did load. A zoom rewrites wantUrl to a
                // bucket nothing has decoded yet, and pointing the Image at it
                // would drop the picture it is holding and leave the cell empty
                // until the worker gets there. So the previous frame stays up -
                // very slightly the wrong moment of the source for a fraction of a
                // second - and is replaced the instant the right one is in memory.
                // This is what stops the strip flickering back to bare cells on
                // every zoom step.
                property string heldUrl: ""
                onReadyChanged: {
                    if (tile.ready)
                        tile.heldUrl = tile.wantUrl
                }

                anchors.fill: parent
                visible: tile.source !== ""
                source: tile.ready ? tile.wantUrl : tile.heldUrl
                sourceSize.height: Math.round(root.cellHeight)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                // Not cached by Qt, on purpose. The backend already caches:
                // TimelineTileCache holds the decoded frames and
                // PreviewFailureRegistry decides what is worth retrying, and both
                // can be invalidated. Qt's pixmap cache is keyed on the URL with
                // no way to drop an entry, so a second, dumber cache in front of
                // them can only pin stale contents for the rest of the session.
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
                color: Qt.rgba(0.1, 0.1, 0.1, 0.28)
                visible: slot.live && slot.slotIndex < root.totalSlots - 1
            }
        }
    }
}
