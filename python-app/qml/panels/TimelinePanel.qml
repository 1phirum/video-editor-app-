pragma ComponentBehavior: Bound
// qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"

Rectangle {
    id: root
    color: Theme.bgPanel
    focus: true

    signal toolRequested(int tool)
    signal vocalRemovalProgressJobsChanged(var jobs, string currentId)

    // Start at a safe density. A duration-aware fit is applied as soon as the
    // panel has a viewport; starting at 30 px/sec creates a huge scene for
    // multi-hour media before that fit pass can run.
    property real pps: 0.05
    property bool fitMode: true
    readonly property real playheadSec: Backend.playheadMs / 1000
    readonly property real sequenceSeconds: Backend.durationMs / 1000
    // Multi-hour sources need sub-pixel-per-second zoom to actually fit the
    // sequence in the viewport. Ruler and preview delegates are virtualized.
    readonly property real minPps: 0.01
    readonly property real maxPps: 400
    // Keep QML item coordinates below the GPU/scene-graph danger zone. A
    // multi-hour clip at the normal 400 px/s zoom would otherwise create an
    // item millions of pixels wide even though only a small viewport is
    // visible. Shotcut avoids this by keeping clip producers lazy and only
    // materializing the visible model rows.
    readonly property real maxTimelinePixels: 4000000
    readonly property real maxCoordinatePps: sequenceSeconds > 0
                                               ? Math.min(maxPps,
                                                          maxTimelinePixels
                                                          / sequenceSeconds)
                                               : maxPps
    readonly property real fitPps: sequenceSeconds > 0 && body.laneW > 0
                                        ? Math.max(minPps, Math.min(maxPps, body.laneW / (sequenceSeconds * 1.02)))
                                        : 4

    function updateTimelineClipViewport() {
        if (!Backend.timelineClipModel || !trackView || !body || body.laneW <= 0)
            return
        var msPerPixel = 1000 / Math.max(root.pps, 0.001)
        var leftMs = Math.max(0, trackView.contentX - body.laneX) * msPerPixel
        // The right edge is the visible span and nothing beyond it. Two things
        // used to widen it until it covered the entire sequence, which put every
        // clip in the model - 12,724 delegates for a transcript-length subtitle
        // track: it was pinned to Backend.durationMs, and the guard meant to
        // keep it ahead of the left edge compared a millisecond value against a
        // pixel value, so on a multi-hour timeline the millisecond figure won
        // the Math.max and was then scaled by ms-per-pixel a second time.
        // TimelineClipModel adds its own 30 s margin on each side, and that is
        // what absorbs scrolling without re-projecting the model.
        var rightMs = leftMs + Math.max(1, trackView.width) * msPerPixel
        // The scale goes with the window: the model collapses runs of clips too
        // narrow to be seen into single bars, and how narrow that is depends
        // entirely on the zoom. Without it, Fit zoom on an imported subtitle
        // track asked the view for one full clip delegate per segment.
        Backend.timelineClipModel.setViewport(leftMs, rightMs, msPerPixel)
    }
    // Apply Fit synchronously before clip delegates see a new long duration.
    // Deferring this by one event-loop turn briefly made an eight-hour clip
    // almost 900,000 pixels wide at the initial editing zoom.
    onSequenceSecondsChanged: {
        if (fitMode)
            pps = fitPps
        Qt.callLater(root.updateTimelineClipViewport)
    }
    onPpsChanged: Qt.callLater(root.updateTimelineClipViewport)
    Component.onCompleted: {
        if (fitMode)
            pps = fitPps
        Qt.callLater(root.updateTimelineClipViewport)
    }
    readonly property int trackHeight: Number(Backend.appSettings.timelineTrackHeight || 68)
    readonly property int subtitleTrackHeight: 26
    readonly property int subtitleTrackCount: Backend.hasSubtitleClips ? 1 : 0
    readonly property int subtitleTrackOffset: subtitleTrackCount * subtitleTrackHeight
    readonly property int effectTrackHeight: 26
    // The effect lane is not a permanent row. It appears once it holds a bar, and
    // it also appears while an effect is being dragged - the drop target has to
    // exist before the drop, and a row that is only there when it can be used is
    // one less empty lane to explain.
    readonly property int effectTrackCount:
        Backend.timelineEffects.length > 0 || Backend.effectDragActive ? 1 : 0
    readonly property int effectTrackOffset: effectTrackCount * effectTrackHeight
    // Top of the video stack. Both overlay lanes sit above it, so every
    // conversion between a track and a y is measured from here.
    readonly property int overlayTrackOffset: subtitleTrackOffset + effectTrackOffset
    readonly property int trackContentHeight: overlayTrackOffset
                                              + (Backend.videoTrackCount
                                                 + Backend.audioTrackCount) * trackHeight
    // Which lanes actually hold clips. Only those get a grey band, so the
    // scan is done once per clip change instead of once per lane repaint, and
    // it stops as soon as every existing lane has been accounted for (track
    // counts are pruned to the highest occupied track, so that is the norm).
    readonly property var occupiedTracks: {
        var map = ({})
        var wanted = Backend.videoTrackCount + Backend.audioTrackCount
                     + subtitleTrackCount + effectTrackCount
        var found = 0
        for (var i = 0; i < Backend.clips.length && found < wanted; ++i) {
            var clip = Backend.clips[i]
            var key = clip.kind === "subtitle"
                      ? "S1" : String(clip.track || "").toUpperCase()
            if (key !== "" && map[key] === undefined) {
                map[key] = true
                ++found
            }
        }
        return map
    }
    property string selectedClipId: ""
    property var selectedClipIds: []
    property var vocalRemovalJobs: []
    property var vocalRemovalQueue: []
    property string vocalRemovalCurrentId: ""
    onVocalRemovalJobsChanged: vocalRemovalProgressJobsChanged(vocalRemovalJobs, vocalRemovalCurrentId)
    onVocalRemovalCurrentIdChanged: vocalRemovalProgressJobsChanged(vocalRemovalJobs, vocalRemovalCurrentId)
    onSelectedClipIdChanged: Backend.selectedClipId = selectedClipId
    // 0 Selection, 1 Track Select Forward, 2 Ripple Edit, 3 Razor,
    // 4 Hand, 5 Zoom. Shared with the tool rail by EditView.
    property int activeTool: 0
    property var trackStateList: Backend.trackStates
    property bool handPanning: false
    property real handPressX: 0
    property real handPressY: 0
    property real handContentX: 0
    property real handContentY: 0
    property bool scrubbingPlayhead: false
    property bool resumePlaybackAfterScrub: false
    property bool pointerInteractionActive: false
    property var activeDragClip: null
    property point clipDragPointer: Qt.point(0, 0)
    // Live snap state for the drag guide. It lives on the panel and not on the
    // clip delegate because the join it marks belongs to the sequence: the line
    // has to cross every track, and only one can ever be shown at a time.
    property bool snapGuideActive: false
    property real snapGuideMs: 0
    property bool marqueeSelecting: false
    property point marqueeStart: Qt.point(0, 0)
    property point marqueeEnd: Qt.point(0, 0)
    property point marqueePointer: Qt.point(0, 0)

    function seekPlayheadFromRuler(xPosition) {
        if (xPosition <= body.laneX)
            return
        Backend.playheadMs = Math.max(
                    0, Math.min(Backend.durationMs,
                                (xPosition - body.laneX + trackView.contentX)
                                / Math.max(root.pps, 0.001) * 1000))
    }

    function beginPlayheadScrub(xPosition) {
        root.forceActiveFocus()
        pointerInteractionActive = true
        resumePlaybackAfterScrub = Backend.playing
        scrubbingPlayhead = true
        if (Backend.playing)
            Backend.playing = false
        seekPlayheadFromRuler(xPosition)
    }

    function finishPlayheadScrub(xPosition, commitPosition) {
        if (!scrubbingPlayhead)
            return
        if (commitPosition)
            seekPlayheadFromRuler(xPosition)
        var shouldResume = resumePlaybackAfterScrub
        scrubbingPlayhead = false
        pointerInteractionActive = false
        resumePlaybackAfterScrub = false
        if (shouldResume && Backend.durationMs > 0)
            Qt.callLater(function() { Backend.playing = true })
    }

    function trackState(track) {
        for (var i = 0; i < trackStateList.length; ++i) {
            if (trackStateList[i].id === track)
                return trackStateList[i]
        }
        return { visible: true, locked: false, solo: false,
                 syncLocked: true, targeted: true }
    }
    function trackLocked(track) { return trackState(track).locked === true }
    function trackVisible(track) { return trackState(track).visible !== false }
    // Proximity in milliseconds for a given pixel gap, so the pull stays the
    // same distance on screen at every zoom level instead of covering hours at
    // the zoomed-out end of a 26-hour source.
    function snapThresholdMs(pixels) {
        return Math.max(20, Math.round(1000 * pixels / Math.max(root.pps, 0.1)))
    }
    function snappedTime(ms, ids) {
        if (!Backend.snappingEnabled)
            return Math.max(0, Math.round(ms))
        return Backend.snapTime(Math.round(ms), ids || [],
                                root.snapThresholdMs(10))
    }

    // Snap a clip that is being dragged whole. Unlike snappedTime this offers
    // both of the clip's edges to the solver, so a clip whose tail meets the
    // head of the next one locks there even though its own head is nowhere near
    // a join, and it reports the join back so the guide can be drawn on it.
    function updateDragSnap(clipItem) {
        if (!clipItem || !clipItem.dragging || !clipItem.modelData
                || !Backend.snappingEnabled) {
            root.clearDragSnap(clipItem)
            return
        }
        var requestedMs = Math.round(clipItem.dragPreviewX / root.pps * 1000)
        var ids = root.selectedClipIds.indexOf(clipItem.modelData.id) >= 0
                  ? root.selectedClipIds : [clipItem.modelData.id]
        var snap = Backend.snapClipDrag(
                       requestedMs,
                       Math.round(clipItem.modelData.durationMs || 0),
                       ids, root.snapThresholdMs(12))
        if (!snap || snap.snapped !== true) {
            root.clearDragSnap(clipItem)
            return
        }
        // The pull is an offset laid over the raw pointer position, never a
        // write back into it. Writing it back is what makes a snap sticky: the
        // pointer would have to cross the threshold twice to leave a join, and
        // every move event would drag the clip one more snap along.
        clipItem.dragSnapOffsetX = (Number(snap.startMs) - requestedMs)
                                   / 1000 * root.pps
        root.snapGuideMs = Number(snap.guideMs)
        root.snapGuideActive = true
    }

    function clearDragSnap(clipItem) {
        if (clipItem)
            clipItem.dragSnapOffsetX = 0
        root.snapGuideActive = false
    }

    SubtitleContextMenu {
        id: subtitleContextMenu
        parent: Overlay.overlay
        onSettingsRequested: (subtitleId) => {
            for (var i = 0; i < Backend.clips.length; ++i) {
                var clip = Backend.clips[i]
                if (clip.id === subtitleId) {
                    subtitleSettingsPopup.subtitleText = clip.text || ""
                    break
                }
            }
            subtitleSettingsPopup.open()
        }
    }

    ClipContextMenu {
        id: clipContextMenu
        parent: Overlay.overlay
        onSplitRequested: clipId => Backend.splitClip(clipId, Backend.playheadMs)
        onDeleteRequested: (clipId, ripple) => {
            var ids = root.isClipSelected(clipId)
                ? root.selectedClipIds.slice(0) : [clipId]
            var removed = ripple
                ? Backend.rippleDeleteClips(ids)
                : Backend.removeClips(ids)
            if (removed) {
                root.selectedClipIds = []
                root.selectedClipId = ""
            }
        }
        onVocalRemovalRequested: clipId => {
            var ids = root.isClipSelected(clipId) ? root.selectedClipIds.slice(0) : [clipId]
            root.startVocalRemovalBatch(ids)
        }
    }

    Connections {
        target: Backend
        function onTimelinePlacementFinished(success, clipIds) {
            if (!success || clipIds.length === 0)
                return
            root.selectedClipIds = clipIds
            root.selectedClipId = clipIds[0]
        }
        function onDemucsFinished(success, clipId) {
            if (root.vocalRemovalCurrentId === "")
                return
            var completed = root.vocalRemovalJobs.slice()
            for (var i = 0; i < completed.length; ++i) {
                if (completed[i].id === root.vocalRemovalCurrentId) {
                    completed[i].status = success ? "Complete" : "Failed"
                    completed[i].detail = success ? "Vocal separation complete" : (Backend.lastError || "Vocal separation failed")
                    break
                }
            }
            root.vocalRemovalJobs = completed
            root.vocalRemovalCurrentId = ""
            if (root.vocalRemovalQueue.length > 0) {
                var next = root.vocalRemovalQueue.shift()
                Qt.callLater(function() { root.startVocalRemovalJob(next) })
            }
        }
    }

    function startVocalRemovalJob(jobId) {
        var job = null
        for (var i = 0; i < vocalRemovalJobs.length; ++i)
            if (vocalRemovalJobs[i].id === jobId) job = vocalRemovalJobs[i]
        if (!job) return
        vocalRemovalCurrentId = jobId
        var started = false
        for (var j = 0; j < Backend.clips.length; ++j) {
            if (String(Backend.clips[j].id) === String(jobId)) {
                started = Backend.setClipEffectSetting(jobId, "vocalRemoval", true)
                break
            }
        }
        if (!started) {
            var failed = vocalRemovalJobs.slice()
            for (var k = 0; k < failed.length; ++k)
                if (failed[k].id === jobId) failed[k].status = "Failed"
            vocalRemovalJobs = failed
            vocalRemovalCurrentId = ""
            if (vocalRemovalQueue.length > 0) {
                var nextFailed = vocalRemovalQueue.shift()
                Qt.callLater(function() { root.startVocalRemovalJob(nextFailed) })
            }
        }
    }

    function startVocalRemovalBatch(ids) {
        var jobs = []
        for (var i = 0; i < ids.length; ++i) {
            for (var j = 0; j < Backend.clips.length; ++j) {
                var clip = Backend.clips[j]
                if (String(clip.id) !== String(ids[i]) || clip.kind === "subtitle") continue
                var media = mediaForId(clip.mediaId)
                if (!media || Number(media.channels || 0) <= 0) continue
                jobs.push({ id: clip.id, name: media.name || clip.name || clip.id,
                            status: "Queued", detail: "", durationMs: Number(media.durationMs || clip.durationMs || 0),
                            thumbnailUrl: media.thumbnailUrl || "" })
            }
        }
        vocalRemovalJobs = jobs
        var queue = []
        for (var q = 1; q < jobs.length; ++q) queue.push(jobs[q].id)
        vocalRemovalQueue = queue
        if (jobs.length > 0) startVocalRemovalJob(jobs[0].id)
    }

    SubtitleSettingsPopup {
        id: subtitleSettingsPopup
        parent: Overlay.overlay
    }

    TimelineMarkerDialog {
        id: markerDialog
        parent: Overlay.overlay
        onSaveRequested: (markerId, positionMs, name, color) => {
            if (markerId === "")
                Backend.addMarker(positionMs, name, color)
            else
                Backend.updateMarker(markerId, positionMs, name, color)
        }
        onRemoveRequested: markerId => Backend.removeMarker(markerId)
    }

    component TrackButton: Button {
        HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        implicitWidth: 28
        implicitHeight: 22
        padding: 0
        flat: true
        font.pixelSize: Theme.fsXs
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? Theme.textSecondary : Theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: parent.down ? Theme.hover : "transparent"
            border.width: 1
            border.color: Theme.border
        }
    }

    function fmtTC(sec) {
        var h = Math.floor(sec / 3600)
        var m = Math.floor(sec / 60) % 60
        var s = Math.floor(sec % 60)
        var mmss = (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
        return h > 0 ? (h < 10 ? "0" : "") + h + ":" + mmss : mmss
    }

    // The ruler's own label. Unlike fmtTC it keeps one width for the whole ruler
    // rather than growing a field at the first tick past an hour: a row of labels
    // that alternates between 00:30 and 01:00:00 reads as noise, and the widest
    // one is what the tick spacing has to be sized for anyway.
    function rulerLabel(sec, hourScale) {
        var whole = Math.max(0, Math.floor(sec + 0.0005))
        var s = whole % 60
        var pad = function (value) { return value < 10 ? "0" + value : "" + value }
        if (!hourScale)
            return Math.floor(whole / 60) + ":" + pad(s)
        return Math.floor(whole / 3600) + ":" + pad(Math.floor(whole / 60) % 60)
               + ":" + pad(s)
    }

    function niceTickInterval(targetSeconds) {
        // Runs to a full day. The old ladder stopped at an hour, so a 26-hour
        // source - which is what a long recap upload actually is - pinned the
        // interval at 3600 s and the ruler then had to fit an 01:00:00 label into
        // 57 px. The labels ran into each other, which is the reported symptom.
        var intervals = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600,
                         7200, 10800, 21600, 43200, 86400]
        for (var i = 0; i < intervals.length; ++i) {
            if (intervals[i] >= targetSeconds)
                return intervals[i]
        }
        return intervals[intervals.length - 1]
    }

    function setZoom(value) {
        fitMode = false
        pps = Math.max(minPps, Math.min(maxCoordinatePps, value))
    }

    function fitToSequence() {
        fitMode = true
        pps = fitPps
    }

    function trackY(track) {
        var number = Math.max(1, parseInt(track.substring(1)))
        if (track.charAt(0) === "S")
            return 0
        if (track.charAt(0) === "F")
            return subtitleTrackOffset
        if (track.charAt(0) === "V")
            return overlayTrackOffset + (Backend.videoTrackCount - number) * trackHeight
        return overlayTrackOffset + (Backend.videoTrackCount + number - 1) * trackHeight
    }

    function trackForY(y) {
        if (subtitleTrackCount > 0 && y < subtitleTrackHeight)
            return "S1"
        if (y < overlayTrackOffset)
            return "F1"
        var adjustedY = Math.max(0, y - overlayTrackOffset)
        var row = Math.max(0, Math.min(Backend.videoTrackCount
                                      + Backend.audioTrackCount - 1,
                                      Math.floor(adjustedY / trackHeight)))
        if (row < Backend.videoTrackCount)
            return "V" + (Backend.videoTrackCount - row)
        return "A" + (row - Backend.videoTrackCount + 1)
    }

    function trackAcceptsKind(track, kind) {
        if (!track || !kind)
            return false
        if (kind === "subtitle")
            return track === "S1"
        if (kind === "effect")
            return track === "F1"
        return kind === "audio" ? track.charAt(0) === "A"
                                : track.charAt(0) === "V"
    }

    // The lane under a lane-space y, or "" when the pointer is in the empty
    // space above the first lane or below the last one. trackForY() clamps
    // instead, which is what a clip drag wants - it is already constrained to
    // the lanes - and wrong for a drop: the clamp turns "above the top track"
    // into "the top track", and once the kind no longer matches, the old
    // fallback turned that into track 1. Row order comes from the backend so
    // the drop handlers and the placement rules cannot drift apart.
    function trackAtY(y) {
        if (y < 0)
            return ""
        if (y < subtitleTrackOffset)
            return "S1"
        if (y < overlayTrackOffset)
            return "F1"
        return Backend.trackForRow(
                    Math.floor((y - overlayTrackOffset) / trackHeight))
    }

    // A drop past the end of the stack means "put it on a new track", the way
    // Premiere adds a lane when you drag above V1. Only the outward direction
    // counts for each kind: video grows upwards, audio downwards. The track
    // itself is created by the placement call, not here, so the rows do not
    // shift under the pointer mid-gesture.
    function overflowTrackForY(y, kind) {
        if (kind === "audio") {
            var lanesEnd = overlayTrackOffset
                    + (Backend.videoTrackCount + Backend.audioTrackCount)
                      * trackHeight
            return y >= lanesEnd && Backend.audioTrackCount < 64
                    ? "A" + (Backend.audioTrackCount + 1) : ""
        }
        if (kind === "video" || kind === "image") {
            return y < overlayTrackOffset && Backend.videoTrackCount < 64
                    ? "V" + (Backend.videoTrackCount + 1) : ""
        }
        return ""
    }

    // Where an item of `kind` belongs for a pointer at lane-space `y`: the lane
    // under the pointer when it can take the item, a new track when the pointer
    // is past the end of the stack, otherwise the nearest compatible lane.
    function dropTrackForY(y, kind) {
        if (kind === "subtitle")
            return "S1"
        if (kind === "effect")
            return "F1"
        var row = root.trackAtY(y)
        if (root.trackAcceptsKind(row, kind))
            return row
        var fresh = root.overflowTrackForY(y, kind)
        if (fresh !== "")
            return fresh
        // Nearest compatible lane, measured in rows on screen - not a blind
        // reset to V1/A1.
        return Backend.compatibleTrackFor(
                    kind, row === "" ? root.trackForY(y) : row)
    }

    function mediaForId(mediaId) {
        for (var i = 0; i < Backend.media.length; ++i) {
            if (Backend.media[i].id === mediaId)
                return Backend.media[i]
        }
        return null
    }

    function toggleVisible(track) {
        Backend.setTrackState(track, "visible", !root.trackVisible(track))
    }

    function toggleLocked(track) {
        Backend.setTrackState(track, "locked", !root.trackLocked(track))
    }

    function selectTrackForward(clip, allTracks) {
        if (!clip)
            return
        var next = []
        for (var i = 0; i < Backend.clips.length; ++i) {
            var candidate = Backend.clips[i]
            if (candidate.kind === "subtitle" && clip.kind !== "subtitle")
                continue
            if ((allTracks && root.trackState(candidate.track).targeted)
                    || candidate.track === clip.track) {
                if (candidate.startMs >= clip.startMs)
                    next.push(candidate.id)
            }
        }
        selectedClipIds = next
        selectedClipId = next.length ? next[next.length - 1] : ""
    }

    function razorAt(ms) {
        var didSplit = false
        for (var i = 0; i < Backend.clips.length; ++i) {
            var clip = Backend.clips[i]
            if (clip.kind === "subtitle" || root.trackLocked(clip.track)
                    || root.trackState(clip.track).targeted === false)
                continue
            if (ms > clip.startMs && ms < clip.startMs + clip.durationMs) {
                didSplit = Backend.splitClip(clip.id, ms) || didSplit
            }
        }
        return didSplit
    }

    function isClipSelected(id) {
        return selectedClipIds.indexOf(id) >= 0
    }

    function updateMarqueeSelection() {
        var left = Math.min(marqueeStart.x, marqueeEnd.x)
        var right = Math.max(marqueeStart.x, marqueeEnd.x)
        var top = Math.min(marqueeStart.y, marqueeEnd.y)
        var bottom = Math.max(marqueeStart.y, marqueeEnd.y)
        var next = []
        for (var i = 0; i < Backend.clips.length; ++i) {
            var clip = Backend.clips[i]
            if (clip.kind === "subtitle")
                continue
            var clipLeft = Number(clip.startMs) / 1000 * root.pps
            var clipRight = clipLeft + Math.max(18,
                            Number(clip.durationMs) / 1000 * root.pps)
            var clipTop = root.trackY(clip.track) + 3
            var clipBottom = clipTop + root.trackHeight - 6
            if (clipLeft < right && clipRight > left
                    && clipTop < bottom && clipBottom > top)
                next.push(clip.id)
        }
        selectedClipIds = next
        selectedClipId = next.length > 0 ? next[next.length - 1] : ""
    }

    function refreshMarqueeFromPointer() {
        if (!marqueeSelecting)
            return
        marqueeEnd = trackView.mapToItem(clipLayer,
                                         marqueePointer.x, marqueePointer.y)
        updateMarqueeSelection()
    }

    function dragTouchesClip(clipItem, edge) {
        if (!clipItem.dragging || !clipItem.modelData)
            return false
        var dragged = clipItem.modelData
        // Snapped position, matching what is drawn: the seam has to appear on
        // the frame the clip is shown at, not the one the pointer is over.
        var draggedStart = (clipItem.dragPreviewX + clipItem.dragSnapOffsetX)
                           / root.pps * 1000
        var draggedEnd = draggedStart + Number(dragged.durationMs || 0)
        var toleranceMs = Math.max(2, 2000 / Math.max(1, root.pps))
        for (var i = 0; i < Backend.clips.length; ++i) {
            var other = Backend.clips[i]
            if (other.id === dragged.id || other.track !== dragged.track
                    || other.enabled === false)
                continue
            var otherStart = Number(other.startMs || 0)
            var otherEnd = otherStart + Number(other.durationMs || 0)
            if (edge === "right"
                    && Math.abs(draggedEnd - otherStart) <= toleranceMs)
                return true
            if (edge === "left"
                    && Math.abs(draggedStart - otherEnd) <= toleranceMs)
                return true
        }
        return false
    }

    function clipAtPlayhead() {
        var result = null
        var resultRank = -1
        for (var i = 0; i < Backend.clips.length; ++i) {
            var clip = Backend.clips[i]
            if (clip.enabled === false || clip.kind === "subtitle"
                    || root.trackState(clip.track).targeted === false
                    || Backend.playheadMs < clip.startMs
                    || Backend.playheadMs >= clip.startMs + clip.durationMs)
                continue

            var isVideo = clip.track && clip.track.charAt(0) === "V"
            var rank = isVideo ? 1000 + parseInt(clip.track.substring(1)) : 0
            if (rank > resultRank) {
                result = clip
                resultRank = rank
            }
        }
        return result
    }

    function editingClip() {
        for (var i = 0; i < Backend.clips.length; ++i) {
            if (Backend.clips[i].id === selectedClipId)
                return Backend.clips[i]
        }
        return clipAtPlayhead()
    }

    function ensureEditingSelection() {
        var clip = editingClip()
        if (!clip)
            return null
        if (selectedClipIds.indexOf(clip.id) < 0) {
            selectedClipIds = [clip.id]
            selectedClipId = clip.id
        }
        return clip
    }

    function playheadInsideSelectedClip() {
        var clip = editingClip()
        return clip !== null
               && Backend.playheadMs > clip.startMs
               && Backend.playheadMs < clip.startMs + clip.durationMs
    }

    function deleteSelectedClips() {
        if (selectedClipIds.length === 0)
            ensureEditingSelection()
        if (selectedClipIds.length === 0)
            return
        var removed = root.activeTool === 2
                ? Backend.rippleDeleteClips(selectedClipIds.slice(0))
                : Backend.removeClips(selectedClipIds.slice(0))
        if (removed) {
            selectedClipIds = []
            selectedClipId = ""
        }
    }

    Keys.onPressed: event => {
        var control = (event.modifiers & Qt.ControlModifier) !== 0
        var shift = (event.modifiers & Qt.ShiftModifier) !== 0
        if (event.key === Qt.Key_Delete) {
            root.deleteSelectedClips()
            event.accepted = true
        } else if (control && event.key === Qt.Key_Z && shift) {
            Backend.redo()
            event.accepted = true
        } else if (control && event.key === Qt.Key_Z) {
            Backend.undo()
            event.accepted = true
        } else if (control && event.key === Qt.Key_Y) {
            Backend.redo()
            event.accepted = true
        } else if (event.key === Qt.Key_V) {
            root.toolRequested(0); event.accepted = true
        } else if (event.key === Qt.Key_A) {
            root.toolRequested(1); event.accepted = true
        } else if (event.key === Qt.Key_B) {
            root.toolRequested(2); event.accepted = true
        } else if (event.key === Qt.Key_C) {
            root.toolRequested(3); event.accepted = true
        } else if (event.key === Qt.Key_H) {
            root.toolRequested(4); event.accepted = true
        } else if (event.key === Qt.Key_Z) {
            root.toolRequested(5); event.accepted = true
        } else if (event.key === Qt.Key_M) {
            Backend.addMarker(Backend.playheadMs)
            event.accepted = true
        } else if (event.key === Qt.Key_S) {
            Backend.snappingEnabled = !Backend.snappingEnabled
            event.accepted = true
        }
    }

    function linkedClipIds(id) {
        var selected = null
        for (var i = 0; i < Backend.clips.length; ++i) {
            if (Backend.clips[i].id === id) {
                selected = Backend.clips[i]
                break
            }
        }
        if (!selected || !selected.linkGroupId)
            return [id]
        var linked = []
        for (var j = 0; j < Backend.clips.length; ++j) {
            if (Backend.clips[j].linkGroupId === selected.linkGroupId)
                linked.push(Backend.clips[j].id)
        }
        return linked.length > 0 ? linked : [id]
    }

    function selectClip(id, modifiers) {
        var next = selectedClipIds.slice(0)
        var additive = (modifiers & Qt.ControlModifier) !== 0
                       || (modifiers & Qt.MetaModifier) !== 0
        var range = (modifiers & Qt.ShiftModifier) !== 0

        if (range && selectedClipIds.length > 0) {
            var anchor = Backend.clips.findIndex(function(item) {
                return item.id === selectedClipIds[0]
            })
            var target = Backend.clips.findIndex(function(item) {
                return item.id === id
            })
            if (anchor >= 0 && target >= 0) {
                var first = Math.min(anchor, target)
                var last = Math.max(anchor, target)
                next = []
                for (var i = first; i <= last; ++i)
                    next.push(Backend.clips[i].id)
            }
        } else if (additive) {
            var index = next.indexOf(id)
            if (index >= 0)
                next.splice(index, 1)
            else
                next.push(id)
        } else {
            next = linkedClipIds(id)
        }

        selectedClipIds = next
        selectedClipId = next.indexOf(id) >= 0
                ? id : (next.length > 0 ? next[next.length - 1] : "")
    }

    function effectDropCompatible(clip, dragSource) {
        if (!clip || clip.kind === "subtitle" || clip.kind === "effect"
                || !dragSource)
            return false
        if (dragSource.dragMediaType === "video")
            return clip.kind === "video" || clip.kind === "image"
        var media = root.mediaForId(clip.mediaId)
        return media !== null && Number(media.channels || 0) > 0
    }

    function moveSelectedClip(activeClip, newStartMs, newTrack, preSnapped) {
        if (!activeClip || selectedClipIds.length === 0)
            return
        var oldNumber = parseInt(activeClip.track.substring(1))
        var newNumber = parseInt(newTrack.substring(1))
        var trackDelta = newTrack.charAt(0) === activeClip.track.charAt(0)
                         ? newNumber - oldNumber : 0
        // A drag has already resolved its own snap against both edges. Running
        // the start-only solver over the result again would fight it: a clip
        // parked by its tail would have its head yanked to some other join.
        var snapped = preSnapped === true
                      ? Math.max(0, Math.round(newStartMs))
                      : root.snappedTime(newStartMs, selectedClipIds)
        Backend.moveClips(selectedClipIds, snapped - activeClip.startMs, trackDelta)
    }

    function maybeCreateTrackForDrag(clipItem) {
        if (!clipItem.dragAutoTrack || clipItem.dragAutoTrackAdded)
            return
        // The overlay lanes are fixed rows, so dragging a bar around on one of
        // them never grows the video/audio stack.
        if (clipItem.modelData.kind === "subtitle"
                || clipItem.modelData.kind === "effect")
            return
        if (clipItem.modelData.kind === "audio") {
            if (clipItem.y >= root.trackContentHeight - clipItem.height - 2
                    && Backend.audioTrackCount < 64) {
                clipItem.dragAutoTrackAdded = true
                // Not sticky: this lane belongs to the clip being dragged, so if
                // the drag ends somewhere else the lane goes away with it.
                Backend.addTrack("audio", false)
                clipItem.dragPreviewY = root.trackContentHeight - clipItem.height
            }
        } else if (clipItem.y <= root.overlayTrackOffset + 2
                   && Backend.videoTrackCount < 64) {
            clipItem.dragAutoTrackAdded = true
            Backend.addTrack("video", false)
        }
    }

    Connections {
        target: Backend
        function onClipsChanged() {
            var valid = []
            for (var i = 0; i < root.selectedClipIds.length; ++i) {
                for (var j = 0; j < Backend.clips.length; ++j) {
                    if (Backend.clips[j].id === root.selectedClipIds[i]) {
                        valid.push(root.selectedClipIds[i])
                        break
                    }
                }
            }
            root.selectedClipIds = valid
            root.selectedClipId = valid.length > 0 ? valid[valid.length - 1] : ""
        }
        function onTimelineChanged() {
            if (root.fitMode)
                Qt.callLater(root.fitToSequence)
            else if (root.pps < root.minPps)
                root.pps = root.minPps
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Sequence header + toolbar -------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Theme.bgPanel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 6

                Text { text: Backend.sequenceName; color: Theme.textPrimary; font.pixelSize: Theme.fsSm; font.weight: Font.DemiBold }
                Text { text: root.fmtTC(Backend.durationMs / 1000); color: Theme.textMuted; font.pixelSize: Theme.fsSm }

                // Edit toolbar
                Rectangle {
                    Layout.leftMargin: 10
                    Layout.alignment: Qt.AlignVCenter
                    width: 1; height: 14; color: Theme.border
                }
                IconButton {
                    iconName: "undo-2"; boxSize: 24; glyphSize: 13
                    hoverEnabled: true
                    enabled: Backend.canUndo
                    ToolTip.visible: hovered
                    ToolTip.text: "Undo"
                    onClicked: Backend.undo()
                }
                IconButton {
                    iconName: "redo-2"; boxSize: 24; glyphSize: 13
                    hoverEnabled: true
                    enabled: Backend.canRedo
                    ToolTip.visible: hovered
                    ToolTip.text: "Redo"
                    onClicked: Backend.redo()
                }
                Rectangle { Layout.alignment: Qt.AlignVCenter; width: 1; height: 12; color: Theme.border }
                IconButton {
                    iconName: "skip-back"; boxSize: 24; glyphSize: 14
                    hoverEnabled: true
                    enabled: Backend.clips.length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Delete left of playhead"
                    onClicked: {
                        var clip = root.ensureEditingSelection()
                        if (clip)
                            Backend.deleteClipLeft(clip.id, Backend.playheadMs)
                    }
                }
                IconButton {
                    iconName: "scissors"; boxSize: 24; glyphSize: 13
                    hoverEnabled: true
                    enabled: Backend.clips.length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Split at playhead"
                    onClicked: {
                        var clip = root.ensureEditingSelection()
                        if (clip)
                            Backend.splitClip(clip.id, Backend.playheadMs)
                    }
                }
                IconButton {
                    iconName: "skip-forward"; boxSize: 24; glyphSize: 14
                    hoverEnabled: true
                    enabled: Backend.clips.length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Delete right of playhead"
                    onClicked: {
                        var clip = root.ensureEditingSelection()
                        if (clip)
                            Backend.deleteClipRight(clip.id, Backend.playheadMs)
                    }
                }
                Rectangle { Layout.alignment: Qt.AlignVCenter; width: 1; height: 12; color: Theme.border }
                IconButton {
                    iconName: "trash-2"; boxSize: 24; glyphSize: 13
                    hoverEnabled: true
                    restColor: Theme.danger; hoverColor: Theme.danger
                    enabled: root.selectedClipIds.length > 0
                             || root.clipAtPlayhead() !== null
                    ToolTip.visible: hovered
                    ToolTip.text: "Delete selected clips"
                    onClicked: root.deleteSelectedClips()
                }
                IconButton {
                    iconName: "arrow-right-left"; boxSize: 24; glyphSize: 13
                    active: Backend.snappingEnabled
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: Backend.snappingEnabled ? "Disable snapping (S)"
                                                          : "Enable snapping (S)"
                    onClicked: Backend.snappingEnabled = !Backend.snappingEnabled
                }
                IconButton {
                    iconName: "plus"; boxSize: 24; glyphSize: 13
                    hoverEnabled: true
                    enabled: Backend.durationMs > 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Add marker at playhead (M)"
                    onClicked: Backend.addMarker(Backend.playheadMs)
                }
                Rectangle { Layout.alignment: Qt.AlignVCenter; width: 1; height: 12; color: Theme.border }
                TrackButton {
                    text: "V+"
                    ToolTip.visible: hovered
                    ToolTip.text: "Add video track"
                    onClicked: Backend.addTrack("video")
                }
                TrackButton {
                    text: "V-"
                    enabled: Backend.videoTrackCount > 1
                    ToolTip.visible: hovered
                    ToolTip.text: "Remove highest empty video track"
                    onClicked: Backend.removeLastTrack("video")
                }
                TrackButton {
                    text: "A+"
                    ToolTip.visible: hovered
                    ToolTip.text: "Add audio track"
                    onClicked: Backend.addTrack("audio")
                }
                TrackButton {
                    text: "A-"
                    enabled: Backend.audioTrackCount > 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Remove last empty audio track"
                    onClicked: Backend.removeLastTrack("audio")
                }

                Item { Layout.fillWidth: true }

                // Zoom cluster
                IconButton { iconName: "zoom-out"; boxSize: 24; glyphSize: 14; onClicked: root.setZoom(root.pps / 1.35) }
                Item {
                    id: zoom
                    Layout.alignment: Qt.AlignVCenter
                    width: 96
                    height: 16
                    readonly property real range: Math.max(0.001, root.maxCoordinatePps - root.minPps)
                    readonly property real frac: Math.max(0, Math.min(1, (root.pps - root.minPps) / range))
                    function seek(mx) {
                        root.setZoom(root.minPps + Math.max(0, Math.min(1, mx / width)) * range)
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width; height: 4; radius: 2
                        color: Theme.bgPrimary
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: parent.width * zoom.frac; radius: 2; color: Theme.accent
                        }
                    }
                    Rectangle {
                        width: 10; height: 10; radius: 5; color: Theme.textPrimary
                        anchors.verticalCenter: parent.verticalCenter
                        x: (parent.width - 10) * zoom.frac
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: (m) => zoom.seek(m.x)
                        onPositionChanged: (m) => { if (pressed) zoom.seek(m.x) }
                    }
                }
                IconButton { iconName: "zoom-in"; boxSize: 24; glyphSize: 14; onClicked: root.setZoom(root.pps * 1.35) }
                Button {
                    id: fitBtn
                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    implicitHeight: 20
                    implicitWidth: fitLabel.implicitWidth + 12
                    flat: true
                    hoverEnabled: false
                    background: Rectangle { radius: Theme.radiusSm; color: "transparent" }
                    contentItem: Text {
                        id: fitLabel
                        text: "Fit"; color: Theme.textMuted; font.pixelSize: Theme.fsXs
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: root.fitToSequence()
                }
            }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

        // ---- Timeline body -------------------------------------------
        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            readonly property real laneX: Theme.trackHeadWidth
            readonly property real laneW: width - laneX
            readonly property real playheadX: laneX + root.playheadSec * root.pps
            // Labels carry hours as soon as the sequence itself does, so the width
            // below is the width of every label on the ruler, not just of the ones
            // past the first hour.
            readonly property bool rulerHourScale: root.sequenceSeconds >= 3600
            // Sized from the text, not from a constant. 72 px was wide enough for
            // "00:30" and too narrow for "25:00:00", which is how a long source
            // ended up with overlapping labels.
            readonly property real rulerLabelSlot:
                Math.max(56, rulerLabelMetrics.width + 34)
            readonly property real tickIntervalSec:
                root.niceTickInterval(rulerLabelSlot / Math.max(root.pps, 0.001))
            // Ten per label, so the halfway tick is a real division instead of the
            // 2-of-5 the ruler used to draw off-centre.
            readonly property real tickSubdivisionSec: tickIntervalSec / 10

            TextMetrics {
                id: rulerLabelMetrics
                font.pixelSize: Theme.fsXs
                text: body.rulerHourScale ? "00:00:00" : "00:00"
            }

            onLaneWChanged: {
                if (root.fitMode)
                    root.pps = root.fitPps
                else if (root.pps < root.minPps)
                    root.pps = root.minPps
                Qt.callLater(root.updateTimelineClipViewport)
            }

            TimelinePlaybackFollower {
                id: playbackFollower
                target: trackView
                playheadContentX: body.playheadX
                headerWidth: body.laneX
                followEnabled: Backend.appSettings.timelineAutoScroll !== false
                blocked: root.scrubbingPlayhead
                         || root.pointerInteractionActive
                         || root.handPanning
                         || root.marqueeSelecting
                         || trackView.dragging
                         || trackView.flicking
            }

            Connections {
                target: Backend
                function onPlayheadChanged() {
                    playbackFollower.requestFollow()
                }
                function onPlayingChanged() {
                    if (Backend.playing)
                        playbackFollower.requestFollow()
                }
            }

            Timer {
                id: marqueeAutoScroll
                interval: 16
                repeat: true
                running: root.marqueeSelecting
                onTriggered: {
                    var edge = 36
                    var speedX = 0
                    var speedY = 0
                    var laneLeft = body.laneX + edge
                    if (root.marqueePointer.x < laneLeft)
                        speedX = -Math.max(1, Math.round(
                            (laneLeft - root.marqueePointer.x) * 0.35))
                    else if (root.marqueePointer.x > trackView.width - edge)
                        speedX = Math.max(1, Math.round(
                            (root.marqueePointer.x - (trackView.width - edge)) * 0.35))
                    if (root.marqueePointer.y < edge)
                        speedY = -Math.max(1, Math.round(
                            (edge - root.marqueePointer.y) * 0.35))
                    else if (root.marqueePointer.y > trackView.height - edge)
                        speedY = Math.max(1, Math.round(
                            (root.marqueePointer.y - (trackView.height - edge)) * 0.35))

                    if (speedX !== 0)
                        trackView.contentX = Math.max(0, Math.min(
                            trackView.contentWidth - trackView.width,
                            trackView.contentX + speedX))
                    if (speedY !== 0)
                        trackView.contentY = Math.max(0, Math.min(
                            trackView.contentHeight - trackView.height,
                            trackView.contentY + speedY))
                    // Recalculate even when the scroll position is clamped;
                    // this keeps the selection and marquee geometry in sync
                    // while the pointer is held outside the viewport.
                    root.refreshMarqueeFromPointer()
                }
            }

            Timer {
                id: clipDragAutoScroll
                interval: 16
                repeat: true
                running: root.activeDragClip !== null
                         && root.activeDragClip.dragging
                onTriggered: {
                    var edge = 72
                    var pointerX = root.clipDragPointer.x
                    var speed = 0
                    if (pointerX < body.laneX + edge)
                        speed = -Math.max(2, Math.round(
                            (body.laneX + edge - pointerX) * 0.32))
                    else if (pointerX > trackView.width - edge)
                        speed = Math.max(2, Math.round(
                            (pointerX - (trackView.width - edge)) * 0.32))
                    if (speed === 0)
                        return

                    var previous = trackView.contentX
                    trackView.contentX = Math.max(0, Math.min(
                        trackView.contentWidth - trackView.width,
                        previous + speed))
                    var moved = trackView.contentX - previous
                    if (moved !== 0 && root.activeDragClip) {
                        root.activeDragClip.dragPreviewX = Math.max(
                            0, root.activeDragClip.dragPreviewX + moved)
                        root.activeDragClip.dragLastContentX = trackView.contentX
                        // The clip keeps travelling here with no pointer event
                        // behind it, so the snap has to be re-solved from the
                        // timer or it would freeze at the join it found last.
                        root.updateDragSnap(root.activeDragClip)
                    }
                }
            }

            // Ruler
            Rectangle {
                id: ruler
                z: 30
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: Theme.rulerHeight
                color: Theme.bgPanel

                // Track-head spacer
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: body.laneX
                    color: Theme.bgPanel
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
                }

                // Ticks
                Repeater {
                    id: rulerTicks
                    readonly property real tickWidth:
                        Math.max(1, body.tickSubdivisionSec * root.pps)
                    readonly property int firstVisibleTick: Math.max(
                        0, Math.floor(Math.max(0, trackView.contentX - body.laneX)
                                      / tickWidth) - 2)
                    readonly property int visibleTickCount: Math.max(
                        1, Math.ceil(body.width / tickWidth) + 5)
                    // tickWidth has a 1 px floor, so a fully zoomed-out timeline
                    // asks for one tick per pixel of body width. 8192 covers an
                    // 8K display; anything past it is a zoom bug, not a ruler.
                    //
                    // The model is a capacity that only grows, in blocks, and not
                    // visibleTickCount itself. Writing a Repeater's model calls
                    // QQuickRepeater::clear() and re-incubates every delegate, and
                    // visibleTickCount moves with body.width - which changes
                    // several times inside a single SplitView layout pass. The
                    // stall tracer caught the cost as 2054 ms of "Not Responding"
                    // during startup:
                    //   QQuickWindowPrivate::polishItems -> SplitViewPrivate::layout
                    //     -> QQuickItem::setWidth -> QQuickRepeater::setModel
                    //       -> clear() -> QQmlEnginePrivate::incubate
                    // Every splitter drag and every window resize paid it too.
                    // Ticks past the visible count park themselves on the
                    // delegate's own visible binding, so the extra capacity costs
                    // one Item each and nothing to draw.
                    readonly property int tickCeiling: 8192
                    property int tickCapacity: 0
                    function growTicks() {
                        const want = Math.min(
                            rulerTicks.tickCeiling,
                            Math.ceil((rulerTicks.visibleTickCount + 8) / 64) * 64)
                        if (want > rulerTicks.tickCapacity)
                            rulerTicks.tickCapacity = want
                    }
                    onVisibleTickCountChanged: {
                        rulerTicks.growTicks()
                        // restart(), so a burst of width changes keeps pushing the
                        // shrink out; it only lands once the ruler has settled.
                        tickShrink.restart()
                    }
                    Component.onCompleted: rulerTicks.growTicks()
                    // Capacity that only grows would keep 8192 live delegates
                    // after one trip to full zoom-out. Shrinking is the one case
                    // where a rebuild is worth it, so it waits for the gesture to
                    // finish: one rebuild after the user stops, never during. The
                    // Timer is a sibling below, not a child: Repeater's default
                    // property is its delegate.
                    model: ModelGuard.bound(tickCapacity, tickCeiling,
                                            "timeline.rulerTicks")
                    delegate: Item {
                        required property int index
                        readonly property int tickIndex:
                            rulerTicks.firstVisibleTick + index
                        readonly property bool majorTick: tickIndex % 10 === 0
                        readonly property bool mediumTick: tickIndex % 10 === 5
                        x: body.laneX + tickIndex * body.tickSubdivisionSec
                           * root.pps - trackView.contentX
                        y: 0
                        width: 1
                        height: ruler.height
                        visible: x >= body.laneX && x < body.width
                        Text {
                            visible: parent.majorTick
                            x: 4
                            y: 2
                            text: root.rulerLabel(
                                      tickIndex * body.tickSubdivisionSec,
                                      body.rulerHourScale)
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsXs
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: 1
                            // A major tick runs the whole height so the label reads
                            // as belonging to the line beside it, the way it does in
                            // an NLE ruler; the halfway and minor ticks stay short.
                            height: parent.majorTick ? ruler.height - 2
                                     : parent.mediumTick ? 8 : 4
                            color: parent.majorTick ? Theme.textMuted : Theme.border
                            opacity: parent.majorTick ? 0.55
                                      : parent.mediumTick ? 1.0 : 0.6
                        }
                    }
                }

                Timer {
                    id: tickShrink
                    interval: 900
                    onTriggered: {
                        const want = Math.min(
                            rulerTicks.tickCeiling,
                            Math.ceil((rulerTicks.visibleTickCount + 8) / 64) * 64)
                        if (want < rulerTicks.tickCapacity)
                            rulerTicks.tickCapacity = want
                    }
                }

                Repeater {
                    model: Backend.markers
                    onCountChanged: ModelGuard.note("timeline.markers", count)
                    delegate: Item {
                        id: markerPin
                        required property var modelData
                        property real previewPositionMs: modelData.positionMs
                        x: body.laneX + previewPositionMs / 1000 * root.pps
                           - trackView.contentX - width / 2
                        y: 0
                        z: 20
                        width: 14
                        height: ruler.height
                        visible: x + width >= body.laneX && x <= body.width

                        Rectangle {
                            anchors.top: parent.top
                            anchors.topMargin: 3
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 9; height: 9; rotation: 45; radius: 1
                            color: markerPin.modelData.color || Theme.accent
                        }
                        Rectangle {
                            anchors.top: parent.top
                            anchors.topMargin: 8
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 2; height: ruler.height - 8
                            color: markerPin.modelData.color || Theme.accent
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            hoverEnabled: true
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                            ToolTip.visible: containsMouse && !pressed
                            ToolTip.text: (markerPin.modelData.name || "Marker")
                                          + "  " + root.fmtTC(markerPin.previewPositionMs / 1000)
                            onPressed: mouse => {
                                if (mouse.button === Qt.RightButton) {
                                    markerDialog.edit(markerPin.modelData)
                                    mouse.accepted = true
                                }
                            }
                            onPositionChanged: mouse => {
                                if (!pressed || (mouse.buttons & Qt.LeftButton) === 0)
                                    return
                                var point = mapToItem(ruler, mouse.x, mouse.y)
                                markerPin.previewPositionMs = Math.max(
                                            0, Math.min(Backend.durationMs,
                                                        Math.round((point.x - body.laneX
                                                                    + trackView.contentX)
                                                                   / root.pps * 1000)))
                            }
                            onReleased: mouse => {
                                if (mouse.button === Qt.LeftButton) {
                                    Backend.updateMarker(markerPin.modelData.id,
                                                         Math.round(markerPin.previewPositionMs),
                                                         markerPin.modelData.name || "Marker",
                                                         markerPin.modelData.color || Theme.accent)
                                    Backend.playheadMs = Math.round(markerPin.previewPositionMs)
                                }
                            }
                            onDoubleClicked: mouse => {
                                if (mouse.button === Qt.LeftButton)
                                    markerDialog.edit(markerPin.modelData)
                            }
                        }
                    }
                }

                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

                // Seek by clicking the ruler lane area
                MouseArea {
                    id: rulerScrubArea
                    z: 0
                    anchors.fill: parent
                    preventStealing: true
                    AppCursor.name: pressed ? "Fist" : "Hand"
                    onPressed: (m) => root.beginPlayheadScrub(m.x)
                    onPositionChanged: (m) => {
                        if (pressed)
                            root.seekPlayheadFromRuler(m.x)
                    }
                    onReleased: (m) => root.finishPlayheadScrub(m.x, true)
                    onCanceled: root.finishPlayheadScrub(0, false)
                    onPressedChanged: {
                        // A release outside the window or a stolen grab can
                        // skip onReleased/onCanceled on some Windows drivers.
                        if (!pressed && root.scrubbingPlayhead)
                            root.finishPlayheadScrub(0, false)
                    }
                }
            }

            Flickable {
                id: trackView
                z: 0
                anchors.top: ruler.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                contentWidth: Math.max(
                                  width,
                                  body.laneX + Math.min(
                                      root.maxTimelinePixels,
                                      root.sequenceSeconds * root.pps)
                                  + (root.fitMode ? 0 : 48))
                contentHeight: Math.max(height, root.trackContentHeight)
                onContentXChanged: Qt.callLater(root.updateTimelineClipViewport)
                onWidthChanged: Qt.callLater(root.updateTimelineClipViewport)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                Item {
                    id: trackContent
                    width: trackView.contentWidth
                    height: trackView.contentHeight
                    readonly property real lanesY: Math.max(0, (height - root.trackContentHeight) / 2)

                    Column {
                        id: tracks
                        z: 0
                        x: 0
                        y: trackContent.lanesY
                        width: trackContent.width

                        component Lane: Item {
                            id: lane
                            required property string trackId
                            required property string trackKind
                            readonly property bool audioTrack: trackKind === "audio"
                            readonly property bool subtitleTrack: trackKind === "subtitle"
                            readonly property bool effectTrack: trackKind === "effect"
                            readonly property bool occupied:
                                root.occupiedTracks[trackId] === true
                            width: tracks.width
                            height: root.trackHeight

                            Rectangle {
                                anchors.fill: parent
                                anchors.leftMargin: body.laneX
                                // The effect lane keeps its band while empty: an
                                // invisible row is not something a user can find
                                // to drop an effect on.
                                visible: lane.occupied || lane.effectTrack
                                opacity: lane.occupied ? 1 : 0.5
                                color: Theme.laneOccupied

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: Theme.laneOccupiedLine
                                }

                                Text {
                                    // Pinned to the left edge of the viewport
                                    // rather than to time 0, so the invitation is
                                    // still there after a scroll.
                                    x: 8 + trackView.contentX
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: lane.effectTrack
                                             && Backend.timelineEffects.length === 0
                                    text: "Drop an effect here to apply it over time"
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fsXs
                                }
                            }
                        }

                        Lane {
                            visible: Backend.hasSubtitleClips
                            height: visible ? root.subtitleTrackHeight : 0
                            trackId: "S1"
                            trackKind: "subtitle"
                        }
                        Lane {
                            visible: root.effectTrackCount > 0
                            height: visible ? root.effectTrackHeight : 0
                            trackId: "F1"
                            trackKind: "effect"
                        }
                        Repeater {
                            // Every lane carries the clips on it, so a track
                            // count that runs away is not a long timeline, it is
                            // a frozen one. 256 is well past any real sequence.
                            model: ModelGuard.bound(Backend.videoTrackCount, 256,
                                                    "timeline.videoLanes")
                            delegate: Lane {
                                required property int index
                                trackId: "V" + (Backend.videoTrackCount - index)
                                trackKind: "video"
                            }
                        }
                        Repeater {
                            model: ModelGuard.bound(Backend.audioTrackCount, 256,
                                                    "timeline.audioLanes")
                            delegate: Lane {
                                required property int index
                                trackId: "A" + (index + 1)
                                trackKind: "audio"
                            }
                        }
                    }

                    Item {
                        id: clipLayer
                        z: 10
                        x: body.laneX
                        y: trackContent.lanesY
                        width: trackContent.width - body.laneX
                        // Audio media dropped on a sequence that has no audio
                        // lane yet lands on A1, one row below the last lane.
                        // Lend the layer that row for the duration of the drag
                        // so the target band and the ghost clip stay visible;
                        // the lane column itself is left alone, otherwise the
                        // rows would shift under the pointer mid-gesture.
                        readonly property int pendingAudioRow:
                            mediaDropArea.containsDrag
                            && Backend.audioTrackCount === 0
                            && mediaDropArea.targetTrack.charAt(0) === "A"
                            ? root.trackHeight : 0
                        height: root.trackContentHeight + pendingAudioRow
                        clip: true

                        Rectangle {
                            z: 1
                            x: 0
                            y: root.trackContentHeight
                            width: parent.width
                            height: root.trackHeight - 1
                            visible: clipLayer.pendingAudioRow > 0
                            color: Theme.laneOccupied
                        }

                        // The effect lane's drop target. An effect dragged here
                        // from the browser becomes a timeline item of its own
                        // instead of being attached to one clip, so the extent of
                        // the bar is the stretch of the sequence it applies to.
                        // Above the bars (z 20) so a drop always makes a new one
                        // rather than being swallowed by whatever is already there.
                        DropArea {
                            id: effectLaneDropArea
                            z: 20
                            x: 0
                            y: root.subtitleTrackOffset
                            width: clipLayer.width
                            height: root.effectTrackOffset
                            keys: ["cutpro-effect"]
                            enabled: root.effectTrackCount > 0
                                     && !root.trackLocked("F1")
                            property real targetStartMs: 0

                            function updateTarget(event) {
                                targetStartMs = Math.max(
                                            0, Math.round(event.x / root.pps * 1000))
                            }

                            onEntered: drag => {
                                drag.acceptProposedAction()
                                updateTarget(drag)
                            }
                            onPositionChanged: drag => updateTarget(drag)
                            onDropped: drop => {
                                if (!drop.source || !drop.source.dragEffectId)
                                    return
                                updateTarget(drop)
                                drop.acceptProposedAction()
                                // Same reason the media drop defers: Drag.drop() is
                                // synchronous on Windows and mutating the timeline
                                // from inside the callback can deadlock Qt Quick
                                // before its timers get a turn.
                                var pendingEffectId = String(drop.source.dragEffectId)
                                var pendingStartMs = Math.round(
                                            root.snappedTime(targetStartMs, []))
                                Qt.callLater(function() {
                                    var clipId = Backend.addTimelineEffect(
                                                pendingEffectId, pendingStartMs, 0)
                                    if (clipId !== "")
                                        root.selectedClipIds = [clipId]
                                })
                            }

                            Rectangle {
                                x: effectLaneDropArea.targetStartMs / 1000 * root.pps
                                y: 3
                                width: Math.max(18, 5 * root.pps)
                                height: root.effectTrackHeight - 6
                                visible: effectLaneDropArea.containsDrag
                                radius: Theme.radiusSm
                                color: Qt.rgba(0.48, 0.36, 0.77, 0.55)
                                border.width: 1
                                border.color: Theme.accent
                            }
                        }

                        DropArea {
                            id: mediaDropArea
                            parent: body
                            anchors.fill: parent
                            // Keep the drop target above the timeline mouse
                            // layer. DropArea does not consume ordinary mouse
                            // clicks, but a negative z puts it behind the
                            // Flickable/MouseArea and prevents drag-enter on
                            // Windows.
                            z: 100
                            keys: ["cutpro-media"]
                            property string targetTrack: ""
                            property real targetStartMs: 0
                            property real previewDurationMs: 5000
                            property string previewName: ""
                            property url previewUrl: ""

                            // The target lane does not exist yet: the drop will
                            // create it. The placement call in the backend grows
                            // the track count, so nothing has to be added here
                            // while the pointer is still moving.
                            readonly property bool targetNewTrack: {
                                var prefix = mediaDropArea.targetTrack.charAt(0)
                                var number = parseInt(
                                            mediaDropArea.targetTrack.substring(1))
                                if (isNaN(number))
                                    return false
                                if (prefix === "V")
                                    return number > Backend.videoTrackCount
                                if (prefix === "A")
                                    return number > Backend.audioTrackCount
                                return false
                            }

                            // Row the target lane occupies in body coordinates,
                            // shared by the highlight band and the ghost clip.
                            // Both used to live inside the clipped clip layer,
                            // which hid them whenever the target was the new
                            // lane above the top track.
                            readonly property real rowScreenY:
                                ruler.height + trackContent.lanesY
                                - trackView.contentY
                                + (mediaDropArea.targetTrack === ""
                                   ? 0 : root.trackY(mediaDropArea.targetTrack))

                            function timelineHasMedia() {
                                for (var i = 0; i < Backend.clips.length; ++i) {
                                    if (Backend.clips[i].kind !== "subtitle")
                                        return true
                                }
                                return false
                            }

                            function updateTarget(event) {
                                var source = event.source
                                if (!source) {
                                    targetTrack = ""
                                } else {
                                    // Both coordinates are read from the
                                    // pointer: y picks the lane, x picks the
                                    // time. The lane is whatever is under the
                                    // pointer - including a new track past the
                                    // end of the stack - and only a pointer
                                    // with no compatible lane anywhere near it
                                    // falls back, to the nearest one rather
                                    // than to the first track.
                                    var laneY = event.y - ruler.height
                                                + trackView.contentY
                                                - trackContent.lanesY
                                    targetTrack = root.dropTrackForY(
                                                laneY, source.dragMediaKind)
                                }
                                targetStartMs = Math.max(
                                            0, Math.round(
                                                (event.x - body.laneX
                                                 + trackView.contentX)
                                                / root.pps * 1000))
                                previewDurationMs = source
                                        ? Math.max(250, Number(source.dragDurationMs || 5000))
                                        : 5000
                                previewName = source ? String(source.dragMediaName || "Media") : ""
                                previewUrl = source ? source.previewUrl || "" : ""
                            }

                            onEntered: (drag) => {
                                // Windows requires the target to accept the
                                // proposed action during drag-enter. Waiting
                                // until onDropped makes the drag leave the
                                // target before release, so no drop is sent.
                                drag.acceptProposedAction()
                                Backend.beginTimelineInteraction()
                                updateTarget(drag)
                            }
                            onPositionChanged: (drag) => {
                                drag.acceptProposedAction()
                                // Keeps background decoding stood down while the
                                // pointer is still moving over the tracks.
                                Backend.touchTimelineInteraction()
                                updateTarget(drag)
                            }
                            onExited: {
                                targetTrack = ""
                                previewName = ""
                                previewUrl = ""
                                Backend.endTimelineInteraction()
                            }
                            onDropped: (drop) => {
                                drop.acceptProposedAction()
                                updateTarget(drop)
                                if (!targetTrack || !drop.source)
                                    return

                                // Drag.drop() is synchronous on Windows. Making
                                // the placement overlay visible from inside this
                                // callback can deadlock Qt Quick before its timer
                                // gets a first turn. Copy primitive values, finish
                                // the drag, then mutate the timeline next tick.
                                var sourceIds = drop.source.dragMediaIds
                                                || [drop.source.dragMediaId]
                                var pendingIds = []
                                for (var i = 0; i < sourceIds.length; ++i)
                                    pendingIds.push(String(sourceIds[i]))
                                var pendingStartMs = targetStartMs
                                var pendingTrack = targetTrack
                                targetTrack = ""
                                // The gesture is over: thumbnails and waveforms
                                // may decode again for the clip that just landed.
                                Backend.endTimelineInteraction()
                                Qt.callLater(function() {
                                    // A single drop follows the same direct
                                    // backend path as the working double-click
                                    // action. The queued placement job is kept
                                    // only for multi-item drops; its progress
                                    // notifications can otherwise trigger a
                                    // large-media monitor refresh during the
                                    // native drag teardown.
                                    if (pendingIds.length === 1) {
                                        var added = Backend.addMediaToTimeline(
                                                    pendingIds[0],
                                                    pendingStartMs,
                                                    pendingTrack)
                                        if (added && added.length > 0)
                                            root.selectedClipIds = added
                                    } else {
                                        Backend.beginTimelinePlacement(
                                                    pendingIds,
                                                    pendingStartMs,
                                                    pendingTrack)
                                    }
                                })
                            }
                        }

                        MouseArea {
                            parent: trackContent
                            z: 0
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            preventStealing: true
                            AppCursor.name: root.activeTool === 4 ? "Hand"
                                            : root.activeTool === 3 ? "Razor"
                                            : root.activeTool === 5 ? "TLZoomIn"
                                            : root.activeTool === 1 ? "MultiSelectRight"
                                            : "Select"
                            onPressed: mouse => {
                                root.forceActiveFocus()
                                root.pointerInteractionActive = true
                                if (root.activeTool === 4) {
                                    root.handPanning = true
                                    root.handPressX = mouse.x
                                    root.handPressY = mouse.y
                                    root.handContentX = trackView.contentX
                                    root.handContentY = trackView.contentY
                                } else if (root.activeTool === 5) {
                                    root.setZoom(root.pps * ((mouse.modifiers & Qt.AltModifier) ? 0.7 : 1.45))
                                } else if (root.activeTool === 0) {
                                    root.selectedClipIds = []
                                    root.selectedClipId = ""
                                    // The fixed track header is not a selectable
                                    // timeline surface and must not start a marquee.
                                    if (mouse.x < body.laneX) {
                                        root.pointerInteractionActive = false
                                        return
                                    }
                                    var viewportPoint = mapToItem(trackView,
                                                                  mouse.x, mouse.y)
                                    root.marqueePointer = viewportPoint
                                    var point = mapToItem(clipLayer, mouse.x, mouse.y)
                                    root.marqueeStart = point
                                    root.marqueeEnd = root.marqueeStart
                                    root.marqueeSelecting = false
                                }
                            }
                            onPositionChanged: mouse => {
                                if (root.activeTool === 0 && pressed) {
                                    root.marqueePointer = mapToItem(trackView,
                                                                    mouse.x, mouse.y)
                                    root.marqueeEnd = mapToItem(clipLayer, mouse.x, mouse.y)
                                    root.marqueeSelecting = Math.abs(
                                                root.marqueeEnd.x - root.marqueeStart.x)
                                                + Math.abs(root.marqueeEnd.y
                                                           - root.marqueeStart.y) > 5
                                    if (root.marqueeSelecting)
                                        root.updateMarqueeSelection()
                                    return
                                }
                                if (root.activeTool !== 4 || !root.handPanning)
                                    return
                                trackView.contentX = Math.max(0, Math.min(
                                    trackView.contentWidth - trackView.width,
                                    root.handContentX - (mouse.x - root.handPressX)))
                                trackView.contentY = Math.max(0, Math.min(
                                    trackView.contentHeight - trackView.height,
                                    root.handContentY - (mouse.y - root.handPressY)))
                            }
                            onReleased: {
                                root.handPanning = false
                                root.marqueeSelecting = false
                                root.pointerInteractionActive = false
                            }
                            onCanceled: {
                                root.handPanning = false
                                root.marqueeSelecting = false
                                root.pointerInteractionActive = false
                            }
                        }

                        Rectangle {
                            parent: body
                            z: 20
                            enabled: false
                            readonly property real selectionLeft:
                                Math.min(root.marqueeStart.x, root.marqueeEnd.x)
                                + body.laneX - trackView.contentX
                            readonly property real selectionRight:
                                Math.max(root.marqueeStart.x, root.marqueeEnd.x)
                                + body.laneX - trackView.contentX
                            readonly property real selectionTop:
                                Math.min(root.marqueeStart.y, root.marqueeEnd.y)
                                + ruler.height + trackContent.lanesY
                                - trackView.contentY
                            readonly property real selectionBottom:
                                Math.max(root.marqueeStart.y, root.marqueeEnd.y)
                                + ruler.height + trackContent.lanesY
                                - trackView.contentY
                            x: Math.max(body.laneX, selectionLeft)
                            y: Math.max(ruler.height, selectionTop)
                            width: Math.max(0, Math.min(body.width, selectionRight) - x)
                            height: Math.max(0, Math.min(body.height, selectionBottom) - y)
                            visible: root.marqueeSelecting
                            color: Qt.rgba(0.29, 0.56, 0.96, 0.14)
                            border.width: 1
                            border.color: "white"
                        }

                        // Target lane highlight. Parented to the body, not to the
                        // clip layer: the clip layer is clipped to the existing
                        // lanes, so a target above the top video track - the new
                        // lane a drop up there asks for - was invisible.
                        // z sits above the tracks and below the ruler and the
                        // fixed track headers.
                        Rectangle {
                            parent: body
                            z: 25
                            enabled: false
                            x: body.laneX
                            y: mediaDropArea.rowScreenY + 1
                            width: body.width - body.laneX
                            height: root.trackHeight - 2
                            visible: mediaDropArea.containsDrag
                                     && mediaDropArea.targetTrack !== ""
                            color: Qt.rgba(0.29, 0.56, 0.96, 0.12)
                            border.width: 1
                            border.color: Theme.accent
                        }

                        Rectangle {
                            id: mediaDropPreview
                            parent: body
                            z: 26
                            enabled: false
                            readonly property real laneStartX:
                                body.laneX + mediaDropArea.targetStartMs / 1000
                                * root.pps - trackView.contentX
                            x: Math.max(body.laneX, mediaDropPreview.laneStartX)
                            y: mediaDropArea.rowScreenY + 3
                            width: Math.max(
                                       0,
                                       Math.min(body.width,
                                                mediaDropPreview.laneStartX
                                                + Math.max(
                                                    54,
                                                    mediaDropArea.previewDurationMs
                                                    / 1000 * root.pps))
                                       - mediaDropPreview.x)
                            height: root.trackHeight - 6
                            visible: mediaDropArea.containsDrag
                                     && mediaDropArea.targetTrack !== ""
                            radius: Theme.radiusSm
                            color: mediaDropArea.targetTrack.charAt(0) === "A"
                                   ? Theme.clipAudio : Theme.clipVideo
                            border.width: 2
                            border.color: "white"
                            opacity: 0.88
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: mediaDropArea.previewUrl
                                fillMode: Image.PreserveAspectCrop
                                opacity: 0.34
                                visible: source.toString() !== ""
                            }
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 7
                                anchors.right: parent.right
                                anchors.rightMargin: 7
                                anchors.verticalCenter: parent.verticalCenter
                                text: mediaDropArea.targetNewTrack
                                      ? mediaDropArea.previewName + "  →  new "
                                        + mediaDropArea.targetTrack
                                      : mediaDropArea.previewName
                                color: "white"
                                font.pixelSize: Theme.fsXs
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                            }
                        }

                        Repeater {
                            model: Backend.timelineClipModel
                            // The heaviest model in the app: every entry is a
                            // clip rectangle with a filmstrip, a waveform and a
                            // label inside it. Counted rather than capped -
                            // hiding a clip the user placed would be worse than
                            // the freeze - but the number belongs in the report.
                            onCountChanged: ModelGuard.note("timeline.clips",
                                                            count)
                            delegate: Rectangle {
                                id: clipItem
                                z: 10
                                required property var modelData
                                // A row standing for a run of clips too narrow to
                                // be seen individually. The model produces these
                                // instead of one delegate per clip, so this is
                                // drawn as a plain bar with a count and takes no
                                // part in selection, dragging or trimming - at
                                // this zoom none of those could hit one clip
                                // anyway. Zooming in shrinks the collapse
                                // threshold and the bar breaks back into clips.
                                readonly property bool isCluster:
                                    clipItem.modelData.isCluster === true
                                property bool dragAutoTrack: false
                                property bool dragAutoTrackAdded: false
                                property bool dragging: false
                                property bool dragMoved: false
                                property real dragPressX: 0
                                property real dragPressY: 0
                                property real dragPressContentX: 0
                                property real dragLastViewportX: 0
                                property real dragLastViewportY: 0
                                property real dragLastContentX: 0
                                property real dragOriginX: 0
                                property real dragOriginY: 0
                                property real dragPreviewX: 0
                                property real dragPreviewY: 0
                                // Correction from the snap solver, kept apart
                                // from dragPreviewX so the raw pointer position
                                // stays honest and the clip can walk out of a
                                // join as soon as the pointer does.
                                property real dragSnapOffsetX: 0
                                property bool trimmingStart: false
                                property bool trimmingEnd: false
                                property real previewStartMs: modelData.startMs
                                property real previewEndMs: modelData.startMs
                                                            + modelData.durationMs
                                readonly property real displayStartMs: trimmingStart
                                                                       ? previewStartMs
                                                                       : modelData.startMs
                                readonly property real displayEndMs: trimmingEnd
                                                                     ? previewEndMs
                                                                     : modelData.startMs
                                                                       + modelData.durationMs
                                readonly property real sourceDurationMs:
                                    modelData.sourceDurationMs > 0
                                    ? modelData.sourceDurationMs
                                    : modelData.sourceInMs + modelData.durationMs
                                // Visible slice of this clip, in clip-local
                                // coordinates, padded by half a screen on each
                                // side. Thumbnails outside it are never built,
                                // which is what keeps a long clip cheap at high
                                // zoom instead of one giant stretched texture.
                                readonly property real viewPad:
                                    Math.max(120, trackView.width * 0.5)
                                readonly property real viewLeft: Math.max(
                                    0, trackView.contentX - body.laneX
                                       - clipItem.x - clipItem.viewPad)
                                readonly property real viewRight: Math.min(
                                    clipItem.width,
                                    trackView.contentX - body.laneX - clipItem.x
                                    + trackView.width + clipItem.viewPad)
                                x: dragging ? dragPreviewX + dragSnapOffsetX
                                            : displayStartMs / 1000 * root.pps
                                y: dragging ? dragPreviewY
                                            : root.trackY(modelData.track) + 3
                                width: Math.max(18, (displayEndMs - displayStartMs)
                                                    / 1000 * root.pps)
                                height: modelData.kind === "subtitle"
                                        ? root.subtitleTrackHeight - 6
                                        : modelData.kind === "effect"
                                          ? root.effectTrackHeight - 6
                                          : root.trackHeight - 6
                                radius: Theme.radiusSm
                                color: modelData.kind === "subtitle"
                                       ? Theme.clipSubtitle
                                       : modelData.kind === "effect"
                                         ? Theme.clipEffect
                                         : modelData.kind === "audio"
                                           ? Theme.clipAudio : Theme.clipVideo
                                opacity: root.trackVisible(modelData.track) ? 1 : 0.35
                                border.width: root.isClipSelected(modelData.id) ? 2 : 0
                                border.color: Theme.accent

                                TimelineClipContent {
                                    anchors.fill: parent
                                    clipData: clipItem.modelData
                                    // No source behind a collapsed row, so every
                                    // density gate inside falls closed: no
                                    // filmstrip, no waveform, no thumbnail
                                    // request. What is left is the count the
                                    // model put in the payload.
                                    mediaData: clipItem.isCluster
                                               ? ({})
                                               : root.mediaForId(clipItem.modelData.mediaId)
                                    pixelsPerSecond: root.pps
                                    sourceInMs: clipItem.modelData.sourceInMs
                                                + (clipItem.displayStartMs
                                                   - clipItem.modelData.startMs)
                                    sourceDurationMs: clipItem.sourceDurationMs
                                    // Clip-local slice of the viewport, padded by
                                    // one screen so a scroll reveals ready tiles.
                                    viewLeft: clipItem.viewLeft
                                    viewRight: clipItem.viewRight
                                }

                                // Drag feedback: show a seam only when the
                                // moving clip touches a neighbor on its track.
                                Rectangle {
                                    x: -1
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 2
                                    z: 40
                                    color: "#080808"
                                    visible: root.dragTouchesClip(clipItem, "left")
                                }
                                Rectangle {
                                    x: parent.width - 1
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 2
                                    z: 40
                                    color: "#080808"
                                    visible: root.dragTouchesClip(clipItem, "right")
                                }

                                DropArea {
                                    id: effectDropArea
                                    anchors.fill: parent
                                    z: 30
                                    enabled: !clipItem.isCluster
                                    keys: ["cutpro-effect"]
                                    onEntered: drag => {
                                        if (!root.effectDropCompatible(
                                                    clipItem.modelData,
                                                    drag.source))
                                            drag.accepted = false
                                    }
                                    onDropped: drop => {
                                        if (!drop.source || !drop.source.dragEffectId)
                                            return
                                        if (!root.effectDropCompatible(
                                                    clipItem.modelData,
                                                    drop.source))
                                            return
                                        var instanceId = Backend.addClipEffect(
                                            clipItem.modelData.id,
                                            drop.source.dragEffectId)
                                        if (instanceId !== "") {
                                            root.selectClip(clipItem.modelData.id,
                                                            Qt.NoModifier)
                                            drop.acceptProposedAction()
                                        }
                                    }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    z: 31
                                    visible: effectDropArea.containsDrag
                                             && root.effectDropCompatible(
                                                 clipItem.modelData,
                                                 effectDropArea.drag.source)
                                    color: Qt.rgba(1, 1, 1, 0.08)
                                    border.width: 2
                                    border.color: Theme.accent
                                    radius: clipItem.radius
                                }

                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    enabled: !clipItem.isCluster
                                    onTapped: {
                                        if (!root.isClipSelected(clipItem.modelData.id))
                                            root.selectClip(clipItem.modelData.id, Qt.NoModifier)
                                        Backend.selectedClipId = clipItem.modelData.id
                                        if (clipItem.modelData.kind === "subtitle") {
                                            subtitleContextMenu.subtitleId = clipItem.modelData.id
                                            subtitleContextMenu.popup()
                                        } else {
                                            clipContextMenu.clipData = clipItem.modelData
                                            clipContextMenu.mediaData = root.mediaForId(
                                                        clipItem.modelData.mediaId)
                                            clipContextMenu.popup()
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    // A collapsed bar is not a clip: it cannot be
                                    // selected, cut or dragged, and letting the
                                    // press through means a click there still
                                    // moves the playhead like a click on the lane.
                                    enabled: !clipItem.isCluster
                                    AppCursor.name: root.activeTool === 3 ? "Razor"
                                                    : root.activeTool === 4 ? "Hand"
                                                    : root.activeTool === 5 ? "TLZoomIn"
                                                    : root.activeTool === 1 ? "MultiSelectRight"
                                                    : clipItem.dragging ? "Fist" : "Hand"
                                    preventStealing: true
                                    onPressed: (mouse) => {
                                        root.forceActiveFocus()
                                        root.pointerInteractionActive = true
                                        var point = mapToItem(clipLayer, mouse.x, mouse.y)
                                        var position = Math.round(point.x / root.pps * 1000)
                                        if (root.activeTool === 3) {
                                            root.razorAt(position)
                                            return
                                        }
                                        if (root.activeTool === 1) {
                                            root.selectTrackForward(clipItem.modelData,
                                                                    (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                            return
                                        }
                                        if (root.activeTool === 4) {
                                            root.handPanning = true
                                            root.handPressX = mouse.x
                                            root.handPressY = mouse.y
                                            root.handContentX = trackView.contentX
                                            root.handContentY = trackView.contentY
                                            return
                                        }
                                        if (root.activeTool === 5) {
                                            root.setZoom(root.pps * ((mouse.modifiers & Qt.AltModifier) ? 0.7 : 1.45))
                                            return
                                        }
                                        var noModifier = mouse.modifiers === Qt.NoModifier
                                        if (!(noModifier
                                              && root.isClipSelected(clipItem.modelData.id)))
                                            root.selectClip(clipItem.modelData.id, mouse.modifiers)
                                        else
                                            root.selectedClipId = clipItem.modelData.id
                                        clipItem.dragAutoTrack = true
                                        clipItem.dragAutoTrackAdded = false
                                        clipItem.dragging = false
                                        clipItem.dragMoved = false
                                        var viewportPoint = mapToItem(
                                                    trackView, mouse.x, mouse.y)
                                        clipItem.dragPressX = viewportPoint.x
                                        clipItem.dragPressY = viewportPoint.y
                                        clipItem.dragPressContentX = trackView.contentX
                                        clipItem.dragLastViewportX = viewportPoint.x
                                        clipItem.dragLastViewportY = viewportPoint.y
                                        clipItem.dragLastContentX = trackView.contentX
                                        clipItem.dragOriginX = clipItem.x
                                        clipItem.dragOriginY = clipItem.y
                                        clipItem.dragPreviewX = clipItem.x
                                        clipItem.dragPreviewY = clipItem.y
                                    }
                                    onClicked: (mouse) => {
                                        if (root.activeTool !== 0 && root.activeTool !== 2)
                                            return
                                        if (clipItem.dragMoved)
                                            return
                                        if (mouse.modifiers === Qt.NoModifier
                                                && root.selectedClipIds.length > 1)
                                            root.selectClip(clipItem.modelData.id,
                                                            Qt.NoModifier)
                                    }
                                    onPositionChanged: mouse => {
                                        root.clipDragPointer = mapToItem(
                                                    trackView, mouse.x, mouse.y)
                                        if (root.activeTool === 4 && root.handPanning) {
                                            trackView.contentX = Math.max(0, Math.min(
                                                trackView.contentWidth - trackView.width,
                                                root.handContentX - (mouse.x - root.handPressX)))
                                            trackView.contentY = Math.max(0, Math.min(
                                                trackView.contentHeight - trackView.height,
                                                root.handContentY - (mouse.y - root.handPressY)))
                                        } else if (pressed
                                                   && (root.activeTool === 0
                                                       || root.activeTool === 2)
                                                   && !root.trackLocked(
                                                       clipItem.modelData.track)) {
                                            // Map through the moving MouseArea
                                            // into the fixed viewport. The result
                                            // is the real screen pointer position,
                                            // independent of clipItem.x/y.
                                            var point = mapToItem(trackView,
                                                                  mouse.x, mouse.y)
                                            var deltaX = (point.x
                                                          - clipItem.dragPressX)
                                                    + (trackView.contentX
                                                       - clipItem.dragPressContentX)
                                            var deltaY = point.y - clipItem.dragPressY
                                            if (!clipItem.dragging
                                                    && Math.abs(deltaX)
                                                       + Math.abs(deltaY) >= 4) {
                                                clipItem.dragging = true
                                                clipItem.dragMoved = true
                                                root.activeDragClip = clipItem
                                                // Editing and playback must not
                                                // fight over horizontal scroll.
                                                if (Backend.playing)
                                                    Backend.playing = false
                                                clipItem.dragLastViewportX = point.x
                                                clipItem.dragLastViewportY = point.y
                                                clipItem.dragLastContentX = trackView.contentX
                                            }
                                            if (!clipItem.dragging)
                                                return
                                            clipItem.dragPreviewX = Math.max(0,
                                                clipItem.dragPreviewX
                                                + (point.x
                                                   - clipItem.dragLastViewportX)
                                                + (trackView.contentX
                                                   - clipItem.dragLastContentX))
                                            clipItem.dragPreviewY = Math.max(0,
                                                clipItem.dragPreviewY
                                                + (point.y
                                                   - clipItem.dragLastViewportY))
                                            clipItem.dragLastViewportX = point.x
                                            clipItem.dragLastViewportY = point.y
                                            clipItem.dragLastContentX = trackView.contentX
                                            if (clipItem.modelData.kind === "subtitle") {
                                                clipItem.dragPreviewY = 3
                                            } else if (clipItem.modelData.kind === "effect") {
                                                // One effect lane, so the bar only
                                                // travels in time.
                                                clipItem.dragPreviewY =
                                                        root.subtitleTrackOffset + 3
                                            } else {
                                                var minimumY = clipItem.modelData.kind === "audio"
                                                        ? root.overlayTrackOffset
                                                          + Backend.videoTrackCount
                                                            * root.trackHeight
                                                        : root.overlayTrackOffset
                                                var maximumY = clipItem.modelData.kind === "audio"
                                                        ? root.trackContentHeight
                                                          - clipItem.height
                                                        : root.overlayTrackOffset
                                                          + Backend.videoTrackCount
                                                            * root.trackHeight
                                                          - clipItem.height
                                                clipItem.dragPreviewY = Math.max(
                                                    minimumY, Math.min(maximumY,
                                                        clipItem.dragPreviewY))
                                            }
                                            root.maybeCreateTrackForDrag(clipItem)
                                            root.updateDragSnap(clipItem)
                                        }
                                    }
                                    onReleased: {
                                        root.pointerInteractionActive = false
                                        root.activeDragClip = null
                                        // The guide belongs to the press, so it
                                        // goes away here whichever of the paths
                                        // below this release takes.
                                        root.snapGuideActive = false
                                        if (root.activeTool === 4) {
                                            root.handPanning = false
                                            return
                                        }
                                        if (root.activeTool !== 0 && root.activeTool !== 2)
                                            return
                                        clipItem.dragAutoTrack = false
                                        if (!clipItem.dragging
                                                || root.trackLocked(
                                                    clipItem.modelData.track)) {
                                            clipItem.dragSnapOffsetX = 0
                                            clipItem.dragging = false
                                            return
                                        }
                                        // Commit what was on screen, snap and
                                        // all, so the clip lands on the join the
                                        // guide was standing on.
                                        var targetX = clipItem.dragPreviewX
                                                      + clipItem.dragSnapOffsetX
                                        var targetY = clipItem.dragPreviewY
                                        var track = root.trackForY(
                                            targetY + clipItem.height / 2)
                                        var compatible = root.trackAcceptsKind(track,
                                                                               clipItem.modelData.kind)
                                        if (compatible)
                                            root.moveSelectedClip(clipItem.modelData,
                                                                  Math.round(targetX / root.pps * 1000),
                                                                  track, true)
                                        clipItem.dragSnapOffsetX = 0
                                        clipItem.dragging = false
                                    }
                                    onCanceled: {
                                        root.pointerInteractionActive = false
                                        root.activeDragClip = null
                                        root.handPanning = false
                                        clipItem.dragAutoTrack = false
                                        root.clearDragSnap(clipItem)
                                        clipItem.dragging = false
                                    }
                                    onPressedChanged: {
                                        if (!pressed
                                                && root.pointerInteractionActive) {
                                            root.pointerInteractionActive = false
                                            root.activeDragClip = null
                                            root.handPanning = false
                                            root.snapGuideActive = false
                                        }
                                    }
                                }

                                MouseArea {
                                    id: trimStartArea
                                    z: 40
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: Math.min(12, parent.width / 2)
                                    enabled: !clipItem.isCluster
                                             && (root.activeTool === 0 || root.activeTool === 2)
                                             && !root.trackLocked(clipItem.modelData.track)
                                    hoverEnabled: true
                                    AppCursor.name: root.activeTool === 2 ? "RippleHead"
                                                                          : "TrimHead"
                                    preventStealing: true
                                    // No tooltip and no glyph drawn at the
                                    // pointer: the TrimHead cursor is already
                                    // there, and a second trim symbol on the
                                    // same pixels reads as one broken icon.

                                    onPressed: mouse => {
                                        root.forceActiveFocus()
                                        root.pointerInteractionActive = true
                                        clipItem.previewStartMs = clipItem.modelData.startMs
                                        clipItem.previewEndMs = clipItem.modelData.startMs
                                                                + clipItem.modelData.durationMs
                                        clipItem.trimmingStart = true
                                        mouse.accepted = true
                                    }
                                    onPositionChanged: mouse => {
                                        if (!pressed)
                                            return
                                        var point = mapToItem(clipLayer, mouse.x, mouse.y)
                                        var requested = Math.round(point.x / root.pps * 1000)
                                        // An effect bar has no source footage
                                        // behind it, so its head is free: it can be
                                        // pulled back to the head of the sequence.
                                        var minimum = clipItem.modelData.kind === "effect"
                                                      ? 0
                                                      : Math.max(0,
                                                               clipItem.modelData.startMs
                                                               - clipItem.modelData.sourceInMs)
                                        clipItem.previewStartMs = Math.max(
                                                    minimum,
                                                    Math.min(clipItem.previewEndMs - 1,
                                                             requested))
                                    }
                                    onReleased: {
                                        Backend.trimClipStart(clipItem.modelData.id,
                                                              root.snappedTime(Math.round(clipItem.previewStartMs),
                                                                               [clipItem.modelData.id]))
                                        clipItem.trimmingStart = false
                                        root.pointerInteractionActive = false
                                    }
                                    onCanceled: {
                                        clipItem.trimmingStart = false
                                        root.pointerInteractionActive = false
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: 3
                                        color: "#ff536c"
                                        visible: root.isClipSelected(clipItem.modelData.id)
                                                 || trimStartArea.containsMouse
                                    }
                                    // No glyph drawn at the pointer here: the
                                    // TrimHead cursor is already under it, and
                                    // two trim symbols on the same pixels read
                                    // as one broken icon.
                                }

                                MouseArea {
                                    id: trimEndArea
                                    z: 40
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: Math.min(12, parent.width / 2)
                                    enabled: !clipItem.isCluster
                                             && (root.activeTool === 0 || root.activeTool === 2)
                                             && !root.trackLocked(clipItem.modelData.track)
                                    hoverEnabled: true
                                    AppCursor.name: root.activeTool === 2 ? "RippleTail"
                                                                          : "TrimTail"
                                    preventStealing: true

                                    onPressed: mouse => {
                                        root.forceActiveFocus()
                                        root.pointerInteractionActive = true
                                        clipItem.previewStartMs = clipItem.modelData.startMs
                                        clipItem.previewEndMs = clipItem.modelData.startMs
                                                                + clipItem.modelData.durationMs
                                        clipItem.trimmingEnd = true
                                        mouse.accepted = true
                                    }
                                    onPositionChanged: mouse => {
                                        if (!pressed)
                                            return
                                        var point = mapToItem(clipLayer, mouse.x, mouse.y)
                                        var requested = Math.round(point.x / root.pps * 1000)
                                        // Nothing runs out at the end of an effect
                                        // bar, so the tail is free. The 24 h stop is
                                        // the same one the backend applies.
                                        var maximum = clipItem.modelData.kind === "effect"
                                                      ? 86400000
                                                      : clipItem.modelData.startMs
                                                        + clipItem.sourceDurationMs
                                                        - clipItem.modelData.sourceInMs
                                        clipItem.previewEndMs = Math.min(
                                                    maximum,
                                                    Math.max(clipItem.previewStartMs + 1,
                                                             requested))
                                    }
                                    onReleased: {
                                        if (root.activeTool === 2)
                                            Backend.rippleTrimClipEnd(clipItem.modelData.id,
                                                                       root.snappedTime(Math.round(clipItem.previewEndMs),
                                                                                        [clipItem.modelData.id]))
                                        else
                                            Backend.trimClipEnd(clipItem.modelData.id,
                                                                root.snappedTime(Math.round(clipItem.previewEndMs),
                                                                                 [clipItem.modelData.id]))
                                        clipItem.trimmingEnd = false
                                        root.pointerInteractionActive = false
                                    }
                                    onCanceled: {
                                        clipItem.trimmingEnd = false
                                        root.pointerInteractionActive = false
                                    }

                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: 3
                                        color: "#ff536c"
                                        visible: root.isClipSelected(clipItem.modelData.id)
                                                 || trimEndArea.containsMouse
                                    }
                                }
                            }
                        }
                    }

                }
            }

            Rectangle {
                id: viewportPlayheadLine
                z: 140
                x: Math.round(body.playheadX - trackView.contentX)
                y: 0
                width: 2
                height: trackView.height + ruler.height
                color: Qt.rgba(0.29, 0.56, 0.96, 0.9)
                visible: x >= body.laneX && x <= body.width
            }

            // Snap guide. Viewport space beside the playhead rather than a child
            // of the dragged clip, because the join it names belongs to the
            // sequence and has to be readable across every track at once. It
            // sits just under the playhead so the two never trade places.
            Rectangle {
                id: snapGuideLine
                z: 139
                x: Math.round(body.laneX
                              + root.snapGuideMs / 1000 * root.pps
                              - trackView.contentX)
                y: ruler.height
                width: 2
                height: trackView.height
                color: Theme.snapGuide
                visible: root.snapGuideActive
                         && x >= body.laneX && x <= body.width
            }

            // Keep the playhead head above the ruler, clips, and fixed headers.
            // It is a sibling overlay instead of a child of the ruler so the
            // track-header stacking context cannot hide it at the lane edge.
            Canvas {
                id: flag
                z: 200
                width: 14
                height: 15
                x: Math.round(body.playheadX - trackView.contentX - width / 2)
                y: 0
                visible: x + width >= body.laneX && x <= body.width
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = Theme.accent
                    ctx.beginPath()
                    ctx.moveTo(0, 0)
                    ctx.lineTo(width, 0)
                    ctx.lineTo(width, 9)
                    ctx.lineTo(width / 2, height)
                    ctx.lineTo(0, 9)
                    ctx.closePath()
                    ctx.fill()
                }
            }

            Item {
                id: fixedTrackHeaders
                z: 150
                x: 0
                y: ruler.height
                width: body.laneX
                height: body.height - ruler.height
                clip: true

                Column {
                    y: trackContent.lanesY - trackView.contentY
                    width: parent.width

                    component Header: Rectangle {
                        id: header
                        required property string trackId
                        required property string trackKind
                        readonly property bool audioTrack: trackKind === "audio"
                        readonly property bool videoTrack: trackKind === "video"
                        readonly property bool subtitleTrack: trackKind === "subtitle"
                        readonly property bool effectTrack: trackKind === "effect"
                        width: fixedTrackHeaders.width
                        height: root.trackHeight
                        color: Theme.bgPanel

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 6
                            spacing: 2
                            Text {
                                text: header.trackId
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsSm
                                font.weight: Font.DemiBold
                                Layout.preferredWidth: 24
                            }
                            Image {
                                visible: header.subtitleTrack || header.effectTrack
                                source: header.effectTrack
                                        ? "../../assets/icons/sliders-horizontal.svg"
                                        : "../../assets/icons/subtitles.svg"
                                sourceSize.width: 14
                                sourceSize.height: 14
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                                opacity: 0.8
                            }
                            Item { Layout.fillWidth: true }
                            IconButton {
                                visible: header.videoTrack || header.effectTrack
                                iconName: root.trackVisible(header.trackId)
                                          ? "eye" : "eye-off"
                                boxSize: 20
                                glyphSize: 12
                                restColor: root.trackVisible(header.trackId)
                                           ? Theme.textMuted : Theme.textPrimary
                                onClicked: root.toggleVisible(header.trackId)
                                ToolTip.visible: hovered
                                ToolTip.text: (root.trackVisible(header.trackId)
                                               ? "Hide " : "Show ")
                                              + (header.effectTrack ? "effect track"
                                                                    : "video track")
                            }
                            IconButton {
                                visible: !header.subtitleTrack && !header.effectTrack
                                readonly property bool trackMuted:
                                    Backend.mutedTracks.indexOf(header.trackId) >= 0
                                iconName: trackMuted ? "volume-x" : "volume-2"
                                boxSize: 20
                                glyphSize: 12
                                restColor: trackMuted ? Theme.danger : Theme.textMuted
                                onClicked: Backend.setTrackMuted(header.trackId,
                                                                 !trackMuted)
                                ToolTip.visible: hovered
                                ToolTip.text: trackMuted ? "Unmute track" : "Mute track"
                            }
                            IconButton {
                                iconName: root.trackLocked(header.trackId)
                                          ? "lock" : "unlock"
                                boxSize: 20
                                glyphSize: 12
                                restColor: root.trackLocked(header.trackId)
                                           ? Theme.accent : Theme.textMuted
                                onClicked: root.toggleLocked(header.trackId)
                                ToolTip.visible: hovered
                                ToolTip.text: root.trackLocked(header.trackId)
                                              ? "Unlock track" : "Lock track"
                            }

                        }
                    }

                    Header {
                        visible: Backend.hasSubtitleClips
                        height: visible ? root.subtitleTrackHeight : 0
                        trackId: "S1"
                        trackKind: "subtitle"
                    }
                    Header {
                        visible: root.effectTrackCount > 0
                        height: visible ? root.effectTrackHeight : 0
                        trackId: "F1"
                        trackKind: "effect"
                    }
                    Repeater {
                        model: ModelGuard.bound(Backend.videoTrackCount, 256,
                                                "timeline.videoHeaders")
                        delegate: Header {
                            required property int index
                            trackId: "V" + (Backend.videoTrackCount - index)
                            trackKind: "video"
                        }
                    }
                    Repeater {
                        model: ModelGuard.bound(Backend.audioTrackCount, 256,
                                                "timeline.audioHeaders")
                        delegate: Header {
                            required property int index
                            trackId: "A" + (index + 1)
                            trackKind: "audio"
                        }
                    }
                }
            }

            // Empty hint over the lane area
            Text {
                x: body.laneX + (body.laneW - implicitWidth) / 2
                y: ruler.height + (body.height - ruler.height - implicitHeight) / 2
                text: "Drag media onto a track to start a sequence."
                visible: Backend.clips.length === 0
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
        }
    }
}
