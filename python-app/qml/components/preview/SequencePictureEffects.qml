import QtQuick
import QtQuick.Effects
import CutPro 1.0

MultiEffect {
    id: root
    readonly property var activeClip: Backend.videoPreviewHelper.activeClip || null
    readonly property var activeMedia: Backend.videoPreviewHelper.activeMedia || null
    readonly property string activeClipId: Backend.videoPreviewHelper.activeClipId
    readonly property var activeEffects: activeClip ? (activeClip.effects || {}) : ({})
    readonly property var activeLumetri: activeClip ? (activeClip.lumetri || {}) : ({})
    readonly property var activeEffectStack: activeClip ? (activeClip.effectStack || []) : []
    property int kfRevision: 0
    Connections {
        target: Backend.keyframeEngine
        function onKeyframesChanged(clipId) { ++root.kfRevision }
    }

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
                root.lumetriColorValue("basicEnabled", "exposure", 0) * 0.08
                + root.lumetriColorValue("basicEnabled", "whites", 0) * 0.0012
                + root.lumetriColorValue("basicEnabled", "blacks", 0) * 0.0008
                + root.lumetriColorValue("creativeEnabled", "fade", 0) * 0.001
                + root.stackValue("brightness_contrast",
                                  "brightness", 0) / 100
                + root.stackValue("exposure", "exposure", 0) * 0.25
                + root.stackValue("hue_saturation",
                                  "brightness", 0) * 0.25))
    contrast: Math.max(-1, Math.min(1,
              root.lumetriColorValue("basicEnabled", "contrast", 0) / 100
              - root.lumetriColorValue("creativeEnabled", "fade", 0) / 180
              + root.stackValue("brightness_contrast",
                                "contrast", 0) / 100))
    saturation: Math.max(-1, Math.min(1,
                root.stackHas("monochrome") ? -1
                : (root.lumetriColorValue("basicEnabled", "saturation", 100) - 100) / 100
                  + root.lumetriColorValue("creativeEnabled", "vibrance", 0) / 200
                  + (root.lumetriColorValue("creativeEnabled", "creativeSaturation", 100) - 100) / 100
                  + (root.stackValue("brightness_contrast",
                                     "saturation", 100) - 100) / 100
                  + (root.stackValue("hue_saturation",
                                     "saturation", 100) - 100) / 100
                  + root.stackValue("vibrance", "intensity", 0) / 200))
    colorization: Math.min(0.18, Math.abs(root.lumetriColorValue("basicEnabled", "temperature", 0)) / 550)
    colorizationColor: root.lumetriColorValue("basicEnabled", "temperature", 0) >= 0 ? "#ffad72" : "#7bbcff"

    readonly property int customBlurStackCount: {
        var stack = root.activeEffectStack
        var found = 0
        for (var i = 0; i < stack.length; ++i) {
            if (stack[i] && stack[i].definitionId === "custom_blur")
                ++found
        }
        return found
    }

    readonly property var effectTrackBlurs: {
        var result = []
        var entries = root.timelineEffectEntries
        for (var i = 0; i < entries.length; ++i) {
            if (entries[i].instance.definitionId === "custom_blur")
                result.push(entries[i])
        }
        return result
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

    function instanceValue(instance, parameterId, fallback) {
        return root.ownerInstanceValue(
                    root.activeClip ? String(root.activeClip.id || "") : "",
                    instance, parameterId, fallback)
    }
}
