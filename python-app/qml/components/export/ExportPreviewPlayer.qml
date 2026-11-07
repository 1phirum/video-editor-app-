pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../lumetri"
import "../preview"
import "../project"
import "../subtitles"
import "../timeline"

Rectangle {
    id: root
    color: Theme.viewer
    clip: true

    property real frameRate: activeMedia ? Number(activeMedia.frameRate || 30) : 30
    property int outputWidth: 1920
    property int outputHeight: 1080
    property int keyframeRevision: 0
    property bool audioEnabled: true
    property bool captionsVisible: true
    property bool scrubbing: false
    property bool resumeAfterScrub: false
    property int transportGeneration: 0
    property var trackStateList: Backend.trackStates

    readonly property bool playing: Backend.playing
    // Picture, media, audio routing and subtitle are all resolved once in C++
    // (VideoPreviewHelper) and cached until the next clip boundary. Reading them
    // here instead of re-scanning Backend.clips on every playhead tick is what
    // stops this pane re-materializing the whole clip list ~33x/sec during
    // playback (the QVariantMap hot-path re-materialization cost). The helper is
    // the same one the program monitor reads, kept at Backend.playheadMs in C++.
    readonly property var activeClip: Backend.videoPreviewHelper.activeClip || null
    readonly property string activeClipId: Backend.videoPreviewHelper.activeClipId
    readonly property var activeMedia: Backend.videoPreviewHelper.activeMedia || null
    readonly property var activeAudioClip: Backend.videoPreviewHelper.activeAudioClip || null
    readonly property var activeSubtitle: Backend.videoPreviewHelper.activeSubtitle || null
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

    // Logos and higher-lane images that composite over the base picture at the
    // playhead. Resolved and cached in C++ (VideoPreviewHelper::activeOverlayClips)
    // alongside the base clip, so this pane no longer rescans every clip per tick.
    readonly property var overlayClips: Backend.videoPreviewHelper.activeOverlayClips

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
        var speed = Math.max(0.05, Number((activeClip.effects || {}).speed || 100) / 100)
        return Math.max(0, (Backend.playheadMs - Number(activeClip.startMs)) * speed
                        + Number(activeClip.sourceInMs || 0))
    }

    // Extract Audio moved this clip's sound onto an independent A-track clip, and
    // this player has only one source: it decodes the video file, whose embedded
    // stream can stand in for the extracted clip while the two are still in step.
    // Once that clip is deleted - or moved, or retrimmed - the embedded stream is
    // no longer the sequence's audio and must not be heard. The program monitor
    // has gated this since Extract Audio landed (MonitorPanel's
    // embeddedAudioSilenced); the Output Preview did not, so it kept playing sound
    // that the monitor was already silent on and that the render leaves out - the
    // export builder drops audio for every separateAudio clip.
    function embeddedAudioSilenced() {
        if (!activeClip || activeClip.separateAudio !== true)
            return false
        var audio = activeAudioClip
        // VideoPreviewHelper::audioForClip hands back the video clip itself when
        // no extracted clip names it, which is the deleted case.
        if (!audio || String(audio.id || "") === String(activeClip.id))
            return true
        return Number(audio.startMs) !== Number(activeClip.startMs)
                || Number(audio.sourceInMs || 0)
                   !== Number(activeClip.sourceInMs || 0)
                || Number(audio.durationMs) !== Number(activeClip.durationMs)
    }

    function audioMuted() {
        return !audioEnabled || Backend.appSettings.muteAllAudio === true
                || embeddedAudioSilenced()
                || (activeAudioClip
                    && (Backend.mutedTracks.indexOf(
                            String(activeAudioClip.track)) >= 0
                        || !trackEnabled(activeAudioClip.track)))
    }

    readonly property real previewAudioLevel: audioMuted() ? 0 : previewVolume()
    onPreviewAudioLevelChanged: if (root.visible) Backend.setPreviewVolume(previewAudioLevel)

    function previewVolume() {
        return Math.max(0, Math.min(1,
                    (Backend.appSettings.masterVolume === undefined
                     ? 100 : Number(Backend.appSettings.masterVolume)) / 100
                    * Math.pow(10, audioEffectValue("volumeDb", 0) / 20)))
    }

    function audioSourcePath() {
        return Backend.clipReplacementAudioPath(activeAudioClip || activeClip || ({}))
    }

    function startCurrentDecode() {
        if (!root.visible || !root.playing)
            return
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
                                   true, audioMuted() ? 0 : previewVolume(),
                                   root.audioSourcePath())
    }

    function requestCurrentFrame(exact) {
        // A ruler click pauses and resumes before this 70 ms debounce may fire.
        // A still request stops the shared streaming decoder, so recheck ownership
        // and playback here even when the request was scheduled while paused.
        if (!root.visible || root.playing)
            return
        if (!activeClip || !activeMedia || activeMedia.kind !== "video") {
            Backend.stopPreviewDecode()
            return
        }
        Backend.requestPreviewFrame(activeMedia.path, sourcePosition(),
                                    Number(activeMedia.width || 0),
                                    Number(activeMedia.height || 0),
                                    exact !== false)
    }

    function scheduleCurrentFrame() {
        if (!root.visible || Backend.playing)
            return
        if (root.scrubbing) {
            // Start once per cadence window; continuous movement must not keep
            // pushing a debounce forward until the drag ends.
            if (!previewScrubThrottle.running)
                previewScrubThrottle.start()
        }
        previewSeekDebounce.restart()
    }

    function resumePlaybackLater() {
        const generation = root.transportGeneration
        Qt.callLater(function() {
            if (root.visible && !Backend.playing
                    && generation === root.transportGeneration)
                root.startPlayback()
        })
    }

    function startPlayback() {
        if (!root.visible || Backend.durationMs <= 0)
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
        previewScrubThrottle.stop()
        previewSeekDebounce.restart()
        if (resumeAfterScrub)
            root.resumePlaybackLater()
        resumeAfterScrub = false
    }

    Connections {
        target: Backend
        enabled: root.visible
        function onPlayheadChanged() {
            if (!Backend.playing)
                root.scheduleCurrentFrame()
        }
        function onClipsChanged() {
            if (root.playing)
                root.startCurrentDecode()
            else
                root.scheduleCurrentFrame()
        }
    }

    onActiveClipIdChanged: {
        if (!root.visible)
            return
        if (root.playing)
            root.startCurrentDecode()
        else
            root.scheduleCurrentFrame()
    }

    onPlayingChanged: {
        ++root.transportGeneration
        previewScrubThrottle.stop()
        previewSeekDebounce.stop()
        if (!root.visible)
            return
        if (playing) {
            startCurrentDecode()
        }
        // Backend pauses, rejects queued producer callbacks and retains the
        // accepted image before notifying us. Pause is never a still request.
    }

    // main.qml retains visited workspace Loaders. A hidden preview must neither
    // control Backend's one decoder nor advance its playhead. On hide, cancel
    // only our pending work: the newly visible monitor owns the shared decoder.
    onVisibleChanged: {
        ++root.transportGeneration
        previewScrubThrottle.stop()
        previewSeekDebounce.stop()
        if (!visible)
            return
        if (playing) {
            startCurrentDecode()
        } else {
            if (!Backend.previewFrameMatchesPlayhead())
                previewSeekDebounce.restart()
        }
    }

    Component.onCompleted: {
        if (root.visible && !root.playing)
            previewSeekDebounce.start()
    }
    Component.onDestruction: {
        if (root.visible) {
            Backend.playing = false
            Backend.stopPreviewDecode()
        }
    }

    Timer {
        id: previewScrubThrottle
        interval: 40
        repeat: false
        onTriggered: {
            if (root.scrubbing)
                root.requestCurrentFrame(false)
        }
    }

    Timer {
        id: previewSeekDebounce
        interval: 70
        repeat: false
        onTriggered: root.requestCurrentFrame(!root.scrubbing)
    }

    Shortcut {
        sequence: "Space"
        enabled: root.visible && Backend.durationMs > 0
        onActivated: root.togglePlayback()
    }

    SequenceAudioRouter {
        enabled: root.visible
        audioEnabled: root.audioEnabled
    }

    Connections {
        target: Backend.keyframeEngine
        function onKeyframesChanged(clipId) { ++root.keyframeRevision }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: viewer
            Layout.alignment: Qt.AlignCenter
            Layout.preferredWidth: Math.min(root.width, Math.max(0, root.height - 68) * root.outputWidth / Math.max(1, root.outputHeight))
            Layout.fillHeight: true
            clip: true

            SequenceBasePicture {
                id: exportBasePicture
                anchors.fill: parent
                activeMedia: root.activeMedia
                activeEffects: root.activeClip ? (root.activeClip.effects || {}) : ({})
                clipId: root.activeClipId
                sourceUrl: root.sourceUrl
                keyframeRevision: root.keyframeRevision
            }

            SequencePictureEffects {
                id: exportPictureEffects
                anchors.fill: parent
                source: exportBasePicture
            }

            SequenceBlurRegions {
                anchors.fill: parent
                z: 2
                controls: exportPictureEffects
                sourceItem: exportBasePicture
            }

            SubtitleBlurRegion {
                anchors.fill: parent
                z: 2
                sourceItem: exportBasePicture
                targetItem: captionBackground
                active: Backend.captionBlurEnabled && root.activeMedia !== null
                trackingEnabled: Backend.captionBlurTrackingEnabled
                manualX: Backend.captionBlurRegionX
                manualY: Backend.captionBlurRegionY
                manualWidth: Backend.captionBlurRegionWidth
                manualHeight: Backend.captionBlurRegionHeight
                blurStrength: Backend.captionBlurStrength
                padding: Backend.captionBlurPadding
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
                anchors.fill: parent
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
                    delegate: SequenceOverlayPicture {
                        required property var modelData
                        clip: modelData
                        frameWidth: overlayLayer.width
                        frameHeight: overlayLayer.height
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
                            : qsTr("No video preview")) : qsTr("FFmpeg preview loading...")
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
                    width: Math.max(40, Math.min(parent.width - 8, Backend.captionBoxWidth * parent.width))
                    height: captionText.implicitHeight + 10
                    x: Backend.captionPosition === "custom"
                       ? Math.max(8, Math.min(parent.width - width - 8,
                                  Backend.captionPositionX * parent.width - width / 2))
                       : (parent.width - width) / 2
                    y: Backend.captionPosition === "top"
                       ? 28
                       : Backend.captionPosition === "center"
                         ? (parent.height - height) / 2
                         : Backend.captionPosition === "custom"
                           ? Math.max(8, Math.min(parent.height - height - 8,
                                      Backend.captionPositionY * parent.height - height / 2))
                           : Math.max(8, parent.height - height - 28)
                    radius: Theme.radiusSm
                    color: Backend.captionBackgroundVisible
                           ? Backend.captionBackgroundColor : "transparent"

                    SequenceCaptionText {
                        id: captionText
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        anchors.topMargin: 5
                        anchors.bottomMargin: 5
                        cue: root.activeSubtitle
                        emSize: Math.min(Backend.captionFontSize, Math.max(12, viewer.height * 0.6))
                        maxWidth: Math.max(60, captionBackground.width - 28)
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
                            previewScrubThrottle.stop()
                            previewSeekDebounce.restart()
                            if (shouldResume)
                                root.resumePlaybackLater()
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
                    ToolTip.text: qsTr("Go to beginning")
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
                    ToolTip.text: root.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
                }
                IconButton {
                    iconName: "skip-forward"
                    boxSize: 30
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    enabled: Backend.durationMs > 0
                    onClicked: Backend.playheadMs = Backend.durationMs
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Go to end")
                }
            }
        }
    }
}
