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
import "../components/preview"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"

Rectangle {
    id: root
    color: Theme.bgSidebar

    // Visited workspaces stay loaded in main.qml. Only the visible monitor may
    // run a playback clock or own the shared decoder; hiding pauses local players
    // without stopping the decoder now owned by the other workspace's preview.
    readonly property bool playing: root.visible && Backend.playing
    // C++ caches these values until a clip/subtitle boundary is crossed. This
    // keeps playback ticks from scanning the entire timeline in JavaScript.
    property var activeClip: Backend.videoPreviewHelper.activeClip || null
    property var activeMedia: Backend.videoPreviewHelper.activeMedia || null
    readonly property real frameRate: activeMedia ? Number(activeMedia.frameRate || 30) : 30
    property var activeAudioClip: Backend.videoPreviewHelper.activeAudioClip || null
    property var activeSubtitle: Backend.videoPreviewHelper.activeSubtitle || null

    // Scalars pulled out of the QVariantMap-backed `activeClip`/`activeMedia`
    // once per clip boundary. Reading a field off a QVariantMap `var` property
    // goes through Qt's VariantAssociation wrapper, which re-materialises the
    // whole map (including activeClip's nested lumetri/effects sub-maps) and
    // allocates fresh V4 strings on every access -> V4 GC churn. The playback
    // tick touched these several times per frame; these bindings re-evaluate
    // only when the clip/media actually changes, so the hot path reads plain
    // numbers/strings instead. (fixes the single-clip playback stutter)
    readonly property real activeClipStartMs: activeClip ? Number(activeClip.startMs || 0) : 0
    readonly property real activeClipSourceInMs: activeClip ? Number(activeClip.sourceInMs || 0) : 0
    readonly property string activeMediaKind: activeMedia ? String(activeMedia.kind || "") : ""
    // The active clip's id as a plain string, straight from the C++ cache
    // (updated only when the picture clip changes). animatedValue() runs five
    // times per transform - scale/positionX/positionY/rotation/opacity - on
    // every 20 Hz playback tick and on every paused seek, and it read
    // `activeClip.id` twice per call; each read re-materialised the whole clip
    // map (nested lumetri/effects included) through Qt's VariantAssociation
    // wrapper, ~200 full-map rebuilds a second of pure GC churn. Reading this
    // scalar instead touches no map. (same hazard the block above was added for)
    readonly property string activeClipId: Backend.videoPreviewHelper.activeClipId

    property url sourceUrl: mediaUrl(activeMedia)
    // Overlay items live at the playhead and draw on top of the decoded base
    // frame: clips flagged overlay:true (logos/graphics/shapes, always
    // composited) and plain images that sit on a video track ABOVE the decoded
    // base picture. The preview helper resolves this set in C++ and only re-emits
    // it when a clip boundary is crossed, so the monitor no longer rescans every
    // clip (re-wrapping the whole timeline, subtitles included) on each 20 Hz tick
    // - the per-tick scan that stalled the GUI thread and glitched preview audio.
    readonly property var overlayClips: Backend.videoPreviewHelper.activeOverlayClips
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
    property bool captionSelected: false
    property var trackStateList: Backend.trackStates
    property bool scrubbingPlayhead: false
    property bool resumePlaybackAfterScrub: false
    property int transportGeneration: 0
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
           String(root.activeEffects.speed || 100),
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
        var ownerId = root.activeClipId
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
    // The same trick for clip data reached through Backend.clipById(), which is
    // Q_INVOKABLE: a binding records the properties it reads, not the methods it
    // calls, so anything resolved that way keeps its first answer forever.
    // Bumped on clipsChanged.
    property int clipRevision: 0
    // Raised by the mask editor around its commit, so the clipsChanged that comes
    // straight back out of it is recognised for what it is. A mask rect is an
    // overlay in the monitor and a crop in the export - the decoded still is
    // byte-for-byte the same - so re-asking the decoder for it only costs a
    // synchronous Image reload on the GUI thread, which is the flicker across the
    // picture that letting go of the box used to produce.
    property bool maskCommitPending: false

    function clipById(clipId) {
        if (!clipId)
            return null
        var clip = Backend.clipById(String(clipId))
        return clip.id === undefined ? null : clip
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

    // How many Custom Blur regions the clip's own stack needs, and which
    // instance each one is. Deliberately a count and a lookup rather than an
    // array model: activeClip is a whole map, so it changes on every slider tick
    // and every mask commit, and handing a Repeater a fresh JS array resets it -
    // every delegate is destroyed and rebuilt. Each delegate owns a live
    // texture, so that reset is a flash across the picture. An int stays equal
    // across those re-evaluations, so the delegates survive and their own
    // bindings carry the new values.
    readonly property int customBlurStackCount: {
        var stack = root.activeEffectStack
        var found = 0
        for (var i = 0; i < stack.length; ++i) {
            if (stack[i] && stack[i].definitionId === "custom_blur")
                ++found
        }
        return found
    }
    function customBlurStackInstance(index) {
        // Reads activeEffectStack, which is what makes a binding that calls this
        // refresh: dependencies are recorded wherever the read happens, not only
        // in the binding's own expression.
        var stack = root.activeEffectStack
        var found = 0
        for (var i = 0; i < stack.length; ++i) {
            if (!stack[i] || stack[i].definitionId !== "custom_blur")
                continue
            if (found === index)
                return stack[i]
            ++found
        }
        return null
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
        if (root.kfRevision < 0 || !root.activeClipId)
            return base
        return Backend.keyframeEngine.valueAt(
                    root.activeClipId, key,
                    Math.max(0, Number(Backend.playheadMs)), base)
    }
    function audioClipFor(clip) {
        if (!clip)
            return null
        if (clip.kind === "audio")
            return clip
        // After Extract Audio the sound is an independent clip that names the
        // video it came out of. Nothing links the two back.
        if (clip.separateAudio !== true)
            return clip
        var clips = Backend.mediaClips
        for (var i = 0; i < clips.length; ++i) {
            var candidate = clips[i]
            if (String(candidate.extractedFromClipId || "") === String(clip.id))
                return candidate
        }
        return clip
    }
    // Extract Audio handed this clip's sound to an independent A-track clip. The
    // preview decoder still plays the file's embedded stream, so it can stand in
    // for that clip - but only while the two are still in step. Once the user
    // moves or trims the extracted clip the sound belongs where they put it, and
    // the decoder has to fall silent so the clip's own player is the only thing
    // heard. Deleting the extracted clip leaves the video silent, which is what
    // CapCut does too; "Restore Clip Audio" or undo brings it back.
    readonly property bool embeddedAudioRouted: {
        if (!root.activeClip || root.activeClip.separateAudio !== true)
            return false
        var audio = root.activeAudioClip
        if (!audio || String(audio.id || "") === String(root.activeClip.id))
            return false
        return Number(audio.startMs) === Number(root.activeClip.startMs)
                && Number(audio.sourceInMs || 0)
                   === Number(root.activeClip.sourceInMs || 0)
                && Number(audio.durationMs) === Number(root.activeClip.durationMs)
    }
    // True when the video clip's own sound must not be heard from the decoder:
    // it has been extracted, and the clip that owns it now is either gone or no
    // longer sitting where the video does.
    readonly property bool embeddedAudioSilenced:
        root.activeClip && root.activeClip.separateAudio === true
        && !root.embeddedAudioRouted
    function audioEffectValue(key, fallback) {
        var values = activeAudioClip && activeAudioClip.effects
                ? activeAudioClip.effects : activeEffects
        var value = values ? values[key] : undefined
        return value === undefined || value === null ? fallback : Number(value)
    }
    function audioSourcePath() {
        return Backend.clipReplacementAudioPath(activeAudioClip || activeClip || ({}))
    }
    function replacementAudioMuted() {
        return Backend.appSettings.muteAllAudio === true
                || root.embeddedAudioSilenced
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
    // Mute, as a level rather than as a stopped stream.
    //
    // The native preview took audioEnabled and volume as start() arguments and
    // never looked at them again, so muting a track mid-playback only turned the
    // icon red - the audio kept going until the next seek - and un-muting had
    // nothing to switch back on, because a session started muted never opened an
    // audio stream at all. The stream now always runs and this level carries the
    // mute, so both directions are heard on the next pump.
    readonly property real previewAudioLevel:
        (root.voiceOverSilencesProgramme || root.replacementAudioMuted())
            ? 0.0 : root.replacementAudioVolume()
    // Silencing the programme during a voice-over take belongs in this binding,
    // not in the recorder. Backend cannot read this level back - setPreviewVolume
    // only forwards it - so a save-and-restore there would have to guess at what
    // mute, master volume and the clip's own volume effect add up to. Folded in
    // here, the end of a take is just the binding re-evaluating to whatever is
    // correct by then.
    readonly property bool voiceOverSilencesProgramme:
        Backend.voiceOverRecording
        && Backend.appSettings.voiceOverMuteMonitor === true
    onPreviewAudioLevelChanged: {
        if (root.visible)
            Backend.setPreviewVolume(root.previewAudioLevel)
    }
    function startReplacementAudio() {
        if (!root.visible || root.useProcessPreview)
            return
        var path = root.audioSourcePath()
        // Deliberately not gated on mute any more: tearing the session down for a
        // muted track is exactly what made un-muting impossible.
        if (!root.playing || path === "" || !root.activeClip) {
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
                                   root.previewAudioLevel)
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
        // clipRevision is read for its dependency, not its value. clipById() is
        // Q_INVOKABLE and a binding does not track a method call, so without it
        // this answer froze at whatever the clip held when edit mode opened: the
        // mask editor never saw its own commits, and the next press restored the
        // box to where the drag before it had started.
        if (root.clipRevision < 0)
            return null
        // Editing must follow the selected effect, rather than clipAt(). The
        // latter can resolve a different clip when visual tracks overlap.
        var clip = Backend.clipById(String(Backend.customBlurEditClipId))
        if (clip.id === undefined)
            return null
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

    // The real chain runs in the fragment stage over both video and image clips
    // (LumetriGpuGrade below), so the Lumetri terms in the MultiEffect would be a
    // second, cruder grade on top of it. They are only still wanted where the
    // shader chain cannot draw at all - the software scene graph - and even then
    // only for image clips, since video frames arrive graded by LumetriPreview.
    function lumetriPreviewNeeded() {
        return !Backend.gpuGrading && root.activeMedia
                && root.activeMedia.kind === "image"
    }

    function lumetriColorValue(section, key, fallback) {
        if (!lumetriPreviewNeeded())
            return fallback
        return sectionColorValue(section, key, fallback)
    }

    function previewColorEffectActive() {
        if (!lumetriPreviewNeeded())
            return false
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
        var replacement = Backend.clipReplacementAudioPath(clip || ({}))
        if (replacement !== "")
            return mediaUrl({path: replacement})
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
        var clips = Backend.mediaClips
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            if (clip.enabled === false
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
        var clips = Backend.clips
        // Text can stack on S1..Sn now; the highest lane is drawn on top, so it
        // is the one the preview shows when several overlap.
        var best = null
        var bestLane = -1
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            if (clip.kind === "subtitle"
                    && clip.enabled !== false
                    && root.trackEnabled(clip.track)
                    && position >= clip.startMs
                    && position < clip.startMs + clip.durationMs) {
                var lane = parseInt(String(clip.track || "S1").substring(1)) || 1
                if (lane > bestLane) {
                    bestLane = lane
                    best = clip
                }
            }
        }
        return best
    }

    function mediaForClip(clip) {
        if (!clip)
            return null
        // O(1) in C++. Walking Backend.media here cost one QVariantMap-to-JS
        // conversion per bin entry, and a generated voice track puts one entry in
        // the bin per spoken cue.
        var media = Backend.mediaById(String(clip.mediaId || ""))
        return media && media.id ? media : null
    }

    function nextClip(position) {
        var result = null
        var clips = Backend.mediaClips
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            if (clip.enabled === false
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
        if (!root.visible || Backend.durationMs <= 0)
            return
        if (Backend.playheadMs >= Backend.durationMs)
            Backend.playheadMs = 0

        Backend.playing = true
        // If playback starts in a gap, keep the playhead moving naturally.
        // The next clip's media source will load when its start is reached.
        Qt.callLater(function() {
            if (root.visible && root.playing && root.activeMedia
                    && root.activeMedia.kind !== "image"
                    && !root.useProcessPreview) {
                root.seekPlayer()
                player.play()
            }
        })
    }

    function timecode(milliseconds) {
        var frames = Math.floor((milliseconds % 1000) * root.frameRate / 1000)
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
        var speed = Math.max(0.05, Number(root.activeEffects.speed || 100) / 100)
        return Math.max(0, (Backend.playheadMs - Number(activeClip.startMs)) * speed
                        + Number(activeClip.sourceInMs || 0))
    }

    // Where in the source the playhead lands once the clip's speed is undone.
    // A retimed clip covers sourceDuration*100/speed of timeline, so one timeline
    // millisecond is `speed/100` source milliseconds - without this scaling the
    // paused/scrubbed monitor would sit on the un-sped frame and only export
    // showed the speed change. Playback keeps activeSourcePosition() as its start
    // anchor; only the still frame is retimed here.
    function activeScrubSourcePosition() {
        if (!root.activeClip)
            return 0
        // Hoisted reads: this runs on every paused seek. Reading activeClip.effects
        // three times (and .startMs/.sourceInMs) re-materialised the whole clip map
        // each access; activeEffects is already resolved once per clip, and
        // activeClipStartMs/activeClipSourceInMs are the cached scalars.
        var fx = root.activeEffects
        var speed = Number(fx && fx.speed) || 100
        if (isNaN(speed) || speed <= 0)
            speed = 100
        var localElapsed = Math.max(0, Backend.playheadMs - root.activeClipStartMs)
        return Math.max(0, localElapsed * speed / 100 + root.activeClipSourceInMs)
    }

    function requestProcessFrame(exact) {
        if (!root.visible || !root.useProcessPreview || !root.processPreviewArmed
                || !root.activeMedia || root.playing)
            return
        Backend.requestPreviewFrame(String(root.activeMedia.path),
                                    root.activeScrubSourcePosition(),
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
        if (!root.visible || !root.useProcessPreview || !root.processPreviewArmed
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

    // A discrete paused seek to a fixed position (transcript click, marker
    // jump, typed timecode): render the exact frame that belongs to it.
    //
    // Arms and prewarms the source first when it is not already armed, so a
    // jump into a clip that was never scrubbed this session still produces a
    // picture instead of leaving the previous frame on screen - requestFrameNow
    // -> requestProcessFrame is a no-op until the source is armed.
    function showExactFrameAtPlayhead() {
        if (!root.visible || !root.useProcessPreview || !root.activeMedia
                || root.activeMedia.kind === "image")
            return
        if (!root.processPreviewArmed) {
            Backend.prewarmPreviewSource(String(root.activeMedia.path),
                                         Number(root.activeMedia.width || 0),
                                         Number(root.activeMedia.height || 0))
            root.processPreviewArmed = true
        }
        root.requestFrameNow(true)
    }

    function startProcessPlayback() {
        if (!root.visible || !root.useProcessPreview || !root.activeMedia || !root.activeClip)
            return false
        // C++ reuses a paused session when the source and position still match.
        console.info("monitor: playback requested at playhead",
                     Math.round(Backend.playheadMs), "clip",
                     String(root.activeClip.id))
        root.processPreviewArmed = true
        var elapsed = Math.max(0, Backend.playheadMs
                                  - Number(root.activeClip.startMs))
        var remaining = Math.max(1, Number(root.activeClip.durationMs) - elapsed)
        return Backend.startPreviewDecode(
                    String(root.activeMedia.path),
                    String(root.activeMedia.kind),
                    root.activeSourcePosition(), remaining,
                    Number(root.activeMedia.width || 0),
                    Number(root.activeMedia.height || 0),
                    Number(root.activeMedia.frameRate || 30),
                    // Always on when the source has audio. The flag is start-time
                    // only, so making it follow mute meant a track un-muted during
                    // playback had no stream to come back to; previewAudioLevel is
                    // what mute moves now, and it is live.
                    true,
                    root.previewAudioLevel,
                    root.audioSourcePath())
    }

    function seekPlayer() {
        if (!root.visible || !activeClip || !activeMedia || activeMedia.kind === "image")
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
        if (scrubbingPlayhead)
            Backend.touchTimelineInteraction()
        var fraction = Math.max(0, Math.min(1, xPosition / scrubberWidth))
        Backend.playheadMs = fraction * Backend.durationMs
    }

    function beginScrub(xPosition, scrubberWidth) {
        if (!scrubbingPlayhead)
            Backend.beginTimelineInteraction()
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
        Backend.endTimelineInteraction()
        if (shouldResume && Backend.durationMs > 0) {
            const generation = root.transportGeneration
            Qt.callLater(function() {
                if (root.visible && !Backend.playing && generation === root.transportGeneration)
                    Backend.playing = true
            })
        }
    }

    MediaPlayer {
        id: player
        // Opening multi-gigabyte sources in Qt Multimedia can synchronously
        // parse the container on Windows. Large sources are armed only when the
        // user starts playback, so adding them to a timeline stays responsive.
        source: root.activeMedia && root.activeMedia.kind !== "image"
                ? root.playerSourceUrl : ""
        videoOutput: rawColorSource.videoOutput
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

    SequenceAudioRouter {
        enabled: root.visible
    }

    AudioOutput {
        id: originalAudioOutput
        // The QML MediaPlayer path. Same expressions as previewAudioLevel, and live
        // here already - this half of the monitor never had the bug.
        muted: root.replacementAudioMuted()
        volume: root.replacementAudioVolume()
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
            if (!root.scrubbingPlayhead && !Backend.timelineInteractionActive)
                root.requestProcessFrame(true)
        }
    }

    Connections {
        target: Backend
        function onTimelineInteractionChanged() {
            if (root.visible && !Backend.timelineInteractionActive && !root.playing)
                root.showExactFrameAtPlayhead()
        }
        function onPlayheadChanged() {
            if (!root.visible || root.playing)
                return
            // Tell a discrete paused seek apart from a live drag.
            //
            // A discrete seek - clicking a transcript subtitle, jumping to a
            // marker, typing a timecode - lands on one exact position, and the
            // monitor must show the frame that belongs to it. The old path sent
            // every paused change through seekPlayer() -> scheduleProcessFrame(),
            // a *coarse* request the scrub service answers from any cached bucket
            // within half a GOP (up to ~500 ms) of the position. So the picture
            // snapped to a nearby keyframe on the click and only corrected once
            // the settle timer's exact request finished decoding: the frame that
            // visibly shifted under the caption.
            //
            // A drag is the opposite case - the scrubber bar (scrubbingPlayhead)
            // or a timeline-ruler scrub (which feeds scrubSettle on every move) -
            // and genuinely wants the cheap coarse cadence, with a single exact
            // frame when the motion settles.
            const isDiscreteVideoSeek =
                root.useProcessPreview && root.activeMedia
                    && root.activeMedia.kind === "video"
                    && !root.scrubbingPlayhead
                    && !Backend.timelineInteractionActive
                    && !scrubSettle.running
            if (isDiscreteVideoSeek)
                root.showExactFrameAtPlayhead()
            else
                root.seekPlayer()
        }
        function onClipsChanged() {
            root.clipRevision += 1
            if (!root.visible)
                return
            // A bulk import (generated subtitle voices) republishes the timeline a
            // few times a second for as long as it runs. Answering each of those
            // with a seek meant a frame decode and a reload of the preview Image
            // several times a second, and the picture is gone while that reload is
            // in flight - the grey band the monitor showed all through generation.
            // The flag is cleared before the import's final clipsChanged, so the
            // finished timeline still gets exactly one seek.
            if (Backend.timelineImportActive)
                return
            // A live text-overlay edit republishes the timeline on every
            // keystroke, but the picture under the playhead never changes. Seeking
            // it anyway reloaded the preview Image each keystroke, which is the
            // frame flicker seen while typing. The caption overlay still updates
            // on its own binding; only the redundant decode is skipped.
            if (Backend.takePreviewSeekSuppression())
                return
            if (root.maskCommitPending)
                root.maskCommitPending = false
            else if (root.useProcessPreview && !root.playing
                     && root.processPreviewArmed && !scrubSettle.running)
                // A stationary edit - a Lumetri tweak, an effect change, a trim
                // under a still playhead - must re-render the *exact* frame the
                // user is looking at. seekPlayer() takes the cheap coarse path,
                // which the scrub service answers from any bucket within half a
                // GOP (up to ~500 ms) of the position, so a colour change on a
                // paused frame snapped the picture to a nearby frame. The jump
                // was invisible on bare footage but obvious the moment a fixed
                // caption sat over it. Same rule onActiveClipChanged already uses.
                root.requestFrameNow(true)
            else
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

    onPlayingChanged: {
        ++root.transportGeneration
        console.info("monitor: playing ->", playing, "at",
                     Math.round(Backend.playheadMs))
        if (playing) {
            scrubThrottle.stop()
            scrubSettle.stop()
            root.scrubRequestQueued = false
            if (root.useProcessPreview) {
                player.stop()
                root.startProcessPlayback()
            } else if (activeMedia && activeMedia.kind !== "image") {
                Qt.callLater(function() {
                    if (root.playing)
                        player.play()
                })
                root.startReplacementAudio()
            }
        } else {
            player.pause()
            root.replacementAudioKey = ""
            // C++ stops presentation and anchors the playhead before emitting
            // playingChanged. Keep its accepted frame without a still reload.
            scrubThrottle.stop()
            scrubSettle.stop()
            root.scrubRequestQueued = false

        }
    }

    // An effects edit re-renders the still and nothing more. requestProcessFrame()
    // is already guarded (armed, paused, has media) and the C++ scrub service
    // keeps the container warm, so this costs one decode of one frame instead of
    // an audio-device teardown and a re-open.
    onActiveClipChanged: {
        if (!root.visible || !root.useProcessPreview || root.playing)
            return
        // Same reason as onClipsChanged: nothing is decoded while a bulk import is
        // still placing clips. The picture that matters is the one at the end.
        if (Backend.timelineImportActive)
            return
        // scrubSettle is running only while the playhead is actually moving. Mid
        // drag this is a clip boundary being crossed, so stay on the cheap
        // cadence; otherwise it is an edit to the clip under a stationary
        // playhead, and re-rendering it at a keyframe instead of the frame the
        // user is looking at would read as the picture jumping backwards.
        if (root.scrubbingPlayhead || Backend.timelineInteractionActive
                || scrubSettle.running)
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
        if (!root.visible) {
            root.processPreviewArmed = false
            return
        }
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

    onVisibleChanged: {
        ++root.transportGeneration
        scrubThrottle.stop()
        scrubSettle.stop()
        root.scrubRequestQueued = false
        if (visible && !Backend.playing) {
            Qt.callLater(function() {
                if (root.visible && !Backend.playing
                        && !Backend.previewFrameMatchesPlayhead())
                    root.showExactFrameAtPlayhead()
            })
        }
    }

    Component.onDestruction: {
        if (root.scrubbingPlayhead)
            Backend.endTimelineInteraction()
        if (root.visible)
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
            tabs: [qsTr("Source: (no clips)"), qsTr("Program: %1").arg(Backend.sequenceName)]
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

                SequenceBasePicture {
                    id: rawColorSource
                    anchors.fill: parent
                    activeMedia: root.activeMedia
                    activeEffects: root.activeEffects
                    clipId: root.activeClipId
                    sourceUrl: root.sourceUrl
                    useProcessPreview: root.useProcessPreview
                    keyframeRevision: root.kfRevision
                    visible: root.activeMedia && (root.activeMedia.kind === "video"
                                                  || root.activeMedia.kind === "image")
                }

                SequencePictureEffects {
                    id: pictureEffects
                    anchors.fill: parent
                    source: rawColorSource
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

            SequenceBlurRegions {
                anchors.fill: viewer
                z: 2
                controls: pictureEffects
                sourceItem: rawColorSource
                playing: root.playing
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

            // ---- Logo / graphic overlays -------------------------------------
            // Pure-QML compositing above the base picture. Geometry mirrors the
            // export: fit into the frame, then the intrinsic scale %, then a
            // centered position offset in percent of the frame - so what is
            // dragged here is what the export renders.
            Item {
                id: overlayLayer
                anchors.fill: viewer
                clip: true
                z: 2

                // Rendered into its own texture, so moving an overlay recomposites
                // that texture instead of dirtying the region of the video Image
                // underneath it. Without this, a drag repaints the base picture's
                // node every frame and leaves smears where the decoded frame is not
                // refreshed in step with the gesture. Only paid for when there is
                // something to draw - an always-on layer is a permanent FBO the
                // size of the monitor.
                layer.enabled: root.overlayClips.length > 0
                layer.smooth: true

                Repeater {
                    model: root.overlayClips
                    delegate: SequenceOverlayPicture {
                        id: ov
                        required property var modelData
                        clip: modelData
                        readonly property bool selected: String(Backend.selectedClipId) === String(clip.id)
                        frameWidth: overlayLayer.width
                        frameHeight: overlayLayer.height
                        scalePercent: liveScale
                        positionX: livePosX
                        positionY: livePosY

                        // Live values drive the item while a gesture is active;
                        // otherwise they track the committed clip effects.
                        property bool interacting: false
                        property real liveScale: Number(fx.scale !== undefined ? fx.scale : 100)
                        property real livePosX: Number(fx.positionX || 0)
                        property real livePosY: Number(fx.positionY || 0)
                        onFxChanged: if (!interacting) {
                            liveScale = Number(fx.scale !== undefined ? fx.scale : 100)
                            livePosX = Number(fx.positionX || 0)
                            livePosY = Number(fx.positionY || 0)
                        }

                        // Keep a dragged overlay inside the picture. Position is a
                        // percentage of the frame away from centre, so with
                        // x = (frame - item)/2 + p/100*frame, staying within
                        // [0, frame - item] bounds p to +/-50*(1 - item/frame).
                        // An overlay scaled larger than the frame gets the mirrored
                        // range, so it can still be panned but never uncovers an
                        // edge. Also inside the export's own +/-100 clamp on
                        // positionX/positionY, so the preview cannot commit a value
                        // the render would ignore.
                        function clampPos(value, itemSize, frameSize) {
                            if (frameSize <= 0)
                                return 0
                            var limit = 50 * (1 - itemSize / frameSize)
                            var low = Math.max(-100, Math.min(limit, -limit))
                            var high = Math.min(100, Math.max(limit, -limit))
                            return Math.max(low, Math.min(high, value))
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: Theme.accent
                            border.width: 1.5
                            visible: ov.selected
                        }

                        // Move: drag anywhere on the overlay.
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeAllCursor
                            acceptedButtons: Qt.LeftButton
                            property real pressX: 0
                            property real pressY: 0
                            property real basePosX: 0
                            property real basePosY: 0
                            onPressed: function (mouse) {
                                Backend.selectedClipId = String(ov.clip.id)
                                ov.interacting = true
                                var p = mapToItem(overlayLayer, mouse.x, mouse.y)
                                pressX = p.x; pressY = p.y
                                basePosX = ov.livePosX; basePosY = ov.livePosY
                            }
                            onPositionChanged: function (mouse) {
                                if (!ov.interacting) return
                                var p = mapToItem(overlayLayer, mouse.x, mouse.y)
                                // Clamped against the press anchor rather than
                                // accumulated, so a pointer that runs past the edge
                                // and comes back picks the overlay up where it left
                                // it instead of drifting.
                                ov.livePosX = ov.clampPos(
                                    basePosX + (p.x - pressX) / overlayLayer.width * 100,
                                    ov.width, overlayLayer.width)
                                ov.livePosY = ov.clampPos(
                                    basePosY + (p.y - pressY) / overlayLayer.height * 100,
                                    ov.height, overlayLayer.height)
                            }
                            onReleased: {
                                ov.interacting = false
                                var cid = String(ov.clip.id)
                                var px = Math.round(ov.livePosX * 100) / 100
                                var py = Math.round(ov.livePosY * 100) / 100
                                Backend.setClipEffectSetting(cid, "positionX", px)
                                Backend.setClipEffectSetting(cid, "positionY", py)
                            }
                        }

                        // Resize: four corner handles, CapCut style. Each scales
                        // the overlay uniformly about its centre - the item is
                        // centre-anchored, so the pointer's distance from the
                        // centre maps straight onto scale no matter which corner
                        // is grabbed. Aspect ratio is preserved (uniform scale),
                        // matching how the export applies scale.
                        Repeater {
                            model: [
                                { hx: 0, vy: 0, diag: true },
                                { hx: 1, vy: 0, diag: false },
                                { hx: 0, vy: 1, diag: false },
                                { hx: 1, vy: 1, diag: true }
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                width: 12; height: 12; radius: 2
                                color: Theme.accent
                                visible: ov.selected
                                x: modelData.hx * ov.width - width / 2
                                y: modelData.vy * ov.height - height / 2
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: modelData.diag ? Qt.SizeFDiagCursor
                                                                : Qt.SizeBDiagCursor
                                    acceptedButtons: Qt.LeftButton
                                    property real startDist: 1
                                    property real startScale: 100
                                    onPressed: function (mouse) {
                                        Backend.selectedClipId = String(ov.clip.id)
                                        ov.interacting = true
                                        var cx = ov.x + ov.width / 2
                                        var cy = ov.y + ov.height / 2
                                        var p = mapToItem(overlayLayer, mouse.x, mouse.y)
                                        startDist = Math.max(4, Math.hypot(p.x - cx, p.y - cy))
                                        startScale = ov.liveScale
                                    }
                                    onPositionChanged: function (mouse) {
                                        if (!ov.interacting) return
                                        var cx = ov.x + ov.width / 2
                                        var cy = ov.y + ov.height / 2
                                        var p = mapToItem(overlayLayer, mouse.x, mouse.y)
                                        var d = Math.hypot(p.x - cx, p.y - cy)
                                        ov.liveScale = Math.max(5, Math.min(400, startScale * d / startDist))
                                    }
                                    onReleased: {
                                        ov.interacting = false
                                        var cid = String(ov.clip.id)
                                        var s = Math.round(ov.liveScale * 100) / 100
                                        Backend.setClipEffectSetting(cid, "scale", s)
                                        // Growing the overlay shrinks how far off
                                        // centre it may sit, so a position that was
                                        // legal at the old size can now hang off an
                                        // edge. Pull it back in and commit only when
                                        // the scale actually moved it.
                                        var cx = ov.clampPos(ov.livePosX, ov.width,
                                                             overlayLayer.width)
                                        var cy = ov.clampPos(ov.livePosY, ov.height,
                                                             overlayLayer.height)
                                        if (Math.abs(cx - ov.livePosX) > 0.001) {
                                            ov.livePosX = cx
                                            Backend.setClipEffectSetting(
                                                cid, "positionX", Math.round(cx * 100) / 100)
                                        }
                                        if (Math.abs(cy - ov.livePosY) > 0.001) {
                                            ov.livePosY = cy
                                            Backend.setClipEffectSetting(
                                                cid, "positionY", Math.round(cy * 100) / 100)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

                Item {
                    id: captionLayer
                    anchors.fill: parent
                    visible: root.activeSubtitle !== null
                    z: 3

                    // Live font size drives the caption while a corner resize is
                    // in progress; otherwise it tracks the committed global style.
                    property bool fontResizing: false
                    property real liveFontSize: Backend.captionFontSize
                    // Same idea for the box width the side handles set.
                    property bool boxResizing: false
                    property real liveBoxWidth: Backend.captionBoxWidth
                    // The box width in pixels, from the fraction the side handles
                    // set - the box no longer hugs the text, so widening it rewraps
                    // the text (CapCut's side-handle behaviour).
                    readonly property real boxWidthFrac: boxResizing ? liveBoxWidth
                                                                     : Backend.captionBoxWidth
                    Connections {
                        target: Backend
                        function onCaptionStyleChanged() {
                            if (!captionLayer.fontResizing)
                                captionLayer.liveFontSize = Backend.captionFontSize
                            if (!captionLayer.boxResizing)
                                captionLayer.liveBoxWidth = Backend.captionBoxWidth
                        }
                    }

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
                        // The box is the width the user set, not a hug around the
                        // text: text wraps inside it and the side handles change it.
                        width: Math.max(40, Math.min(parent.width - 8,
                                        captionLayer.boxWidthFrac * parent.width))
                        height: captionText.implicitHeight + 10
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

                    SequenceCaptionText {
                        id: captionText
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        anchors.topMargin: 5
                        anchors.bottomMargin: 5
                        cue: root.activeSubtitle
                        emSize: Math.min(captionLayer.fontResizing ? captionLayer.liveFontSize : Backend.captionFontSize,
                                         Math.max(12, viewer.height * 0.6))
                        maxWidth: Math.max(60, captionBackground.width - 28)
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

                    // Resize: four corners plus a handle at the middle of each
                    // side, the way CapCut does it, so the caption can be grabbed
                    // from whichever spot is not under the picture's subject. They
                    // all scale the same global font size and all measure distance
                    // from the box centre - so a handle pulled out grows the
                    // caption and pushed in shrinks it, whichever one it is.
                    Repeater {
                        model: [{"fx": 0, "fy": 0, "cursor": Qt.SizeFDiagCursor, "mode": "scale"},
                                {"fx": 1, "fy": 0, "cursor": Qt.SizeBDiagCursor, "mode": "scale"},
                                {"fx": 0, "fy": 1, "cursor": Qt.SizeBDiagCursor, "mode": "scale"},
                                {"fx": 1, "fy": 1, "cursor": Qt.SizeFDiagCursor, "mode": "scale"},
                                {"fx": 0, "fy": 0.5, "cursor": Qt.SizeHorCursor, "mode": "width"},
                                {"fx": 1, "fy": 0.5, "cursor": Qt.SizeHorCursor, "mode": "width"}]
                        delegate: Rectangle {
                            required property var modelData
                            width: 12; height: 12; radius: 2
                            color: Theme.accent
                            border.width: 1
                            border.color: "#ffffff"
                            visible: root.captionSelected
                            z: 22
                            x: captionBackground.width * modelData.fx - width / 2
                            y: captionBackground.height * modelData.fy - height / 2
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: modelData.cursor
                                acceptedButtons: Qt.LeftButton
                                preventStealing: true
                                readonly property bool widthMode: modelData.mode === "width"
                                property real startDist: 1
                                property real startSize: 24
                                property real startBoxFrac: 0.9
                                property real startCenterX: 0
                                onPressed: function (mouse) {
                                    root.captionSelected = true
                                    var cx = captionBackground.x + captionBackground.width / 2
                                    var cy = captionBackground.y + captionBackground.height / 2
                                    startCenterX = cx
                                    var p = mapToItem(captionLayer, mouse.x, mouse.y)
                                    if (widthMode) {
                                        // Side handle: set the box width only, so the
                                        // text rewraps. Font stays the same.
                                        captionLayer.boxResizing = true
                                        captionLayer.liveBoxWidth = Backend.captionBoxWidth
                                    } else {
                                        // Corner: uniform scale - both the font and
                                        // the box width grow/shrink by the same
                                        // factor, so the whole caption zooms like
                                        // CapCut and can be pulled right down small.
                                        captionLayer.fontResizing = true
                                        captionLayer.boxResizing = true
                                        startDist = Math.max(4, Math.hypot(p.x - cx, p.y - cy))
                                        startSize = Backend.captionFontSize
                                        startBoxFrac = Backend.captionBoxWidth
                                        captionLayer.liveFontSize = startSize
                                        captionLayer.liveBoxWidth = startBoxFrac
                                    }
                                }
                                onPositionChanged: function (mouse) {
                                    var p = mapToItem(captionLayer, mouse.x, mouse.y)
                                    if (widthMode) {
                                        if (!captionLayer.boxResizing) return
                                        // Grow symmetrically about the centre: the
                                        // half-width is the distance from centre to
                                        // the pointer, so both edges track together.
                                        var halfW = Math.abs(p.x - startCenterX)
                                        var frac = captionLayer.width > 0
                                                   ? (halfW * 2) / captionLayer.width : 0.9
                                        captionLayer.liveBoxWidth = Math.max(0.1,
                                            Math.min(1.0, frac))
                                    } else {
                                        if (!captionLayer.fontResizing) return
                                        var cy = captionBackground.y + captionBackground.height / 2
                                        var d = Math.hypot(p.x - startCenterX, p.y - cy)
                                        var factor = d / startDist
                                        captionLayer.liveFontSize = Math.max(12,
                                            Math.min(400, startSize * factor))
                                        captionLayer.liveBoxWidth = Math.max(0.1,
                                            Math.min(1.0, startBoxFrac * factor))
                                    }
                                }
                                onReleased: {
                                    if (widthMode) {
                                        captionLayer.boxResizing = false
                                        Backend.captionBoxWidth = captionLayer.liveBoxWidth
                                    } else {
                                        captionLayer.fontResizing = false
                                        captionLayer.boxResizing = false
                                        Backend.captionFontSize = Math.round(captionLayer.liveFontSize)
                                        Backend.captionBoxWidth = captionLayer.liveBoxWidth
                                    }
                                }
                                onCanceled: {
                                    captionLayer.fontResizing = false
                                    captionLayer.boxResizing = false
                                }
                            }
                        }
                    }
                }
            }

            CustomBlurMaskEditor {
                id: customBlurEditor
                // Anchored to the picture, not to the grey panel around it. The
                // mask is stored as fractions of the frame - CustomBlurRegion
                // paints it that way and the export's crop reads it that way -
                // so an editor filling the whole stage put the box somewhere the
                // blur was not on every letterboxed sequence, and a drag near
                // the picture's edge fought the backend's clamps.
                anchors.fill: viewer
                z: 8
                readonly property var instance: root.editingCustomBlurInstance()
                readonly property var mask: root.customBlurMask(instance)
                active: instance !== null
                maskX: mask.x
                maskY: mask.y
                maskWidth: mask.width
                maskHeight: mask.height
                onMaskCommitted: (x, y, width, height) => {
                    // setCustomBlurMask emits clipsChanged() before it returns,
                    // and the handler runs synchronously inside this call, so the
                    // flag is read and cleared there. Clearing it again is only
                    // insurance against a commit that never comes back.
                    root.maskCommitPending = true
                    Backend.editSelectedCustomBlurMask(Backend.customBlurEditClipId,
                                              Backend.customBlurEditInstanceId,
                                              x, y, width, height)
                    root.maskCommitPending = false
                }
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
                        text: qsTr("Drag the box to move, its edges to resize")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsXs
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Blurriness")
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
                        onMoved: Backend.editSelectedClipEffectParameter(
                                     Backend.customBlurEditClipId,
                                     Backend.customBlurEditInstanceId,
                                     "amount", blurStrength.value)
                        // Live preview must stream to the backend each tick, so
                        // the gesture is bracketed to keep it one undo step
                        // instead of one full-project snapshot per mouse-move.
                        onPressedChanged: blurStrength.pressed
                                          ? Backend.beginLiveParameterEdit()
                                          : Backend.endLiveParameterEdit()
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
                        ToolTip.text: qsTr("Finish editing the blur region")
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
                    enabled: Backend.clipCount > 0
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
                                  ? qsTr("Leave full screen (Esc)") : qsTr("Play full screen")
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
            text: qsTr("Esc to exit  ·  Space to play")
            color: Qt.rgba(1, 1, 1, 0.72)
            font.pixelSize: Theme.fsMd
            opacity: cinemaHintTimer.running ? 1 : 0
            visible: opacity > 0.01
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
        sequence: (Backend.appSettings, Backend.shortcut("playback.togglePlay"))
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
