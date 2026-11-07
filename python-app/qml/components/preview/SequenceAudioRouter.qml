import QtQuick
import QtMultimedia
import CutPro 1.0

Item {
    id: root
    property bool audioEnabled: true
    readonly property bool playing: enabled && Backend.playing
    readonly property var activeClip: Backend.videoPreviewHelper.activeClip || null
    readonly property var activeMedia: Backend.videoPreviewHelper.activeMedia || null
    readonly property var activeAudioClip: Backend.videoPreviewHelper.activeAudioClip || null
    readonly property bool embeddedAudioRouted: {
        var video = activeClip
        var audio = activeAudioClip
        return video && video.separateAudio === true && audio
                && String(audio.id) !== String(video.id)
                && Number(audio.startMs) === Number(video.startMs)
                && Number(audio.sourceInMs || 0) === Number(video.sourceInMs || 0)
                && Number(audio.durationMs) === Number(video.durationMs)
                && Number((audio.effects || {}).speed || 100) === Number((video.effects || {}).speed || 100)
    }
    function mediaUrl(media) {
        if (!media || !media.path) return ""
        var path = String(media.path).replace(/\\/g, "/")
        return (path.charAt(0) === "/" ? "file://" : "file:///") + encodeURI(path)
    }
    function audioClipPath(clip) {
        var replacement = Backend.clipReplacementAudioPath(clip || ({}))
        return mediaUrl(replacement ? {path: replacement} : Backend.mediaById(String(clip.mediaId || "")))
    }
    function trackEnabled(track) {
        var states = Backend.trackStates
        var prefix = String(track).charAt(0), solo = false, own = null
        for (var i = 0; i < states.length; ++i) {
            if (String(states[i].id).charAt(0) === prefix && states[i].solo) solo = true
            if (String(states[i].id) === String(track)) own = states[i]
        }
        return (!own || own.visible !== false) && (!solo || (own && own.solo === true))
    }
    function audioClipMuted(clip) {
        return !audioEnabled || Backend.appSettings.muteAllAudio === true
                || (Backend.voiceOverRecording && Backend.appSettings.voiceOverMuteMonitor === true)
                || !trackEnabled(clip.track) || Backend.mutedTracks.indexOf(String(clip.track)) >= 0
    }
    function audioClipVolume(clip) {
        var fx = clip.effects || ({})
        var master = Number(Backend.appSettings.masterVolume === undefined ? 100 : Backend.appSettings.masterVolume)
        return Math.max(0, Math.min(1, master / 100 * Math.pow(10, Number(fx.volumeDb || 0) / 20)))
    }

    Repeater {
        model: root.enabled ? Backend.videoPreviewHelper.activeAudioClips : []
        delegate: Item {
            id: audioDelegate
            required property var modelData
            property var timelineClip: modelData
            property bool audioClip: timelineClip
                                           && timelineClip.kind === "audio"
            // Whose audio the program player already carries. Two cases: the
            // active clip *is* this audio clip, and - after Extract Audio - this
            // is the extracted clip of the active video clip and still sits
            // where that clip does, in which case the preview decoder is playing
            // the same stream out of the same file. Without the second case an
            // extracted clip would be heard twice, once from the decoder and
            // once from the MediaPlayer below; without the "still sits where"
            // test, dragging it out of sync would silence it instead.
            property bool representedByMainPlayer: (root.activeMedia
                                                   && root.activeMedia.kind === "audio"
                                                   && root.activeClip
                                                   && String(root.activeClip.id)
                                                      === String(timelineClip.id))
                                                   || (root.embeddedAudioRouted
                                                       && root.activeAudioClip
                                                       && String(root.activeAudioClip.id || "")
                                                          === String(timelineClip.id))

            function localPosition() {
                return Math.max(0, (Backend.playheadMs
                                - Number(timelineClip.startMs)) * audioDelegate.speed
                                + Number(timelineClip.sourceInMs || 0))
            }

            readonly property real speed: Math.max(0.05, Number((timelineClip.effects || {}).speed || 100) / 100)

            function synchronize() {
                if (!root.visible || !audioClip || audioPlayer.source === "")
                    return
                var target = localPosition()
                if (Math.abs(Number(audioPlayer.position) - target) > 80)
                    audioPlayer.position = target
            }

            MediaPlayer {
                id: audioPlayer
                playbackRate: audioDelegate.speed
                source: audioDelegate.audioClip
                        ? root.audioClipPath(audioDelegate.timelineClip) : ""
                audioOutput: timelineAudioOutput
                onMediaStatusChanged: {
                    if (mediaStatus === MediaPlayer.LoadedMedia) {
                        audioDelegate.synchronize()
                        if (root.playing
                                && !audioDelegate.representedByMainPlayer)
                            play()
                    }
                }
            }

            AudioOutput {
                id: timelineAudioOutput
                muted: !audioDelegate.audioClip
                       || audioDelegate.representedByMainPlayer
                       || root.audioClipMuted(audioDelegate.timelineClip)
                volume: audioDelegate.audioClip
                        ? root.audioClipVolume(audioDelegate.timelineClip) : 0
            }

            Connections {
                target: Backend
                function onPlayheadChanged() {
                    if (!audioDelegate.audioClip
                            || audioDelegate.representedByMainPlayer)
                        return
                    if (!root.playing)
                        audioDelegate.synchronize()
                }
            }
            Connections {
                target: root
                function onPlayingChanged() {
                    if (!audioDelegate.audioClip
                            || audioDelegate.representedByMainPlayer)
                        return
                    if (root.playing) {
                        audioDelegate.synchronize()
                        audioPlayer.play()
                    } else {
                        audioPlayer.pause()
                    }
                }
            }

            Component.onCompleted: {
                if (audioDelegate.audioClip) {
                    audioDelegate.synchronize()
                    if (root.playing && !audioDelegate.representedByMainPlayer)
                        audioPlayer.play()
                }
            }
        }
    }

}
