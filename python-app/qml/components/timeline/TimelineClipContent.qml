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

// What is drawn inside a timeline clip. This file decides *which* layers a clip
// earns at the current zoom; the layers themselves live in
// TimelineClipFilmstrip / TimelineClipWaveform / TimelineClipLabel.
//
// The density rules are the point. A clip 25px wide cannot show 12 thumbnails,
// a name, and a waveform — attempting all three is what makes a zoomed-out
// timeline look like noise. Each layer has a width (or height) it must clear.
Item {
    id: root

    required property var clipData
    required property var mediaData
    required property real pixelsPerSecond
    required property real sourceInMs
    required property real sourceDurationMs
    // Visible slice of this clip, in clip-local coordinates. The timeline
    // narrows this to the viewport so off-screen thumbnails are never built.
    property real viewLeft: 0
    property real viewRight: root.width

    readonly property string kind: String(root.clipData.kind || "video")
    readonly property bool isAudio: root.kind === "audio"
    readonly property bool isSubtitle: root.kind === "subtitle"
    // An effect-track bar. No media behind it, so none of the media layers can
    // draw: what it gets instead is a glyph and the effect's name.
    readonly property bool isEffect: root.kind === "effect"
    // Long/large sources still draw a filmstrip and a waveform - both are now
    // produced by seeking rather than by decoding the whole file, so length is no
    // longer a reason to leave a clip blank. What "lightweight" still buys is the
    // deferred monitor load and the bounded preview decode.
    readonly property bool lightweight:
        String(root.clipData.timelineRenderMode || "normal") === "lightweight"
        || Number(root.clipData.durationMs || 0) >= 1800000
        || Number(root.mediaData ? root.mediaData.sizeBytes || 0 : 0) >= 1073741824

    readonly property string mediaPath:
        root.mediaData ? String(root.mediaData.path || "") : ""
    // On-demand thumbnails, for zoom levels the cached sheet is too coarse for.
    // Requested once per media path; the backend hands back the same handle for
    // every clip cut from the same source.
    readonly property string tileToken:
        !root.isAudio && !root.isSubtitle && !root.mediaIsImage
        && root.mediaPath !== "" && Backend.timelineTilesAvailable()
        ? Backend.timelineTileToken(root.mediaPath) : ""
    // On-demand waveform windows, for zoom levels the whole-file sheet smears.
    // Gated on the source actually having audio: asking for a window of a
    // video-only file would be an open per visible window for no picture.
    readonly property bool mediaHasAudio:
        root.isAudio
        || Number(root.mediaData ? (root.mediaData.channels || 0) : 0) > 0
    readonly property string waveToken:
        !root.isSubtitle && !root.mediaIsImage && root.mediaHasAudio
        && root.mediaPath !== "" && Backend.waveformWindowsAvailable()
        ? Backend.waveformWindowToken(root.mediaPath) : ""

    readonly property string filmstripUrl:
        root.mediaData ? String(root.mediaData.timelineThumbnailUrl || "") : ""
    readonly property string waveformUrl:
        root.mediaData ? String(root.mediaData.waveformUrl || "") : ""
    // Projects saved before the cell layout was reported carry no frame fields.
    // Their cached sheets are the same "filmstrip-v1" 12x(160x90) tiles, so
    // those values are the correct fallback; a still is always a single cell.
    readonly property bool mediaIsImage:
        root.mediaData && String(root.mediaData.kind || "") === "image"
    readonly property int frameCount: {
        var declared = Number(root.mediaData ? (root.mediaData.filmstripFrames || 0) : 0)
        if (declared > 0)
            return declared
        return root.mediaIsImage ? 1 : 12
    }
    readonly property real frameAspect: {
        var w = Number(root.mediaData ? (root.mediaData.filmstripFrameWidth || 0) : 0)
        var h = Number(root.mediaData ? (root.mediaData.filmstripFrameHeight || 0) : 0)
        if (w > 0 && h > 0)
            return w / h
        // A still fills its slot at the media's own aspect, not a tile's.
        var mw = Number(root.mediaData ? (root.mediaData.width || 0) : 0)
        var mh = Number(root.mediaData ? (root.mediaData.height || 0) : 0)
        return root.mediaIsImage && mw > 0 && mh > 0 ? mw / mh : 16 / 9
    }
    readonly property real mediaDurationMs:
        Number(root.mediaData ? (root.mediaData.durationMs || 0) : 0)
    // Names this clip's two prefetch wishes. The prefetcher round-robins between
    // requesters, so one key per clip per layer is what stops a long source from
    // finishing its whole strip before its neighbours get a single thumbnail. It
    // is also the handle each layer cancels when it goes away.
    readonly property string clipKey: String(root.clipData.id || "")

    // ---- Density gates --------------------------------------------------
    readonly property bool showFilmstrip: !root.isAudio && !root.isSubtitle
                                          && (root.filmstripUrl !== ""
                                              || root.tileToken !== "")
                                          && root.frameCount > 0
                                          && root.width >= Theme.clipMinThumbWidth
    // Either layer is enough to earn the row: a source whose sheet has not been
    // built yet - or whose cache was cleared - still draws a waveform from the
    // on-demand windows, which is the whole point of having them.
    readonly property bool showWaveform: (root.waveformUrl !== ""
                                          || root.waveToken !== "")
                                         && !root.isSubtitle
                                         // Extract Audio moved this clip's sound
                                         // to its own lane; the strip belongs
                                         // there now, not here.
                                         && root.clipData.separateAudio !== true
                                         && (root.isAudio
                                             || root.height >= Theme.clipMinWaveformSpace)
    readonly property bool showLabel: root.width >= Theme.clipMinLabelWidth

    clip: true

    // Base wash. On video clips this is what shows through when the clip is too
    // narrow for thumbnails, so it carries the clip color from the parent.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.02, 0.04, 0.06,
                       root.isAudio ? 0.08
                                    : root.isSubtitle ? 0.30
                                                      : root.isEffect ? 0.16 : 0.05)
    }

    TimelineClipFilmstrip {
        anchors.fill: parent
        visible: root.showFilmstrip
        sourceUrl: root.showFilmstrip ? root.filmstripUrl : ""
        pixelsPerSecond: root.pixelsPerSecond
        sourceInMs: root.sourceInMs
        sourceDurationMs: root.sourceDurationMs
        frameCount: Math.max(1, root.frameCount)
        frameAspect: root.frameAspect
        viewLeft: root.viewLeft
        viewRight: root.viewRight
        tileToken: root.showFilmstrip ? root.tileToken : ""
        // A still has one frame and no zoom level makes a second one appear.
        preferTiles: !root.mediaIsImage
        requesterKey: root.clipKey !== "" ? "strip:" + root.clipKey : ""
    }

    // Darken the strip so the name plate and waveform stay readable over it.
    Rectangle {
        anchors.fill: parent
        visible: root.showFilmstrip
        color: Qt.rgba(0, 0, 0, 0.12)
    }

    TimelineClipWaveform {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.isAudio ? 3 : 2
        height: root.isAudio ? Math.max(1, parent.height - 6)
                             : Theme.clipWaveformHeight
        visible: root.showWaveform
        opacity: root.isAudio ? 0.95 : 0.66
        sourceUrl: root.showWaveform ? root.waveformUrl : ""
        waveToken: root.showWaveform ? root.waveToken : ""
        pixelsPerSecond: root.pixelsPerSecond
        sourceInMs: root.sourceInMs
        mediaDurationMs: root.mediaDurationMs > 0 ? root.mediaDurationMs
                                                  : root.sourceDurationMs
        viewLeft: root.viewLeft
        viewRight: root.viewRight
        requesterKey: root.clipKey !== "" ? "wave:" + root.clipKey : ""
    }

    Image {
        id: subtitleIcon
        visible: root.isSubtitle && root.width >= Theme.clipMinThumbWidth
        anchors.left: parent.left
        anchors.leftMargin: 5
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        source: "../../assets/icons/subtitles.svg"
        opacity: 0.9
    }

    // Subtitles show their text wrapped over the whole clip, not a name plate.
    Text {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 6
        anchors.topMargin: 4
        visible: root.isSubtitle
        text: root.isSubtitle ? String(root.clipData.text || "") : ""
        color: "#191919"
        font.pixelSize: Theme.fsXs
        font.weight: Font.Medium
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        elide: Text.ElideRight
    }

    Image {
        id: effectIcon
        visible: root.isEffect && root.width >= 26
        anchors.left: parent.left
        anchors.leftMargin: 5
        anchors.verticalCenter: parent.verticalCenter
        width: 13
        height: 13
        source: "../../assets/icons/sliders-horizontal.svg"
        opacity: 0.95
    }

    // The effect's name on one line, the way a short bar can carry it. It follows
    // the horizontal scroll like a clip's name plate, so a bar trimmed to cover a
    // long stretch still says what it is wherever the user is looking.
    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 22 + Math.max(0, Math.min(root.width - 40, root.viewLeft))
        visible: root.isEffect && root.width >= Theme.clipMinLabelWidth
        text: root.isEffect ? String(root.clipData.name || "Effect") : ""
        color: "#f4f1ff"
        font.pixelSize: Theme.fsXs
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    TimelineClipLabel {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        // Follow the clip horizontally so the name stays on screen while a long
        // clip is scrolled, the way Premiere pins it.
        leftInset: 6 + Math.max(0, Math.min(root.width - 40, root.viewLeft))
        visible: root.showLabel && !root.isSubtitle && !root.isEffect
        label: String(root.clipData.name || "")
        overThumbnail: root.showFilmstrip
    }
}
