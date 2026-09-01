pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

Rectangle {
    id: root
    color: Theme.viewer
    clip: true

    property real frameRate: 30
    property bool audioEnabled: true
    property bool captionsVisible: true
    property bool scrubbing: false
    property bool resumeAfterScrub: false
    property double lastTickMs: 0
    property var trackStateList: Backend.trackStates

    readonly property bool playing: Backend.playing
    readonly property var activeClip: clipAt(Backend.playheadMs)
    readonly property string activeClipId: activeClip ? String(activeClip.id) : ""
    readonly property var activeMedia: mediaForClip(activeClip)
    readonly property var activeAudioClip: audioClipFor(activeClip)
    readonly property var activeSubtitle: subtitleAt(Backend.playheadMs)
    readonly property url sourceUrl: mediaUrl(activeMedia)

    function trackHasSolo(prefix) {
        for (var i = 0; i < trackStateList.length; ++i) {
            var state = trackStateList[i]
            if (String(state.id).charAt(0) === prefix && state.solo)
                return true
        }
        return false
    }

    function trackEnabled(track) {
        var id = String(track || "")
        var state = null
        for (var i = 0; i < trackStateList.length; ++i) {
            if (String(trackStateList[i].id) === id) {
                state = trackStateList[i]
                break
            }
        }
        if (state && state.visible === false)
            return false
        var prefix = id.charAt(0)
        return !trackHasSolo(prefix) || (state && state.solo === true)
    }

    // Base picture selection. Mirrors VideoPreviewHelper::rebuildIndexes in C++:
    // a real video outranks an image on any track, so an image on an upper video
    // track no longer replaces the video beneath it. It is composited on top by
    // the overlay layer instead, which is what the render actually does - the
    // export builder stacks every video-track clip lowest-track-first and
    // positions each one from its intrinsic effects.
    function clipAt(position) {
        var selected = null
        var selectedRank = -1
        var clips = Backend.mediaClips
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            if (clip.enabled === false
                    || !trackEnabled(clip.track)
                    || position < Number(clip.startMs)
                    || position >= Number(clip.startMs) + Number(clip.durationMs))
                continue

            var isVideo = String(clip.track).charAt(0) === "V"
            var lane = isVideo ? (parseInt(String(clip.track).slice(1)) || 0) : 0
            var rank = !isVideo ? 0
                     : clip.kind === "image" ? 1000 + lane : 2000 + lane
            if (rank > selectedRank) {
                selected = clip
                selectedRank = rank
            }
        }
        return selected
    }

    // Image clips that composite over the base picture: logos added via + Logo
    // and plain images sitting on a video track above the base. Everything at or
    // below the base is already covered by it, exactly as the render stacks them.
    readonly property var overlayClips: {
        var out = []
        var clips = Backend.mediaClips
        var position = Backend.playheadMs
        var base = root.activeClip
        var baseId = base ? String(base.id) : ""
        var baseLane = (base && String(base.track).charAt(0) === "V")
                       ? (parseInt(String(base.track).slice(1)) || 0) : 0
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            if (clip.enabled === false || !trackEnabled(clip.track))
                continue
            if (position < Number(clip.startMs)
                    || position >= Number(clip.startMs) + Number(clip.durationMs))
                continue
            if (String(clip.id) === baseId)
                continue
            var isLogo = clip.overlay === true
            if (!isLogo && clip.kind !== "image")
                continue
            if (!isLogo) {
                var lane = String(clip.track).charAt(0) === "V"
                           ? (parseInt(String(clip.track).slice(1)) || 0) : 0
                if (lane <= baseLane)
                    continue
            }
            out.push(clip)
        }
        return out
    }

    function subtitleAt(position) {
        var clips = Backend.clips
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            if (clip.kind === "subtitle" && clip.enabled !== false
                    && trackEnabled(clip.track)
                    && position >= Number(clip.startMs)
                    && position < Number(clip.startMs) + Number(clip.durationMs))
                return clip
        }
        return null
    }

    function mediaForClip(clip) {
        if (!clip)
            return null
        var media = Backend.mediaById(String(clip.mediaId || ""))
        return media && media.id ? media : null
    }

    function audioClipFor(clip) {
        // After Extract Audio the sound is an independent clip that names the
        // video it came out of; the video clip only carries the separateAudio
        // flag, and nothing links the two back.
        if (!clip || clip.kind === "audio" || clip.separateAudio !== true)
            return clip
        var clips = Backend.mediaClips
        for (var i = 0; i < clips.length; ++i) {
            var candidate = clips[i]
            if (String(candidate.extractedFromClipId || "") === String(clip.id))
                return candidate
        }
        return clip
    }

    function audioEffectValue(key, fallback) {
        var effects = activeAudioClip && activeAudioClip.effects
                ? activeAudioClip.effects : ({})
        var value = effects[key]
        return value === undefined || value === null ? fallback : Number(value)
    }

    function mediaUrl(media) {
        if (!media || !media.path)
            return ""
        var path = String(media.path).replace(/\\/g, "/")
        return path.charAt(0) === "/"
                ? "file://" + encodeURI(path)
                : "file:///" + encodeURI(path)
    }

    function timecode(milliseconds) {
        var fps = Math.max(1, Number(frameRate) || 30)
        var value = Math.max(0, Number(milliseconds) || 0)
        var totalSeconds = Math.floor(value / 1000)
        var frames = Math.floor((value % 1000) * fps / 1000)
        var seconds = totalSeconds % 60
        var minutes = Math.floor(totalSeconds / 60) % 60
        var hours = Math.floor(totalSeconds / 3600)
        function pad(number) { return number < 10 ? "0" + number : String(number) }
        return pad(hours) + ":" + pad(minutes) + ":" + pad(seconds)
                + ":" + pad(frames)
    }

    function sourcePosition() {
        if (!activeClip)
            return 0
        return Math.max(0, Backend.playheadMs - Number(activeClip.startMs)
                        + Number(activeClip.sourceInMs || 0))
    }

    function audioMuted() {
        return !audioEnabled || Backend.appSettings.muteAllAudio === true
                || (activeAudioClip
                    && (Backend.mutedTracks.indexOf(
                            String(activeAudioClip.track)) >= 0
                        || !trackEnabled(activeAudioClip.track)))
    }

    function previewVolume() {
        return Math.max(0, Math.min(1,
                    (Backend.appSettings.masterVolume === undefined
                     ? 100 : Number(Backend.appSettings.masterVolume)) / 100
                    * Math.pow(10, audioEffectValue("volumeDb", 0) / 20)))
    }

    function audioSourcePath() {
        var clipEffects = activeClip && activeClip.effects ? activeClip.effects : ({})
        var audioEffects = activeAudioClip && activeAudioClip.effects
                ? activeAudioClip.effects : ({})
        if (clipEffects.vocalRemoval !== true && audioEffects.vocalRemoval !== true)
            return ""
        return String(audioEffects.demucsPath || clipEffects.demucsPath || "")
    }

    function startCurrentDecode() {
        if (!activeClip || !activeMedia || activeMedia.kind === "image") {
            Backend.stopPreviewDecode()
            return
        }
        var elapsed = Math.max(0, Backend.playheadMs - Number(activeClip.startMs))
        var remaining = Math.max(1, Number(activeClip.durationMs) - elapsed)
        Backend.startPreviewDecode(activeMedia.path, activeMedia.kind,
                                   sourcePosition(), remaining,
                                   Number(activeMedia.width || 0),
                                   Number(activeMedia.height || 0),
                                   Number(activeMedia.frameRate || frameRate),
                                   !audioMuted(), previewVolume(),
                                   root.audioSourcePath())
    }

    function requestCurrentFrame() {
        if (!activeClip || !activeMedia || activeMedia.kind !== "video") {
            Backend.stopPreviewDecode()
            return
        }
        Backend.requestPreviewFrame(activeMedia.path, sourcePosition(),
                                    Number(activeMedia.width || 0),
                                    Number(activeMedia.height || 0))
    }

    function startPlayback() {
        if (Backend.durationMs <= 0)
            return
        if (Backend.playheadMs >= Backend.durationMs)
            Backend.playheadMs = 0
        Backend.playing = true
    }

    function togglePlayback() {
        if (playing)
            Backend.playing = false
        else
            startPlayback()
    }

    function seekFromPointer(xPosition, availableWidth) {
        if (availableWidth <= 0 || Backend.durationMs <= 0)
            return
        var ratio = Math.max(0, Math.min(1, xPosition / availableWidth))
        Backend.playheadMs = Math.round(ratio * Backend.durationMs)
    }

    function beginScrub(xPosition, availableWidth) {
        resumeAfterScrub = playing
        scrubbing = true
        Backend.playing = false
        seekFromPointer(xPosition, availableWidth)
    }

    function finishScrub(xPosition, availableWidth) {
        if (!scrubbing)
            return
        seekFromPointer(xPosition, availableWidth)
        scrubbing = false
        if (resumeAfterScrub)
            Qt.callLater(root.startPlayback)
        resumeAfterScrub = false
    }

    Connections {
        target: Backend
        function onPlayheadChanged() {
            if (!root.playing)
                previewSeekDebounce.restart()
        }
        function onClipsChanged() {
            if (root.playing)
                root.startCurrentDecode()
            else
                previewSeekDebounce.restart()
        }
    }

    onActiveClipIdChanged: {
        if (root.playing)
            root.startCurrentDecode()
        else
            previewSeekDebounce.restart()
    }

    onPlayingChanged: {
        if (playing) {
            lastTickMs = Date.now()
            startCurrentDecode()
        } else {
            Backend.stopPreviewDecode()
            previewSeekDebounce.restart()
        }
    }

    Component.onCompleted: previewSeekDebounce.start()
    Component.onDestruction: {
        Backend.playing = false
        Backend.stopPreviewDecode()
    }

    Timer {
        id: previewSeekDebounce
        interval: 70
        repeat: false
        onTriggered: root.requestCurrentFrame()
    }

    Timer {
        interval: 30
        repeat: true
        running: root.playing
        onTriggered: {
            if (Backend.durationMs <= 0) {
                Backend.playing = false
                return
            }

            var now = Date.now()
            var elapsed = root.lastTickMs > 0 ? now - root.lastTickMs : interval
            root.lastTickMs = now
            var nextPosition = Backend.playheadMs
                    + Math.max(1, Math.min(250, elapsed))

            Backend.playheadMs = Math.min(Backend.durationMs,
                                          Math.max(0, nextPosition))
            if (Backend.playheadMs >= Backend.durationMs) {
                Backend.playing = false
                if (Backend.appSettings.loopPlayback === true) {
                    Backend.playheadMs = 0
                    Qt.callLater(root.startPlayback)
                }
            }
        }
    }

    Shortcut {
        sequence: "Space"
        enabled: root.visible && Backend.durationMs > 0
        onActivated: root.togglePlayback()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: viewer
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Image {
                anchors.fill: parent
                source: root.activeMedia && root.activeMedia.kind === "video"
                        ? Backend.previewFrameUrl : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: false
                cache: false
                visible: root.activeMedia && root.activeMedia.kind === "video"
            }

            Image {
                anchors.fill: parent
                source: root.activeMedia && root.activeMedia.kind === "image"
                        ? root.sourceUrl : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                visible: root.activeMedia && root.activeMedia.kind === "image"
            }

            Image {
                anchors.centerIn: parent
                width: 34
                height: 34
                source: "../../assets/icons/music.svg"
                opacity: 0.5
                visible: root.activeMedia && root.activeMedia.kind === "audio"
            }

            // The frame the render composites into: the base picture fits it, and
            // overlays are placed relative to it. Sized to the base media's own
            // aspect ratio so it matches the letterboxed picture drawn above.
            Item {
                id: overlayLayer
                readonly property real baseAR: (root.activeMedia
                                                && root.activeMedia.width > 0
                                                && root.activeMedia.height > 0)
                                               ? root.activeMedia.width / root.activeMedia.height
                                               : 16 / 9
                width: Math.min(parent.width, parent.height * baseAR)
                height: Math.min(parent.height, parent.width / baseAR)
                anchors.centerIn: parent
                clip: true
                z: 2

                // Own texture, so a moving overlay does not dirty the decoded frame
                // drawn beneath it. Positions are deliberately NOT clamped here:
                // this pane previews the render, so it has to show whatever the
                // filter graph would produce, off-frame values included.
                layer.enabled: root.overlayClips.length > 0
                layer.smooth: true

                Repeater {
                    model: root.overlayClips
                    delegate: Item {
                        id: ov
                        required property var modelData
                        readonly property var clip: modelData
                        readonly property var media: Backend.mediaById(String(clip.mediaId || ""))
                        readonly property var fx: clip.effects ? clip.effects : ({})

                        readonly property real mediaAR: (media && media.width > 0 && media.height > 0)
                                                        ? media.width / media.height : 1
                        readonly property real frameAR: overlayLayer.width / Math.max(1, overlayLayer.height)
                        // force_original_aspect_ratio=decrease into the frame,
                        // then the intrinsic scale, then the overlay position -
                        // the same three steps the export filter graph applies.
                        readonly property real fitW: mediaAR > frameAR
                                                     ? overlayLayer.width
                                                     : overlayLayer.height * mediaAR
                        readonly property real fitH: fitW / mediaAR
                        readonly property real scaleFactor: Number(fx.scale !== undefined ? fx.scale : 100)

                        width: fitW * scaleFactor / 100
                        height: fitH * scaleFactor / 100
                        x: (overlayLayer.width - width) / 2
                           + Number(fx.positionX || 0) / 100 * overlayLayer.width
                        y: (overlayLayer.height - height) / 2
                           + Number(fx.positionY || 0) / 100 * overlayLayer.height
                        opacity: Number(fx.opacity !== undefined ? fx.opacity : 100) / 100

                        Image {
                            anchors.fill: parent
                            source: ov.media ? root.mediaUrl(ov.media) : ""
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: true
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: !root.activeMedia
                         || (root.activeMedia.kind === "video"
                             && Backend.previewFrameUrl === "")
                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 28
                    height: 28
                    source: "../../assets/icons/film.svg"
                    opacity: 0.4
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Backend.previewError !== "" ? Backend.previewError
                          : !root.activeMedia ? (Backend.durationMs > 0
                            ? root.timecode(Backend.playheadMs)
                            : "No video preview") : "FFmpeg preview loading..."
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    font.family: Backend.durationMs > 0 ? Theme.monoFont : ""
                }
            }

            Item {
                id: captionLayer
                anchors.fill: parent
                visible: root.captionsVisible && root.activeSubtitle !== null
                z: 3

                Rectangle {
                    id: captionBackground
                    width: Math.min(parent.width - 32, Math.max(100, parent.width * 0.92))
                    height: Math.min(parent.height - 16,
                                     captionText.implicitHeight + 14)
                    x: Backend.captionPosition === "custom"
                       ? Math.max(8, Math.min(parent.width - width - 8,
                                  Backend.captionPositionX * parent.width - width / 2))
                       : (parent.width - width) / 2
                    y: Backend.captionPosition === "top"
                       ? 18
                       : Backend.captionPosition === "center"
                         ? (parent.height - height) / 2
                         : Backend.captionPosition === "custom"
                           ? Math.max(8, Math.min(parent.height - height - 8,
                                      Backend.captionPositionY * parent.height - height / 2))
                           : Math.max(8, parent.height - height - 18)
                    radius: Theme.radiusSm
                    color: Backend.captionBackgroundVisible
                           ? Backend.captionBackgroundColor : "transparent"

                    Text {
                        id: captionText
                        x: 7
                        y: 7
                        width: parent.width - 14
                        height: implicitHeight
                        text: root.activeSubtitle ? root.activeSubtitle.text : ""
                        color: Backend.captionTextColor
                        font.family: Backend.captionFontFamily
                        font.pixelSize: Math.min(Backend.captionFontSize,
                                                 Math.max(11, viewer.height * 0.11))
                        font.bold: Backend.captionBold
                        font.italic: Backend.captionItalic
                        wrapMode: Text.WordWrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        horizontalAlignment: Backend.captionAlignment === "left"
                                             ? Text.AlignLeft
                                             : Backend.captionAlignment === "right"
                                               ? Text.AlignRight : Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        style: Backend.captionBackgroundVisible
                               ? Text.Normal : Text.Outline
                        styleColor: "black"
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: Theme.bgTimeline

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Text {
                    text: root.timecode(Backend.playheadMs)
                    color: Theme.accent
                    font.pixelSize: Theme.fsXs
                    font.family: Theme.monoFont
                }

                Item {
                    id: scrubber
                    Layout.fillWidth: true
                    Layout.preferredHeight: 24

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: 3
                        radius: 2
                        color: Theme.hover

                        Rectangle {
                            height: parent.height
                            radius: parent.radius
                            width: parent.width * (Backend.durationMs > 0
                                                   ? Backend.playheadMs / Backend.durationMs : 0)
                            color: Theme.accent
                        }
                    }

                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        color: Theme.textPrimary
                        anchors.verticalCenter: parent.verticalCenter
                        x: (parent.width - width) * (Backend.durationMs > 0
                                                    ? Backend.playheadMs / Backend.durationMs : 0)
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: Backend.durationMs > 0
                        preventStealing: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                        onPressed: mouse => root.beginScrub(mouse.x, width)
                        onPositionChanged: mouse => {
                            if (pressed)
                                root.seekFromPointer(mouse.x, width)
                        }
                        onReleased: mouse => root.finishScrub(mouse.x, width)
                        onCanceled: {
                            var shouldResume = root.resumeAfterScrub
                            root.scrubbing = false
                            root.resumeAfterScrub = false
                            if (shouldResume)
                                Qt.callLater(root.startPlayback)
                        }
                    }
                }

                Text {
                    text: root.timecode(Backend.durationMs)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsXs
                    font.family: Theme.monoFont
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: Theme.bgPanel

            RowLayout {
                anchors.centerIn: parent
                spacing: 4

                IconButton {
                    iconName: "skip-back"
                    boxSize: 30
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    enabled: Backend.durationMs > 0
                    onClicked: Backend.playheadMs = 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Go to beginning"
                }
                IconButton {
                    iconName: root.playing ? "pause" : "play"
                    boxSize: 34
                    glyphSize: 18
                    restColor: Theme.textPrimary
                    active: root.playing
                    enabled: Backend.durationMs > 0
                    onClicked: root.togglePlayback()
                    ToolTip.visible: hovered
                    ToolTip.text: root.playing ? "Pause (Space)" : "Play (Space)"
                }
                IconButton {
                    iconName: "skip-forward"
                    boxSize: 30
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    enabled: Backend.durationMs > 0
                    onClicked: Backend.playheadMs = Backend.durationMs
                    ToolTip.visible: hovered
                    ToolTip.text: "Go to end"
                }
                Rectangle {
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 18
                    color: Theme.border
                }
                IconButton {
                    iconName: "subtitles"
                    boxSize: 30
                    glyphSize: 16
                    restColor: Theme.textSecondary
                    active: root.captionsVisible
                    enabled: Backend.clips.length > 0
                    onClicked: root.captionsVisible = !root.captionsVisible
                    ToolTip.visible: hovered
                    ToolTip.text: root.captionsVisible
                                  ? "Hide timeline captions" : "Show timeline captions"
                }
            }
        }
    }
}
