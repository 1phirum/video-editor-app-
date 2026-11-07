import QtQuick
import CutPro 1.0
import "../effects"

Item {
    id: root
    required property var controls
    required property Item sourceItem
    property bool playing: visible && Backend.playing
    Repeater {
        // A count, not the stack itself - see customBlurStackCount. One
        // live blur region per Custom Blur instance, and each one owns a
        // texture: this is the most expensive per-item model in the
        // program monitor, so its count is worth more than most.
        model: root.controls.customBlurStackCount
        onCountChanged: ModelGuard.note("monitor.effectStack", count)
        delegate: CustomBlurRegion {
            required property int index
            readonly property var instance:
                root.controls.customBlurStackInstance(index)
            anchors.fill: parent
            z: 2
            sourceItem: root.sourceItem
            playing: root.playing
            active: root.controls.activeMedia !== null && instance !== null
                    && instance.enabled !== false
            readonly property var mask: root.controls.customBlurMask(instance)
            maskX: mask.x
            maskY: mask.y
            maskWidth: mask.width
            maskHeight: mask.height
            blurAmount: root.controls.customBlurAmount(instance)
        }
    }

    // The same regions, but coming from the effect track instead of from
    // the clip's own stack. The bar's extent is what decides when the blur
    // is on, which is exactly the rule the export applies.
    Repeater {
        model: root.controls.effectTrackBlurs.length
        onCountChanged: ModelGuard.note("monitor.effectTrackBlurs", count)
        delegate: CustomBlurRegion {
            required property int index
            readonly property var entry:
                root.controls.effectTrackBlurs[index] || null
            anchors.fill: parent
            z: 2
            sourceItem: root.sourceItem
            playing: root.playing
            active: root.controls.activeMedia !== null && entry !== null
                    && Backend.playheadMs >= entry.startMs
                    && Backend.playheadMs < entry.endMs
            readonly property var mask:
                root.controls.customBlurMask(entry ? entry.instance : null)
            maskX: mask.x
            maskY: mask.y
            maskWidth: mask.width
            maskHeight: mask.height
            blurAmount: entry ? root.controls.ownerInstanceValue(entry.clipId,
                                                       entry.instance,
                                                       "amount", 12) : 12
        }
    }


}
