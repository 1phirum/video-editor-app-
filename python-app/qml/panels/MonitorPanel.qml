// qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import QtMultimedia
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
    color: Theme.bgSidebar

    readonly property bool playing: Backend.playing
    // C++ caches these values until a clip/subtitle boundary is crossed. This
    // keeps playback ticks from scanning the entire timeline in JavaScript.
    property var activeClip: Backend.videoPreviewHelper.activeClip || null
    property var activeMedia: Backend.videoPreviewHelper.activeMedia || null
    property var activeAudioClip: Backend.videoPreviewHelper.activeAudioClip || null
    property var activeSubtitle: Backend.videoPreviewHelper.activeSubtitle || null
    property url sourceUrl: mediaUrl(activeMedia)
    property bool deferredSourceArmed: false
    // Armed once a video clip becomes active, or on the first play/scrub gesture.
    // Arming a source no longer means re-opening it per frame: the C++ scrub
    // service keeps the container warm, so a large file costs the same as a small
    // one after the first still.
    property bool processPreviewArmed: false
    readonly property bool deferActiveSource:
        Boolean(activeMedia
                && (activeMedia.deferMonitorLoad === true
                    || Number(activeMedia.durationMs || 0) >= 1800000
                    || Number(activeMedia.sizeBytes || 0) >= 1073741824))
        || Boolean(activeClip && activeClip.timelineRenderMode === "lightweight")
    // All program video/audio now goes through the persistent native C++
    // FFmpeg decoder. Qt Multimedia remains available only for independent
    // small A-track players until the native audio mixer is ported.
    readonly property bool useProcessPreview:
        Boolean(activeMedia
                && (activeMedia.kind === "video"
                    || activeMedia.kind === "audio"))
    readonly property url playerSourceUrl:
        useProcessPreview ? "" : sourceUrl
    // Previous value of activeSourceKey, kept only so the log can print what
    // actually changed when a playback session is torn down.
    property string lastSourceKey: ""
    property double processPlaybackWallMs: 0
    property double processPlaybackTimelineMs: 0
    // Source position of the last frame the decoder reported as being on screen.
    // The playhead is re-anchored to it whenever it changes; the wall clock only
    // fills the gap between two frames. -1 means nothing has been shown yet for
    // the current session.
    property double presentedSourceMs: -1
    property bool captionSelected: false
    property var trackStateList: Backend.trackStates
    property bool scrubbingPlayhead: false
    property bool resumePlaybackAfterScrub: false
    property bool changingTimelineClip: false
    property string replacementAudioKey: ""
    readonly property var activeLumetri: activeClip && activeClip.lumetri ? activeClip.lumetri : ({})
    readonly property var activeEffects: activeClip && activeClip.effects ? activeClip.effects : ({})

    // Everything the decoder actually cares about: which file, which slice of
    // it, and which audio stream. VideoPreviewHelper::resolve() compares whole
    // clip maps, so activeClip "changes" whenever any field in it changes -
    // including the effects blob. Keying the decoder teardown on the whole map
    // meant every checkbox and every slider tick in Effect Controls was treated
    // as "the monitor moved to a different clip": stopPreviewDecode() destroys
    // the WASAPI sink synchronously on the GUI thread, which the watchdog
    // measured at 404-1749 ms per call, and the prewarm behind it re-opened a
    // 26-hour container. An effects edit changes the picture, not the source.
    readonly property string activeSourceKey:
        root.activeClip && root.activeMedia
        ? [String(root.activeClip.id),
           String(root.activeMedia.path),
           String(root.activeMedia.kind),
           String(root.activeClip.sourceInMs || 0),
           String(root.activeClip.startMs || 0),
           String(root.activeClip.durationMs || 0),
           root.audioSourcePath()].join("|")
        : ""
    readonly property var activeEffectStack: activeClip && activeClip.effectStack
                                             ? activeClip.effectStack : []
    // What the monitor should be showing right now: the clip's own enabled
    // effects, then the effect-track bars covering the playhead. Each entry keeps
    // the id of the item that owns it, because keyframe channels are stored per
    // item and a bar's channels are not the clip's.
    readonly property var previewStackEntries: {
        var entries = []
        var ownerId = root.activeClip ? String(root.activeClip.id || "") : ""
        var stack = root.activeEffectStack
        for (var i = 0; i < stack.length; ++i) {
            if (stack[i] && stack[i].enabled !== false)
                entries.push({"ownerId": ownerId, "instance": stack[i]})
        }
        var playhead = Math.max(0, Number(Backend.playheadMs))
        var bars = root.timelineEffectEntries
        for (var b = 0; b < bars.length; ++b) {
            var bar = bars[b]
            if (playhead < bar.startMs || playhead >= bar.endMs)
                continue
            entries.push({"ownerId": bar.clipId, "instance": bar.instance})
        }
        return entries
    }
    // keyframesFor() is a plain call, not a tracked property, so the animated
    // motion bindings need something to depend on to refresh when the curve
    // itself changes. Bumped by the Connections below on keyframesChanged.
    property int kfRevision: 0

    function clipById(clipId) {
        if (!clipId)
            return null
        for (var i = 0; i < Backend.clips.length; ++i) {
            if (Backend.clips[i].id === clipId)
                return Backend.clips[i]
        }
        return null
    }

    function monitorClipAt(position) {
        // During mask editing, preview the selected clip even if another
        // visual track is above it at the current playhead position. An
        // effect-track bar is never the preview: it has no picture of its own,
        // so what it blurs is whatever clip is under the playhead.
        if (Backend.customBlurEditClipId !== "") {
            var editingClip = clipById(Backend.customBlurEditClipId)
            if (editingClip && editingClip.kind !== "audio"
                    && editingClip.kind !== "subtitle"
                    && editingClip.kind !== "effect")
                return editingClip
        }
        return clipAt(position)
    }

    // Every enabled effect living on the effect track, with the bar that carries
    // it and the stretch that bar covers. Bars are not media, so this list only
    // changes when the timeline does - the playhead is read where each entry is
    // used rather than here, because rebuilding a Repeater model whose items each
    // own a texture would recreate those textures on every frame of playback.
    readonly property var timelineEffectEntries: {
        var entries = []
        var bars = Backend.timelineEffects || []
        for (var barIndex = 0; barIndex < bars.length; ++barIndex) {
            var bar = bars[barIndex]
            if (!bar || bar.enabled === false || !root.trackVisible(bar.track))
                continue
            var startMs = Number(bar.startMs || 0)
            var endMs = startMs + Number(bar.durationMs || 0)
            if (endMs <= startMs)
                continue
            var stack = bar.effectStack || []
            for (var i = 0; i < stack.length; ++i) {
                var instance = stack[i]
                if (!instance || instance.enabled === false)
                    continue
                entries.push({"clipId": String(bar.id || ""),
                              "startMs": startMs,
                              "endMs": endMs,
                              "instance": instance})
            }
        }
        return entries
    }

    // The Custom Blur subset, which is the one the monitor draws as its own
    // region rather than folding into the picture's MultiEffect.
    readonly property var effectTrackBlurs: {
        var result = []
        var entries = root.timelineEffectEntries
        for (var i = 0; i < entries.length; ++i) {
            if (entries[i].instance.definitionId === "custom_blur")
                result.push(entries[i])
        }
        return result
    }

    // The effect lane's own eye toggle. Backend keeps track visibility as a list
    // of state records, so a hidden F1 stops previewing the same way it stops
    // exporting.
    function trackVisible(track) {
        var states = Backend.trackStates || []
        for (var i = 0; i < states.length; ++i) {
            if (states[i].id === track)
                return states[i].visible !== false
        }
        return true
    }

    function colorValue(key, fallback) {
        var value = activeLumetri[key]
        return value === undefined || value === null ? fallback : Number(value)
    }
    function lumetriSectionEnabled(key) {
        var value = activeLumetri[key]
        return value === undefined || value === null ? true : Boolean(value)
    }
    function sectionColorValue(sectionKey, valueKey, fallback) {
        return lumetriSectionEnabled(sectionKey) ? colorValue(valueKey, fallback)
                                                  : fallback
    }
    function effectValue(key, fallback) {
        var value = activeEffects[key]
        return value === undefined || value === null ? fallback : Number(value)
    }
    // Same as effectValue, but follows keyframes. When the active clip has a
    // keyframed channel for `key`, the value is interpolated at the current
    // playhead so the program monitor actually animates as it plays or scrubs;
    // otherwise this is exactly effectValue. Depends on playheadMs and on
    // kfRevision (bumped when a keyframe is added/moved/deleted) so the monitor
    // transform re-evaluates in both cases.
    function animatedValue(key, fallback) {
        var base = effectValue(key, fallback)
        if (root.kfRevision < 0 || !root.activeClip || !root.activeClip.id)
            return base
        return Backend.keyframeEngine.valueAt(
                    String(root.activeClip.id), key,
                    Math.max(0, Number(Backend.playheadMs)), base)
    }
    function audioClipFor(clip) {
        if (!clip)
            return null
        if (clip.kind === "audio")
            return clip
        if (!clip.linkGroupId)
            return clip
        for (var i = 0; i < Backend.clips.length; ++i) {
            var candidate = Backend.clips[i]
            if (candidate.linkGroupId === clip.linkGroupId
                    && candidate.linkedRole === "audio")
                return candidate
        }
        return clip
    }
    function audioEffectValue(key, fallback) {
        var values = activeAudioClip && activeAudioClip.effects
                ? activeAudioClip.effects : activeEffects
        var value = values ? values[key] : undefined
        return value === undefined || value === null ? fallback : Number(value)
    }
    function audioSourcePath() {
        var clipEffects = activeClip && activeClip.effects ? activeClip.effects : ({})
        var audioEffects = activeAudioClip && activeAudioClip.effects
                ? activeAudioClip.effects : ({})
        if (clipEffects.vocalRemoval !== true && audioEffects.vocalRemoval !== true)
            return ""
        return String(audioEffects.demucsPath || clipEffects.demucsPath || "")
    }
    function replacementAudioMuted() {
        return Backend.appSettings.muteAllAudio === true
                || (root.activeAudioClip
                    && (Backend.mutedTracks.indexOf(
                            String(root.activeAudioClip.track)) >= 0
                        || !root.trackEnabled(root.activeAudioClip.track)))
    }
    function replacementAudioVolume() {
        return Math.max(0, Math.min(1,
                    (Backend.appSettings.masterVolume === undefined
                     ? 100 : Number(Backend.appSettings.masterVolume)) / 100
                    * Math.pow(10, root.audioEffectValue("volumeDb", 0) / 20)))
    }
    function startReplacementAudio() {
        if (root.useProcessPreview)
            return
        var path = root.audioSourcePath()
        if (!root.playing || path === "" || !root.activeClip
                || root.replacementAudioMuted()) {
            if (root.replacementAudioKey !== "") {
                root.replacementAudioKey = ""
                Backend.stopPreviewDecode()
            }
            return
        }
        var clipKey = String(root.activeClip.id || "")
        var nextKey = clipKey + "|" + path
        if (nextKey === root.replacementAudioKey)
            return
        var localPosition = Math.max(0,
                Backend.playheadMs - Number(root.activeClip.startMs)
                + Number(root.activeClip.sourceInMs || 0))
        var remaining = Math.max(1,
                Number(root.activeClip.durationMs || 0)
                - Math.max(0, Backend.playheadMs - Number(root.activeClip.startMs)))
        root.replacementAudioKey = nextKey
        Backend.startPreviewDecode(path, "audio", localPosition, remaining,
                                   0, 0, 30, true,
                                   root.replacementAudioVolume())
    }
    // Same idea as animatedValue(), for a parameter of one effect instance. The
    // channel name comes from the engine so the panel, the monitor and the export
    // builder all agree on it.
    function instanceValue(instance, parameterId, fallback) {
        return root.ownerInstanceValue(
                    root.activeClip ? String(root.activeClip.id || "") : "",
                    instance, parameterId, fallback)
    }
    // Keyframe channels are stored per (clip, instance), so an instance carried by
    // an effect-track bar has to be read against the bar's id rather than against
    // whatever clip the monitor happens to be previewing.
    function ownerInstanceValue(ownerId, instance, parameterId, fallback) {
        var parameters = instance && instance.parameters ? instance.parameters : ({})
        var base = parameters[parameterId] === undefined
                ? fallback : Number(parameters[parameterId])
        if (root.kfRevision < 0 || !instance || !instance.id || !ownerId)
            return base
        return Backend.keyframeEngine.instanceValueAt(
                    String(ownerId), String(instance.id),
                    parameterId, Math.max(0, Number(Backend.playheadMs)), base)
    }
    function stackHas(effectId) {
        var entries = root.previewStackEntries
        for (var i = 0; i < entries.length; ++i) {
            if (entries[i].instance.definitionId === effectId)
                return true
        }
        return false
    }
    function stackValue(effectId, parameterId, fallback) {
        var result = fallback
        var entries = root.previewStackEntries
        for (var i = 0; i < entries.length; ++i) {
            var entry = entries[i]
            var instance = entry.instance
            if (instance.definitionId === effectId && instance.parameters
                    && instance.parameters[parameterId] !== undefined)
                result = root.ownerInstanceValue(
                            entry.ownerId, instance, parameterId,
                            Number(instance.parameters[parameterId]))
        }
        return result
    }
    function customBlurMask(instance) {
        var parameters = instance && instance.parameters ? instance.parameters : ({})
        var mask = parameters.mask || ({})
        return {
            x: mask.x === undefined ? 0.30 : Number(mask.x),
            y: mask.y === undefined ? 0.35 : Number(mask.y),
            width: mask.width === undefined ? 0.40 : Number(mask.width),
            height: mask.height === undefined ? 0.30 : Number(mask.height)
        }
    }
    function customBlurAmount(instance) {
        return root.instanceValue(instance, "amount", 12)
    }
    function editingCustomBlurInstance() {
        if (Backend.customBlurEditClipId === ""
                || Backend.customBlurEditInstanceId === "")
            return null
        // Editing must follow the selected effect, rather than clipAt(). The
        // latter can resolve a different clip when visual tracks overlap.
        for (var clipIndex = 0; clipIndex < Backend.clips.length; ++clipIndex) {
            var clip = Backend.clips[clipIndex]
            if (clip.id !== Backend.customBlurEditClipId)
                continue
            var stack = clip.effectStack || []
            for (var effectIndex = 0; effectIndex < stack.length; ++effectIndex) {
                var effect = stack[effectIndex]
                if (effect.id === Backend.customBlurEditInstanceId
                        && effect.definitionId === "custom_blur"
                        && effect.enabled !== false)
                    return effect
            }
            return null
        }
        return null
    }
    function stackPreviewEffectActive() {
        // Only the effects MultiEffect can actually express are listed. The rest
        // of the registry is applied on export, where FFmpeg runs the real
        // filter chain.
        return stackHas("brightness_contrast") || stackHas("monochrome")
                || stackHas("gaussian_blur") || stackHas("box_blur")
                || stackHas("exposure") || stackHas("hue_saturation")
                || stackHas("vibrance") || stackHas("directional_blur")
                || stackHas("smart_blur")
    }

    function previewColorEffectActive() {
        return Math.abs(sectionColorValue("basicEnabled", "exposure", 0)) > 0.001
                || Math.abs(sectionColorValue("basicEnabled", "contrast", 0)) > 0.001
                || Math.abs(sectionColorValue("basicEnabled", "whites", 0)) > 0.001
                || Math.abs(sectionColorValue("basicEnabled", "blacks", 0)) > 0.001
                || Math.abs(sectionColorValue("creativeEnabled", "fade", 0)) > 0.001
                || Math.abs(sectionColorValue("creativeEnabled", "vibrance", 0)) > 0.001
                || Math.abs(sectionColorValue("basicEnabled", "temperature", 0)) > 0.001
                || Math.abs(sectionColorValue("basicEnabled", "saturation", 100) - 100) > 0.001
                || Math.abs(sectionColorValue("creativeEnabled", "creativeSaturation", 100) - 100) > 0.001
    }

    function trackHasSolo(prefix) {
        for (var i = 0; i < Backend.trackStates.length; ++i) {
            var state = Backend.trackStates[i]
            if (String(state.id).charAt(0) === prefix && state.solo)
                return true
        }
        return false
    }

    function trackEnabled(track) {
        var id = String(track)
        var state = null
        for (var i = 0; i < trackStateList.length; ++i) {
            if (trackStateList[i].id === id) { state = trackStateList[i]; break }
        }
        if (state && state.visible === false)
            return false
        var prefix = id.charAt(0)
        return !trackHasSolo(prefix) || (state && state.solo === true)
    }

    function mediaUrl(media) {
        if (!media || !media.path)
            return ""

        // MediaPlayer expects a URL. A raw Windows path such as C:\video.mp4
        // is otherwise interpreted as a URL with a "c" scheme and will not
        // load. encodeURI keeps spaces and non-ASCII filenames valid as well.
        var path = String(media.path).replace(/\\/g, "/")
        return path.charAt(0) === "/"
                ? "file://" + encodeURI(path)
                : "file:///" + encodeURI(path)
    }

    // Audio clips are independent timeline sources. The program video player
    // only owns the embedded audio of the active video; these delegates keep
    // generated speech and other A-track clips audible at the same time.
    function audioClipPath(clip) {
        var media = mediaForClip(clip)
        return media ? mediaUrl(media) : ""
    }

    function audioClipMuted(clip) {
        return Backend.appSettings.muteAllAudio === true
                || !root.trackEnabled(clip.track)
                || Backend.mutedTracks.indexOf(String(clip.track)) >= 0
    }

    function audioClipVolume(clip) {
        var effects = clip && clip.effects ? clip.effects : ({})
        var master = Backend.appSettings.masterVolume === undefined
                ? 100 : Number(Backend.appSettings.masterVolume)
        var db = effects.volumeDb === undefined ? 0 : Number(effects.volumeDb)
        return Math.max(0, Math.min(1, master / 100 * Math.pow(10, db / 20)))
    }

    function clipAt(position) {
        var selected = null
        var selectedRank = -1
        for (var i = 0; i < Backend.clips.length; ++i) {
            var clip = Backend.clips[i]
            if (clip.enabled === false
                    || clip.kind === "subtitle"
                    || !root.trackEnabled(clip.track)
                    || position < clip.startMs
                    || position >= clip.startMs + clip.durationMs)
                continue

            // Higher-numbered video tracks are visually above lower tracks.
            // Audio is only used when no video clip exists at this position.
            var video = clip.track.charAt(0) === "V"
            var rank = video ? 1000 + parseInt(clip.track.substring(1)) : 0
            if (rank > selectedRank) {
                selected = clip
                selectedRank = rank
            }
        }
        return selected
    }

    function subtitleAt(position) {
        for (var i = 0; i < Backend.clips.length; ++i) {
            var clip = Backend.clips[i]
            if (clip.kind === "subtitle"
                    && clip.enabled !== false
                    && root.trackEnabled(clip.track)
                    && position >= clip.startMs
                    && position < clip.startMs + clip.durationMs)
                return clip
        }
        return null
    }

    function mediaForClip(clip) {
        if (!clip)
            return null
        for (var i = 0; i < Backend.media.length; ++i) {
            if (Backend.media[i].id === clip.mediaId)
                return Backend.media[i]
        }
        return null
    }

    function nextClip(position) {
        var result = null
        for (var i = 0; i < Backend.clips.length; ++i) {
            var clip = Backend.clips[i]
            if (clip.enabled === false || clip.kind === "subtitle"
                    || !root.trackEnabled(clip.track)
                    || clip.startMs < position)
                continue
            var clipRank = clip.track.charAt(0) === "V"
                    ? parseInt(clip.track.substring(1)) : 0
            var resultRank = result && result.track.charAt(0) === "V"
                    ? parseInt(result.track.substring(1)) : 0
            if (!result || clip.startMs < result.startMs
                    || (clip.startMs === result.startMs && clipRank > resultRank))
                result = clip
        }
        return result
    }

    function finishSequencePlayback() {
        if (Backend.appSettings.loopPlayback === true) {
            Backend.playing = false
            Backend.playheadMs = 0
            Qt.callLater(root.startPlayback)
        } else {
            Backend.playing = false
        }
    }

    function startPlayback() {
        if (Backend.durationMs <= 0)
            return
        if (Backend.playheadMs >= Backend.durationMs)
            Backend.playheadMs = 0

        Backend.playing = true
        // If playback starts in a gap, keep the playhead moving naturally.
        // The next clip's media source will load when its start is reached.
        Qt.callLater(function() {
            if (root.activeMedia && root.activeMedia.kind !== "image"
                    && !root.useProcessPreview) {
                root.seekPlayer()
                player.play()
            }
        })
    }

    function timecode(milliseconds) {
        var frames = Math.floor((milliseconds % 1000) / 40)
        var total = Math.floor(milliseconds / 1000)
        var seconds = total % 60
        var minutes = Math.floor(total / 60) % 60
        var hours = Math.floor(total / 3600)
        function pad(value) { return value < 10 ? "0" + value : value }
        return pad(hours) + ":" + pad(minutes) + ":" + pad(seconds) + ":" + pad(frames)
    }

    function activeSourcePosition() {
        if (!activeClip)
            return 0
        return Math.max(0, Backend.playheadMs - Number(activeClip.startMs)
                        + Number(activeClip.sourceInMs || 0))
    }

    function requestProcessFrame(exact) {
        if (!root.useProcessPreview || !root.processPreviewArmed
                || !root.activeMedia || root.playing)
            return
        Backend.requestPreviewFrame(String(root.activeMedia.path),
                                    root.activeSourcePosition(),
                                    Number(root.activeMedia.width || 0),
                                    Number(root.activeMedia.height || 0),
                                    exact === true)
    }

    // Live scrubbing. A playhead drag delivers playheadChanged every 8-16 ms,
    // which is faster than any debounce interval worth using - and a QML Timer
    // restart pushes its deadline forward, so a debounce fed at that rate never
    // fires at all. The monitor therefore stayed on the frame the drag started
    // from and only caught up once the pointer stopped moving.
    //
    // A throttle instead: one request on the leading edge, then at most one every
    // scrubIntervalMs for as long as the playhead keeps moving, so the picture
    // follows the drag. Those in-drag requests are the cheap kind (the seek lands
    // on the keyframe at or before the position); the precise frame costs a
    // forward decode, so it is asked for once, when the motion settles.
    readonly property int scrubIntervalMs: 40
    readonly property int scrubSettleMs: 90
    property double lastScrubRequestMs: 0
    property bool scrubRequestQueued: false

    function scheduleProcessFrame() {
        if (!root.useProcessPreview || !root.processPreviewArmed
                || !root.activeMedia || root.playing)
            return
        // Restarted on every move: this one is meant to fire after the motion,
        // not during it.
        scrubSettle.restart()
        var now = Date.now()
        var since = now - root.lastScrubRequestMs
        if (since >= root.scrubIntervalMs || since < 0) {
            scrubThrottle.stop()
            root.scrubRequestQueued = false
            root.lastScrubRequestMs = now
            root.requestProcessFrame(false)
            return
        }
        // Inside the cadence window: remember that a newer position is waiting
        // and let the timer serve it when the window closes. start() rather than
        // restart(), and only while nothing is queued, so a stream of moves
        // cannot keep pushing the deadline the way the old debounce did.
        if (!root.scrubRequestQueued) {
            root.scrubRequestQueued = true
            scrubThrottle.interval = Math.max(1, root.scrubIntervalMs - since)
            scrubThrottle.start()
        }
    }

    // For everything that is not a drag: a new clip, a paused seek, an effects
    // edit. There is no cadence to respect, so ask immediately.
    function requestFrameNow(exact) {
        scrubThrottle.stop()
        scrubSettle.stop()
        root.scrubRequestQueued = false
        root.lastScrubRequestMs = Date.now()
        root.requestProcessFrame(exact === true)
    }

    function startProcessPlayback() {
        if (!root.useProcessPreview || !root.activeMedia || !root.activeClip)
            return false
        // One line per playback session. A session means opening the container
        // and seeking, so anything that starts more than one per Play is a bug
        // worth seeing named.
        console.info("monitor: playback session at playhead",
                     Math.round(Backend.playheadMs), "clip",
                     String(root.activeClip.id))
        root.processPreviewArmed = true
        var elapsed = Math.max(0, Backend.playheadMs
                                  - Number(root.activeClip.startMs))
        var remaining = Math.max(1, Number(root.activeClip.durationMs) - elapsed)
        root.processPlaybackWallMs = Date.now()
        root.processPlaybackTimelineMs = Backend.playheadMs
        root.presentedSourceMs = -1
        return Backend.startPreviewDecode(
                    String(root.activeMedia.path),
                    String(root.activeMedia.kind),
                    root.activeSourcePosition(), remaining,
                    Number(root.activeMedia.width || 0),
                    Number(root.activeMedia.height || 0),
                    Number(root.activeMedia.frameRate || 30),
                    !originalAudioOutput.muted,
                    originalAudioOutput.volume,
                    root.audioSourcePath())
    }

    function seekPlayer() {
        if (!activeClip || !activeMedia || activeMedia.kind === "image")
            return
        if (root.useProcessPreview) {
            root.scheduleProcessFrame()
            return
        }
        if (playerSourceUrl.toString() === "")
            return
        var localPosition = root.activeSourcePosition()
        if (Math.abs(player.position - localPosition) > 100)
            player.position = localPosition
    }

    function seekFromScrubber(xPosition, scrubberWidth) {
        if (scrubberWidth <= 0 || Backend.durationMs <= 0)
            return
        var fraction = Math.max(0, Math.min(1, xPosition / scrubberWidth))
        Backend.playheadMs = fraction * Backend.durationMs
    }

    function beginScrub(xPosition, scrubberWidth) {
        if (root.useProcessPreview)
            root.processPreviewArmed = true
        resumePlaybackAfterScrub = Backend.playing
        scrubbingPlayhead = true
        if (Backend.playing)
            Backend.playing = false
        seekFromScrubber(xPosition, scrubberWidth)
    }

    function finishScrub(xPosition, scrubberWidth, commitPosition) {
        if (!scrubbingPlayhead)
            return
        if (commitPosition)
            seekFromScrubber(xPosition, scrubberWidth)
        var shouldResume = resumePlaybackAfterScrub
        scrubbingPlayhead = false
        resumePlaybackAfterScrub = false
        if (shouldResume && Backend.durationMs > 0)
            Qt.callLater(function() { Backend.playing = true })
    }

    MediaPlayer {
        id: player
        // Opening multi-gigabyte sources in Qt Multimedia can synchronously
        // parse the container on Windows. Large sources are armed only when the
        // user starts playback, so adding them to a timeline stays responsive.
        source: root.activeMedia && root.activeMedia.kind !== "image"
                ? root.playerSourceUrl : ""
        videoOutput: videoOutput
        // Do not attach an audio output while a Demucs replacement exists.
        // This prevents QtMultimedia from decoding the original AAC stream.
        audioOutput: root.audioSourcePath() === "" ? originalAudioOutput : null
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia) {
                root.changingTimelineClip = false
                root.seekPlayer()
                if (root.playing)
                    play()
            } else if (mediaStatus === MediaPlayer.EndOfMedia
                       && root.playing) {
                // Preserve the timeline gap after this source. The timer will
                // continue advancing with a black/silent monitor until the
                // next clip's start time is reached.
                if (root.activeClip) {
                    Backend.playheadMs = Math.min(
                                Backend.durationMs,
                                Number(root.activeClip.startMs)
                                + Number(root.activeClip.durationMs))
                } else if (Backend.playheadMs >= Backend.durationMs) {
                    root.finishSequencePlayback()
                }
            }
        }
    }

    // The C++ helper updates this small list only at clip boundaries. Each
    // active source gets its own output, so overlaps are mixed without
    // constructing a MediaPlayer for every clip in a large project.
    Repeater {
        model: Backend.videoPreviewHelper.activeAudioClips
        delegate: Item {
            id: audioDelegate
            required property var modelData
            property var timelineClip: modelData
            property bool audioClip: timelineClip
                                           && timelineClip.kind === "audio"
            property bool representedByMainPlayer: root.activeMedia
                                                   && root.activeMedia.kind === "audio"
                                                   && root.activeClip
                                                   && String(root.activeClip.id)
                                                      === String(timelineClip.id)

            function localPosition() {
                return Math.max(0, Backend.playheadMs
                                - Number(timelineClip.startMs)
                                + Number(timelineClip.sourceInMs || 0))
            }

            function synchronize() {
                if (!audioClip || audioPlayer.source === "")
                    return
                var target = localPosition()
                if (Math.abs(Number(audioPlayer.position) - target) > 80)
                    audioPlayer.position = target
            }

            MediaPlayer {
                id: audioPlayer
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
                if (audioDelegate.audioClip)
                    audioDelegate.synchronize()
            }
        }
    }

    AudioOutput {
        id: originalAudioOutput
        muted: Backend.appSettings.muteAllAudio === true
               || (root.activeAudioClip
                   && (Backend.mutedTracks.indexOf(
                           String(root.activeAudioClip.track)) >= 0
                       || !root.trackEnabled(root.activeAudioClip.track)))
        volume: Math.max(0, Math.min(1,
                    (Backend.appSettings.masterVolume === undefined
                     ? 100 : Number(Backend.appSettings.masterVolume)) / 100
                    * Math.pow(10, root.audioEffectValue("volumeDb", 0) / 20)))
    }

    Timer {
        id: scrubThrottle
        // The cadence gate. Interval is set by scheduleProcessFrame() to whatever
        // is left of the current window, so the queued position is served the
        // moment the window closes rather than a full interval later.
        interval: root.scrubIntervalMs
        repeat: false
        onTriggered: {
            root.scrubRequestQueued = false
            root.lastScrubRequestMs = Date.now()
            root.requestProcessFrame(false)
        }
    }

    Timer {
        id: scrubSettle
        // The playhead has stopped. Ask for the frame that actually belongs to
        // this position: the coarse requests during the drag can only show the
        // keyframe at or before it.
        interval: root.scrubSettleMs
        repeat: false
        onTriggered: {
            scrubThrottle.stop()
            root.scrubRequestQueued = false
            root.requestProcessFrame(true)
        }
    }

    Connections {
        target: Backend
        function onPlayheadChanged() {
            if (!root.playing)
                root.seekPlayer()
        }
        function onClipsChanged() {
            root.seekPlayer()
            if (root.playing)
                root.startReplacementAudio()
        }
        function onAppSettingsChanged() {
            if (root.playing)
                root.startReplacementAudio()
        }
    }

    // Refresh the animated motion transform when a keyframe is added, moved or
    // deleted while the playhead is stationary.
    Connections {
        target: Backend.keyframeEngine
        function onKeyframesChanged(clipId) {
            if (root.activeClip && String(clipId) === String(root.activeClip.id))
                root.kfRevision += 1
        }
    }

    Timer {
        // VideoOutput renders at the source frame rate. The UI playhead needs
        // fewer updates, which leaves more CPU/GPU time for actual decoding.
        interval: Backend.videoPreviewHelper.uiTickInterval
        repeat: true
        running: root.playing
        onTriggered: {
            if (!root.activeClip) {
                Backend.playheadMs = Math.min(Backend.durationMs,
                                               Backend.playheadMs + interval)
                if (Backend.playheadMs >= Backend.durationMs)
                    root.finishSequencePlayback()
                return
            }

            if (root.activeMedia.kind === "image") {
                Backend.playheadMs = Math.min(Backend.durationMs,
                                               Backend.playheadMs + interval)
            } else if (root.useProcessPreview) {
                // The playhead follows the picture, not the button press.
                //
                // This used to be a pure wall clock: processPlaybackWallMs was
                // stamped in startProcessPlayback(), before the decoder had
                // opened the container, seeked and decoded anything. All of that
                // time counted as elapsed playback, so the playhead - and the
                // timecode, the subtitle overlay and the still rendered on pause
                // - sat that far ahead of the frame on screen, which is why
                // pausing looked like the image jumped forward.
                //
                // previewPresentedSourceMs() is the pts of the frame the decoder
                // actually published. Each new one re-anchors the clock; between
                // frames the wall clock still interpolates, so the timecode moves
                // smoothly at the 50 ms UI tick instead of stepping at the source
                // frame rate.
                const shown = Backend.previewPresentedSourceMs()
                if (shown >= 0 && shown !== root.presentedSourceMs) {
                    root.presentedSourceMs = shown
                    root.processPlaybackTimelineMs =
                            Number(root.activeClip.startMs)
                            + Math.max(0, shown - Number(
                                           root.activeClip.sourceInMs || 0))
                    root.processPlaybackWallMs = Date.now()
                } else if (shown < 0
                           && Date.now() - root.processPlaybackWallMs < 600) {
                    // Nothing on screen yet. Holding the playhead still is the
                    // honest answer while the first frame is being decoded -
                    // advancing it and snapping back on arrival would show the
                    // timecode running and then jumping backwards. The 600 ms
                    // bound is what keeps an audio-only clip, which publishes no
                    // video frame at all, from freezing the playhead forever.
                    return
                }
                Backend.playheadMs = Math.min(
                            Backend.durationMs,
                            root.processPlaybackTimelineMs
                            + Math.max(0, Date.now()
                                       - root.processPlaybackWallMs))
                // Only the monitor can convert the decoder's source pts into a
                // timeline position, so it is the monitor that hands the trace
                // the pair to compare. A no-op without CUTPRO_PLAYBACK_TRACE.
                if (root.presentedSourceMs >= 0)
                    Backend.tracePlaybackDrift(
                                Number(root.activeClip.startMs)
                                + Math.max(0, root.presentedSourceMs
                                           - Number(root.activeClip.sourceInMs
                                                    || 0)))
            } else {
                Backend.playheadMs = root.activeClip.startMs
                        + Math.max(0, player.position
                                   - root.activeClip.sourceInMs)
            }
            if (Backend.playheadMs >= Backend.durationMs)
                root.finishSequencePlayback()
        }
    }

    onPlayingChanged: {
        console.info("monitor: playing ->", playing, "at",
                     Math.round(Backend.playheadMs))
        if (playing) {
            if (root.useProcessPreview) {
                player.stop()
                root.startProcessPlayback()
            } else if (activeMedia && activeMedia.kind !== "image") {
                Qt.callLater(function() { player.play() })
                root.startReplacementAudio()
            }
        } else {
            player.pause()
            root.replacementAudioKey = ""
            // Snap the playhead back onto the frame that was on screen, and only
            // ever backwards.
            //
            // Measured, with CUTPRO_PLAYBACK_TRACE on: the click writes
            // playing = false at, say, playhead 6458 ms, and by the time this
            // handler reads the decoder - 36 ms later, because the write goes
            // through the metaobject system and the decode thread is not stopped
            // yet - the decoder has published the *next* frame at 6541 ms. That
            // frame was never the picture the user clicked on. Taking it moved the
            // playhead 42-83 ms forward on every pause, and requestFrameNow()
            // then rendered that later frame, which is exactly the "the image goes
            // ahead when I pause" report.
            //
            // The playhead during playback is the last published frame plus
            // interpolation, so it is never behind the picture. A presented
            // position that is *ahead* of it therefore always means a frame that
            // arrived after the click, and must be ignored; one that is behind it
            // is the frame boundary to land on.
            const shown = root.useProcessPreview
                        ? Backend.previewPresentedSourceMs() : -1
            if (shown >= 0 && root.activeClip) {
                const shownTimelineMs = Number(root.activeClip.startMs)
                        + Math.max(0, shown - Number(
                                       root.activeClip.sourceInMs || 0))
                if (shownTimelineMs < Backend.playheadMs)
                    Backend.playheadMs = shownTimelineMs
            }
            Backend.stopPreviewDecode()
            if (root.useProcessPreview)
                root.requestFrameNow(true)
        }
    }

    // An effects edit re-renders the still and nothing more. requestProcessFrame()
    // is already guarded (armed, paused, has media) and the C++ scrub service
    // keeps the container warm, so this costs one decode of one frame instead of
    // an audio-device teardown and a re-open.
    onActiveClipChanged: {
        if (!root.useProcessPreview || root.playing)
            return
        // scrubSettle is running only while the playhead is actually moving. Mid
        // drag this is a clip boundary being crossed, so stay on the cheap
        // cadence; otherwise it is an edit to the clip under a stationary
        // playhead, and re-rendering it at a keyframe instead of the frame the
        // user is looking at would read as the picture jumping backwards.
        if (scrubSettle.running)
            root.scheduleProcessFrame()
        else
            root.requestFrameNow(true)
    }

    onActiveSourceKeyChanged: {
        // The key is what decides whether the decoder has to be torn down, so
        // when it changes mid-playback the log has to say which field moved.
        console.info("monitor: source key", root.lastSourceKey, "->",
                     root.activeSourceKey)
        root.lastSourceKey = root.activeSourceKey
        player.stop()
        Backend.stopPreviewDecode()
        root.processPreviewArmed = false
        if (root.playing && root.useProcessPreview)
            root.startProcessPlayback()
        else if (root.playing)
            root.startReplacementAudio()
        else if (root.useProcessPreview && root.activeMedia
                 && root.activeMedia.kind === "video") {
            // A paused monitor used to stay black until the user pressed play or
            // dragged the scrubber, because nothing armed the process preview.
            // Opening a source is now cheap and cancellable (warm decode session
            // + capped probe), so prewarm it and ask for the frame under the
            // playhead right away: a newly selected clip shows a picture at once,
            // however long the file is.
            Backend.prewarmPreviewSource(String(root.activeMedia.path),
                                         Number(root.activeMedia.width || 0),
                                         Number(root.activeMedia.height || 0))
            root.processPreviewArmed = true
            // Coarse frame now, precise frame once the playhead settles - which
            // is immediately when the clip was just selected, and after the drag
            // when the playhead crossed into this source mid-scrub.
            root.scheduleProcessFrame()
        }
    }

    Component.onDestruction: {
        Backend.stopPreviewDecode()
        // The stage is re-hosted in the window's overlay while full screen, which
        // outlives this panel: leaving the flag set would keep a black layer over
        // a panel that no longer has a picture to put in it.
        Backend.videoFullScreen = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelTabs {
            Layout.fillWidth: true
            tabs: ["Source: (no clips)", "Program: " + Backend.sequenceName]
            currentIndex: 1
            overflowIcon: "menu"
        }

        // Where the stage sits while it is docked in this panel. A layout, like the
        // full-screen slot, so the stage keeps sizing itself the same way in both
        // and moving it needs no geometry changes. A placeholder rather than
        // re-parenting straight back into the column, because re-parenting appends
        // to the children and a column lays out in that order - the stage would
        // come back below the transport bar. This never moves, so order is kept.
        ColumnLayout {
            id: monitorStage
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
        }

        // Grey monitor panel — fills the entire available space,
        // matching Premiere Pro's grey background.
        Rectangle {
            id: monitorPanel
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.viewer

            // Black sequence frame — the output canvas, centered inside
            // the grey panel.  Its aspect ratio adapts to the active
            // video (portrait, landscape, square, etc.), exactly like
            // Premiere Pro's parent/child box model.
            Rectangle {
                id: viewer
                // Compute the aspect ratio from the active media.
                // Falls back to 16:9 when no clip is loaded.
                readonly property real mediaAR: {
                    var m = root.activeMedia
                    if (m && m.width > 0 && m.height > 0)
                        return m.width / m.height
                    return 16 / 9
                }
                width: Math.min(parent.width,  parent.height * mediaAR)
                height: Math.min(parent.height, parent.width  / mediaAR)
                anchors.centerIn: parent
                color: "#000000"

                // The decoder decodes at the source's own resolution up to the
                // size this frame is drawn at, so it has to be told that size:
                // without it the preview was bounded by a constant and then
                // upscaled by the scene graph, which is what made the monitor
                // softer than the file. Debounced, because a window drag emits a
                // new size every frame and each report can change the decode
                // geometry mid-playback.
                onWidthChanged: previewSurfaceReport.restart()
                onHeightChanged: previewSurfaceReport.restart()
                Component.onCompleted: previewSurfaceReport.restart()

                Timer {
                    id: previewSurfaceReport
                    interval: 180
                    onTriggered: Backend.setPreviewSurfaceSize(
                                     Math.round(viewer.width),
                                     Math.round(viewer.height))
                }

                Item {
                    id: mediaSurface
                    anchors.fill: parent
                    clip: true

                Item {
                    id: rawColorSource
                    anchors.fill: parent
                    visible: root.activeMedia && (root.activeMedia.kind === "video"
                                                  || root.activeMedia.kind === "image")

                    // Video content container — sized by Scale so only the
                    // video scales, not the letterbox. Matches Premiere Pro
                    // where the sequence frame stays fixed and the clip
                    // content scales within it.
                    Item {
                        id: videoContainer
                        property real scaleFactor: root.animatedValue("scale", 100) / 100
                        width: parent.width * scaleFactor
                        height: parent.height * scaleFactor
                        x: (parent.width - width) / 2
                           + root.animatedValue("positionX", 0) / 100 * parent.width
                        y: (parent.height - height) / 2
                           + root.animatedValue("positionY", 0) / 100 * parent.height
                        rotation: root.animatedValue("rotation", 0)
                        opacity: root.animatedValue("opacity", 100) / 100
                        transformOrigin: Item.Center
                        transform: Scale {
                            origin.x: videoContainer.width / 2
                            origin.y: videoContainer.height / 2
                            xScale: root.activeEffects.horizontalFlip ? -1 : 1
                            yScale: root.activeEffects.verticalFlip ? -1 : 1
                        }

                        Image {
                            anchors.fill: parent
                            anchors.margins: 8
                            source: root.useProcessPreview && root.activeMedia
                                    ? (root.activeMedia.thumbnailUrl || "") : ""
                            fillMode: Image.PreserveAspectFit
                            visible: root.useProcessPreview
                                     && Backend.previewFrameUrl === ""
                            asynchronous: true
                        }

                        Image {
                            anchors.fill: parent
                            source: root.useProcessPreview
                                    ? Backend.previewFrameUrl : ""
                            fillMode: Image.PreserveAspectFit
                            visible: root.useProcessPreview
                                     && Backend.previewFrameUrl !== ""
                            asynchronous: false
                            cache: false
                        }

                        VideoOutput {
                            id: videoOutput
                            anchors.fill: parent
                            fillMode: VideoOutput.PreserveAspectFit
                            visible: root.activeMedia && root.activeMedia.kind === "video"
                                     && !root.useProcessPreview
                        }

                        Image {
                            anchors.fill: parent
                            anchors.margins: 8
                            source: root.activeMedia && root.activeMedia.kind === "image" ? root.sourceUrl : ""
                            fillMode: Image.PreserveAspectFit
                            visible: root.activeMedia && root.activeMedia.kind === "image"
                            asynchronous: true
                        }
                    }
                }

                MultiEffect {
                    anchors.fill: parent
                    source: rawColorSource
                    // The source already carries the clip transform above.
                    // Applying Scale/Rotation/Opacity again here doubled the
                    // transform whenever an effect preview was active.
                    blurEnabled: root.effectValue("blur", 0) > 0.001
                                 || root.stackHas("gaussian_blur")
                                 || root.stackHas("box_blur")
                                 || root.stackHas("directional_blur")
                                 || root.stackHas("smart_blur")
                    blur: Math.max(root.effectValue("blur", 0) / 100,
                                   root.stackValue("gaussian_blur", "amount", 0) / 30,
                                   root.stackValue("box_blur", "radius", 0) / 30,
                                   root.stackValue("directional_blur", "radius", 0) / 40,
                                   root.stackValue("smart_blur", "radius", 0) / 5)
                    visible: root.activeMedia
                             && (root.activeMedia.kind === "video"
                                 || root.activeMedia.kind === "image")
                              && (root.previewColorEffectActive()
                                  || root.stackPreviewEffectActive())
                    brightness: Math.max(-1, Math.min(1,
                                root.sectionColorValue("basicEnabled", "exposure", 0) * 0.08
                                + root.sectionColorValue("basicEnabled", "whites", 0) * 0.0012
                                + root.sectionColorValue("basicEnabled", "blacks", 0) * 0.0008
                                + root.sectionColorValue("creativeEnabled", "fade", 0) * 0.001
                                + root.stackValue("brightness_contrast",
                                                  "brightness", 0) / 100
                                + root.stackValue("exposure", "exposure", 0) * 0.25
                                + root.stackValue("hue_saturation",
                                                  "brightness", 0) * 0.25))
                    contrast: Math.max(-1, Math.min(1,
                              root.sectionColorValue("basicEnabled", "contrast", 0) / 100
                              - root.sectionColorValue("creativeEnabled", "fade", 0) / 180
                              + root.stackValue("brightness_contrast",
                                                "contrast", 0) / 100))
                    saturation: Math.max(-1, Math.min(1,
                                root.stackHas("monochrome") ? -1
                                : (root.sectionColorValue("basicEnabled", "saturation", 100) - 100) / 100
                                  + root.sectionColorValue("creativeEnabled", "vibrance", 0) / 200
                                  + (root.sectionColorValue("creativeEnabled", "creativeSaturation", 100) - 100) / 100
                                  + (root.stackValue("brightness_contrast",
                                                     "saturation", 100) - 100) / 100
                                  + (root.stackValue("hue_saturation",
                                                     "saturation", 100) - 100) / 100
                                  + root.stackValue("vibrance", "intensity", 0) / 200))
                    colorization: Math.min(0.18, Math.abs(root.sectionColorValue("basicEnabled", "temperature", 0)) / 550)
                    colorizationColor: root.sectionColorValue("basicEnabled", "temperature", 0) >= 0 ? "#ffad72" : "#7bbcff"
                }

                Image {
                    anchors.centerIn: parent
                    source: "../../assets/icons/music.svg"
                    sourceSize.width: 48
                    sourceSize.height: 48
                    opacity: 0.45
                    visible: root.activeMedia && root.activeMedia.kind === "audio"
                }

                Text {
                    anchors.centerIn: parent
                    text: root.timecode(Backend.playheadMs)
                    visible: !root.activeMedia
                    color: Qt.rgba(1, 1, 1, 0.3)
                    font.pixelSize: Theme.fsLg
                    font.family: Theme.monoFont
                    font.letterSpacing: 3
                }
            }

            MouseArea {
                anchors.fill: parent
                z: 1
                acceptedButtons: Qt.LeftButton
                onClicked: root.captionSelected = false
            }

            Repeater {
                model: root.activeEffectStack
                // One live blur region per effect instance, and each one owns a
                // texture: this is the most expensive per-item model in the
                // program monitor, so its count is worth more than most.
                onCountChanged: ModelGuard.note("monitor.effectStack", count)
                delegate: CustomBlurRegion {
                    required property var modelData
                    anchors.fill: viewer
                    z: 2
                    sourceItem: rawColorSource
                    active: root.activeMedia !== null
                            && modelData.enabled !== false
                            && modelData.definitionId === "custom_blur"
                    readonly property var mask: root.customBlurMask(modelData)
                    maskX: mask.x
                    maskY: mask.y
                    maskWidth: mask.width
                    maskHeight: mask.height
                    blurAmount: root.customBlurAmount(modelData)
                }
            }

            // The same regions, but coming from the effect track instead of from
            // the clip's own stack. The bar's extent is what decides when the blur
            // is on, which is exactly the rule the export applies.
            Repeater {
                model: root.effectTrackBlurs
                onCountChanged: ModelGuard.note("monitor.effectTrackBlurs", count)
                delegate: CustomBlurRegion {
                    required property var modelData
                    anchors.fill: viewer
                    z: 2
                    sourceItem: rawColorSource
                    active: root.activeMedia !== null
                            && Backend.playheadMs >= modelData.startMs
                            && Backend.playheadMs < modelData.endMs
                    readonly property var mask: root.customBlurMask(modelData.instance)
                    maskX: mask.x
                    maskY: mask.y
                    maskWidth: mask.width
                    maskHeight: mask.height
                    blurAmount: root.ownerInstanceValue(modelData.clipId,
                                                        modelData.instance,
                                                        "amount", 12)
                }
            }

            SubtitleBlurRegion {
                id: subtitleBlurRegion
                anchors.fill: parent
                z: 2
                sourceItem: mediaSurface
                targetItem: captionBackground
                active: Backend.captionBlurEnabled
                        && root.activeMedia !== null
                        && (root.activeMedia.kind === "video"
                            || root.activeMedia.kind === "image")
                trackingEnabled: Backend.captionBlurTrackingEnabled
                manualX: Backend.captionBlurRegionX
                manualY: Backend.captionBlurRegionY
                manualWidth: Backend.captionBlurRegionWidth
                manualHeight: Backend.captionBlurRegionHeight
                blurStrength: Backend.captionBlurStrength
                padding: Backend.captionBlurPadding
            }

                Item {
                    id: captionLayer
                    anchors.fill: parent
                    visible: root.activeSubtitle !== null
                    z: 3

                    function clampX(value, itemWidth) {
                        return Math.max(8, Math.min(parent.width - itemWidth - 8, value))
                    }

                    function clampY(value, itemHeight) {
                        return Math.max(8, Math.min(parent.height - itemHeight - 8, value))
                    }

                    Rectangle {
                        id: captionBackground
                        property bool dragging: false
                        property real dragX: 0
                        property real dragY: 0
                        width: Math.min(parent.width - 48,
                                        Math.max(120, captionText.implicitWidth + 28))
                        height: captionText.implicitHeight + 18
                        x: dragging
                           ? dragX
                           : Backend.captionPosition === "custom"
                             ? captionLayer.clampX(Backend.captionPositionX * parent.width
                                                   - width / 2, width)
                             : (parent.width - width) / 2
                        y: dragging
                           ? dragY
                           : Backend.captionPosition === "top"
                             ? 28
                             : Backend.captionPosition === "center"
                               ? Math.max(8, (parent.height - height) / 2)
                               : Backend.captionPosition === "custom"
                                 ? captionLayer.clampY(Backend.captionPositionY * parent.height
                                                       - height / 2, height)
                                 : Math.max(8, parent.height - height - 28)
                        radius: Theme.radiusSm
                        color: Backend.captionBackgroundVisible
                               ? Backend.captionBackgroundColor : "transparent"

                    Text {
                        id: captionText
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        anchors.topMargin: 9
                        anchors.bottomMargin: 9
                        text: root.activeSubtitle ? root.activeSubtitle.text : ""
                        color: Backend.captionTextColor
                        font.family: Backend.captionFontFamily
                        font.pixelSize: Math.min(Backend.captionFontSize,
                                                 Math.max(12, viewer.height * 0.18))
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

                    MouseArea {
                        id: captionDragArea
                        anchors.fill: captionBackground
                        z: 20
                        acceptedButtons: Qt.LeftButton
                        preventStealing: true
                        hoverEnabled: true
                        AppCursor.name: containsMouse || pressed
                                        ? "CrossArrow" : ""

                        onPressed: mouse => {
                            root.captionSelected = true
                            captionBackground.dragX = captionBackground.x
                            captionBackground.dragY = captionBackground.y
                            captionBackground.dragging = true
                            grabOffsetX = mouse.x
                            grabOffsetY = mouse.y
                        }

                        onPositionChanged: mouse => {
                            if (!pressed)
                                return
                            var point = mapToItem(captionLayer, mouse.x, mouse.y)
                            captionBackground.dragX = captionLayer.clampX(
                                        point.x - grabOffsetX, captionBackground.width)
                            captionBackground.dragY = captionLayer.clampY(
                                        point.y - grabOffsetY, captionBackground.height)
                        }

                        onReleased: {
                            if (captionLayer.width > 0 && captionLayer.height > 0)
                                Backend.setCaptionPositionNormalized(
                                            (captionBackground.dragX
                                             + captionBackground.width / 2)
                                            / captionLayer.width,
                                            (captionBackground.dragY
                                             + captionBackground.height / 2)
                                            / captionLayer.height)
                            captionBackground.dragging = false
                        }

                        onCanceled: captionBackground.dragging = false

                        property real grabOffsetX: 0
                        property real grabOffsetY: 0
                    }

                    Rectangle {
                        anchors.fill: captionBackground
                        anchors.margins: -2
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.accent
                        radius: Theme.radiusSm + 2
                        visible: root.captionSelected
                        z: 21
                    }
                }
            }

            CustomBlurMaskEditor {
                id: customBlurEditor
                anchors.fill: parent
                z: 8
                readonly property var instance: root.editingCustomBlurInstance()
                readonly property var mask: root.customBlurMask(instance)
                active: instance !== null
                maskX: mask.x
                maskY: mask.y
                maskWidth: mask.width
                maskHeight: mask.height
                onMaskCommitted: (x, y, width, height) =>
                    Backend.setCustomBlurMask(Backend.customBlurEditClipId,
                                              Backend.customBlurEditInstanceId,
                                              x, y, width, height)
            }

            // Region and strength in one bar, on top of the picture: placing the
            // box and choosing how hard to blur it are one task, so the slider
            // lives here instead of only in the Effect Controls tree.
            Rectangle {
                id: blurBar
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 12
                z: 9
                visible: customBlurEditor.active
                width: barRow.implicitWidth + 24
                height: 34
                radius: Theme.radiusSm
                color: Qt.rgba(0.05, 0.06, 0.08, 0.90)
                border.width: 1
                border.color: Theme.accent

                readonly property real amount:
                    root.ownerInstanceValue(Backend.customBlurEditClipId,
                                            customBlurEditor.instance, "amount", 12)

                Row {
                    id: barRow
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Drag the box to move, its edges to resize"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsXs
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Blurriness"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsXs
                    }
                    Slider {
                        id: blurStrength
                        anchors.verticalCenter: parent.verticalCenter
                        width: 120
                        height: 16
                        from: 0
                        to: 30
                        live: true
                        Binding {
                            target: blurStrength
                            property: "value"
                            value: blurBar.amount
                            when: !blurStrength.pressed
                            restoreMode: Binding.RestoreBinding
                        }
                        onMoved: Backend.setClipEffectParameter(
                                     Backend.customBlurEditClipId,
                                     Backend.customBlurEditInstanceId,
                                     "amount", blurStrength.value)
                        background: Rectangle {
                            y: blurStrength.availableHeight / 2 - 1
                            width: blurStrength.availableWidth
                            height: 3
                            radius: 1
                            color: Theme.ecTrack
                            Rectangle {
                                width: blurStrength.visualPosition * parent.width
                                height: parent.height
                                radius: 1
                                color: Theme.accent
                            }
                        }
                        handle: Rectangle {
                            x: blurStrength.visualPosition
                               * (blurStrength.availableWidth - width)
                            y: blurStrength.availableHeight / 2 - height / 2
                            width: 11
                            height: 11
                            radius: 6
                            color: blurStrength.pressed ? "#ffffff" : "#dcdcdc"
                            border.width: 1
                            border.color: Theme.border
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        text: blurBar.amount.toFixed(1)
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsXs
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        boxSize: 22
                        glyphSize: 13
                        hoverEnabled: true
                        adobeStyle: true
                        iconName: "check"
                        onClicked: Backend.endCustomBlurMaskEdit()
                        ToolTip.visible: hovered
                        ToolTip.text: "Finish editing the blur region"
                    }
                }
            }

            Rectangle {
                x: subtitleBlurRegion.trackedRect.x
                y: subtitleBlurRegion.trackedRect.y
                width: subtitleBlurRegion.trackedRect.width
                height: subtitleBlurRegion.trackedRect.height
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(0.55, 0.68, 1.0, 0.85)
                visible: root.captionSelected
                         && Backend.captionBlurEnabled
                         && root.activeMedia !== null
                         && (Backend.captionBlurTrackingEnabled
                             ? root.activeSubtitle !== null : true)
                z: 4
            }

            Rectangle {
                id: manualBlurBox
                property bool dragging: false
                property bool resizing: false
                property real previewX: subtitleBlurRegion.manualRect.x
                property real previewY: subtitleBlurRegion.manualRect.y
                property real previewWidth: subtitleBlurRegion.manualRect.width
                property real previewHeight: subtitleBlurRegion.manualRect.height
                x: 0
                y: 0
                width: 0
                height: 0
                color: Qt.rgba(0.29, 0.56, 0.96, 0.08)
                border.width: 2
                border.color: Qt.rgba(0.45, 0.68, 1.0, 0.95)
                visible: Backend.captionBlurEnabled
                         && !Backend.captionBlurTrackingEnabled
                         && root.activeMedia !== null
                         && (root.activeMedia.kind === "video"
                             || root.activeMedia.kind === "image")
                z: 20

                Binding {
                    target: manualBlurBox
                    property: "x"
                    value: subtitleBlurRegion.manualRect.x
                    when: !manualBlurBox.dragging && !manualBlurBox.resizing
                }
                Binding {
                    target: manualBlurBox
                    property: "y"
                    value: subtitleBlurRegion.manualRect.y
                    when: !manualBlurBox.dragging && !manualBlurBox.resizing
                }
                Binding {
                    target: manualBlurBox
                    property: "width"
                    value: subtitleBlurRegion.manualRect.width
                    when: !manualBlurBox.dragging && !manualBlurBox.resizing
                }
                Binding {
                    target: manualBlurBox
                    property: "height"
                    value: subtitleBlurRegion.manualRect.height
                    when: !manualBlurBox.dragging && !manualBlurBox.resizing
                }

                MouseArea {
                    id: manualBlurDrag
                    anchors.fill: parent
                    anchors.margins: 8
                    z: 10
                    acceptedButtons: Qt.LeftButton
                    preventStealing: true
                    hoverEnabled: true
                    AppCursor.name: pressed || containsMouse
                                    ? "CrossArrow" : ""
                    drag.target: manualBlurBox
                    drag.axis: Drag.XAndYAxis
                    drag.minimumX: 0
                    drag.minimumY: 0
                    drag.maximumX: viewer.width - manualBlurBox.width
                    drag.maximumY: viewer.height - manualBlurBox.height

                    onPressed: {
                        manualBlurBox.dragging = true
                        manualBlurBox.previewWidth = manualBlurBox.width
                        manualBlurBox.previewHeight = manualBlurBox.height
                    }
                    onReleased: {
                        Backend.setCaptionBlurRegionNormalized(
                                    manualBlurBox.x / viewer.width,
                                    manualBlurBox.y / viewer.height,
                                    manualBlurBox.width / viewer.width,
                                    manualBlurBox.height / viewer.height)
                        manualBlurBox.dragging = false
                    }
                    onCanceled: manualBlurBox.dragging = false
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: 14
                    height: 14
                    color: Theme.accent
                    border.width: 1
                    border.color: "white"
                    z: 30

                    MouseArea {
                        id: manualBlurResize
                        anchors.fill: parent
                        anchors.margins: -5
                        acceptedButtons: Qt.LeftButton
                        preventStealing: true
                        hoverEnabled: true
                        AppCursor.name: "ScaleTLBR"
                        property real startWidth: 0
                        property real startHeight: 0

                        onPressed: mouse => {
                            startWidth = manualBlurBox.width
                            startHeight = manualBlurBox.height
                            manualBlurBox.previewWidth = manualBlurBox.width
                            manualBlurBox.previewHeight = manualBlurBox.height
                            manualBlurBox.resizing = true
                            mouse.accepted = true
                        }
                        onPositionChanged: mouse => {
                            if (!pressed)
                                return
                            var point = mapToItem(viewer, mouse.x, mouse.y)
                            manualBlurBox.previewWidth = Math.max(
                                        32,
                                        Math.min(viewer.width - manualBlurBox.x,
                                                 point.x - manualBlurBox.x))
                            manualBlurBox.previewHeight = Math.max(
                                        24,
                                        Math.min(viewer.height - manualBlurBox.y,
                                                 point.y - manualBlurBox.y))
                        }
                        onReleased: {
                            Backend.setCaptionBlurRegionNormalized(
                                        manualBlurBox.x / viewer.width,
                                        manualBlurBox.y / viewer.height,
                                        manualBlurBox.previewWidth / viewer.width,
                                        manualBlurBox.previewHeight / viewer.height)
                            manualBlurBox.resizing = false
                        }
                        onCanceled: manualBlurBox.resizing = false
                    }
                }
            }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 12
            Layout.topMargin: 8

            Rectangle {
                id: scrubber
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                height: 4
                radius: 2
                color: Theme.bgPrimary

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * (Backend.durationMs > 0 ? Backend.playheadMs / Backend.durationMs : 0)
                    radius: 2
                    color: Qt.rgba(0.29, 0.56, 0.96, 0.7)
                }
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: Theme.textPrimary
                    border.color: Theme.border
                    anchors.verticalCenter: parent.verticalCenter
                    x: (parent.width - width) * (Backend.durationMs > 0 ? Backend.playheadMs / Backend.durationMs : 0)
                }
            }

            MouseArea {
                id: scrubArea
                anchors.fill: parent
                preventStealing: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                onPressed: (mouse) => root.beginScrub(
                               Math.max(0, mouse.x - 12), Math.max(1, width - 24))
                onPositionChanged: (mouse) => {
                    if (pressed)
                        root.seekFromScrubber(
                                    Math.max(0, mouse.x - 12),
                                    Math.max(1, width - 24))
                }
                onReleased: (mouse) => root.finishScrub(
                                Math.max(0, mouse.x - 12),
                                Math.max(1, width - 24), true)
                onCanceled: root.finishScrub(0, 1, false)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.transportHeight
            color: "transparent"

            // Playhead on the left, duration on the right, buttons still centred
            // under the picture. Anchored to the edges rather than laid out in one
            // row with the buttons, because a row would let the width of a
            // timecode decide where the play button sits.
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                // Hidden instead of overlapping when the panel is too narrow to
                // hold both timecodes beside the buttons. Both read the same
                // format, so one width answers for the pair.
                visible: parent.width - transportButtons.width >= width * 2 + 48
                text: root.timecode(Backend.playheadMs)
                color: Theme.accent
                font.pixelSize: Theme.fsMd
                font.family: Theme.monoFont
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                visible: parent.width - transportButtons.width >= width * 2 + 48
                text: root.timecode(Backend.durationMs)
                color: Theme.textSecondary
                font.pixelSize: Theme.fsMd
                font.family: Theme.monoFont
            }

            RowLayout {
                id: transportButtons
                anchors.centerIn: parent
                spacing: 4

                IconButton { iconName: "chevrons-left"; boxSize: 28; glyphSize: 16; restColor: Theme.textPrimary; onClicked: Backend.playheadMs = 0 }
                IconButton { iconName: "skip-back"; boxSize: 28; glyphSize: 16; restColor: Theme.textPrimary; onClicked: Backend.playheadMs = Math.max(0, Backend.playheadMs - 1000) }
                Button {
                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    implicitWidth: 32
                    implicitHeight: 32
                    flat: true
                    enabled: Backend.clips.length > 0
                    display: AbstractButton.IconOnly
                    icon.source: root.playing ? "../../assets/icons/pause.svg" : "../../assets/icons/play.svg"
                    icon.width: 16
                    icon.height: 16
                    icon.color: Theme.textPrimary
                    background: Rectangle { radius: Theme.radiusMd; color: Theme.hover }
                    onClicked: {
                        if (root.playing)
                            Backend.playing = false
                        else
                            root.startPlayback()
                    }
                }
                IconButton { iconName: "skip-forward"; boxSize: 28; glyphSize: 16; restColor: Theme.textPrimary; onClicked: Backend.playheadMs = Math.min(Backend.durationMs, Backend.playheadMs + 1000) }
                IconButton { iconName: "chevrons-right"; boxSize: 28; glyphSize: 16; restColor: Theme.textPrimary; onClicked: Backend.playheadMs = Backend.durationMs }
                IconButton {
                    iconName: "maximize-2"
                    boxSize: 28
                    glyphSize: 15
                    Layout.leftMargin: 4
                    active: Backend.videoFullScreen
                    ToolTip.visible: hovered
                    ToolTip.text: Backend.videoFullScreen
                                  ? "Leave full screen (Esc)" : "Play full screen"
                    onClicked: Backend.videoFullScreen = !Backend.videoFullScreen
                }
            }
        }
    }

    // Full-screen playback. The stage is moved here rather than copied: the
    // MediaPlayer feeds one VideoOutput, so a second one could not show the same
    // picture, and moving the item it lives in keeps playback running instead of
    // re-opening the source. Everything that draws over the picture - the blur
    // regions, the mask editor, the caption boxes - is a sibling of the frame
    // inside this stage, so it travels with it and keeps anchoring to it.
    //
    // The slot is a ColumnLayout because the stage sizes itself with
    // Layout.fillWidth/fillHeight. Handing it another layout means neither end
    // needs to know where it currently lives.
    Item {
        id: cinemaHost
        // The window's overlay layer, so the picture covers every panel. Bound
        // through Window.window because that is the property that reports when
        // this panel has a window at all: the panel is built by a Loader, so the
        // first evaluation can run before it is in the scene, and an item left
        // without a parent would never be shown again. Until then it stays where
        // it was declared, which is invisible anyway.
        parent: root.Window.window ? root.Overlay.overlay : root
        // Explicit geometry rather than anchors: the overlay is not known until
        // this panel has a window, and anchoring to a null parent is a warning.
        x: 0
        y: 0
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0
        visible: Backend.videoFullScreen
        z: 40

        Rectangle {
            anchors.fill: parent
            color: "#000000"
        }

        // The stage only takes left clicks, so this is what stops a right or
        // middle click from reaching the panels the picture is covering.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        ColumnLayout {
            id: cinemaSlot
            anchors.fill: parent
            spacing: 0
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 48
            text: "Esc to exit  ·  Space to play"
            color: Qt.rgba(1, 1, 1, 0.72)
            font.pixelSize: Theme.fsMd
            opacity: cinemaHintTimer.running ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 400 } }
        }

        Timer {
            id: cinemaHintTimer
            interval: 2600
        }
    }

    // Escape is handled once for the whole window (main.qml), so it is not
    // repeated here - two shortcuts on one key is an ambiguous overload and
    // neither fires reliably. Space has no owner in this window, and full screen
    // is the one place the transport bar is not reachable.
    Shortcut {
        sequence: "Space"
        enabled: Backend.videoFullScreen
        onActivated: {
            if (root.playing)
                Backend.playing = false
            else
                root.startPlayback()
        }
    }

    // Which container currently holds the monitor stage. A bound property rather
    // than a Connections handler, so the stage cannot be left behind in the
    // overlay by a flag change this panel did not see.
    readonly property Item videoFullScreenHost:
        Backend.videoFullScreen ? cinemaSlot : monitorStage

    function applyVideoFullScreenHost() {
        if (!monitorPanel || !root.videoFullScreenHost
                || monitorPanel.parent === root.videoFullScreenHost)
            return
        monitorPanel.parent = root.videoFullScreenHost
        if (root.videoFullScreenHost === cinemaSlot)
            cinemaHintTimer.restart()
    }

    // The stage is declared in the column and moved into the placeholder here, so
    // the docked case does not depend on the order the bindings happen to run in.
    Component.onCompleted: root.applyVideoFullScreenHost()

    onVideoFullScreenHostChanged: root.applyVideoFullScreenHost()
}
