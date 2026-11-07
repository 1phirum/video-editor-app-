import QtQuick
import QtQuick.Effects
import CutPro 1.0
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

Item {
    id: root

    required property Item sourceItem
    property bool active: false
    // Whether the monitor is actually advancing frames. The capture only needs to
    // re-render every frame while this is true; see the ShaderEffectSource below.
    property bool playing: false
    property real maskX: 0.30
    property real maskY: 0.35
    property real maskWidth: 0.40
    property real maskHeight: 0.30
    property real blurAmount: 12

    clip: true

    readonly property rect regionRect: Qt.rect(
        Math.max(0, Math.min(width, width * maskX)),
        Math.max(0, Math.min(height, height * maskY)),
        Math.max(0, Math.min(width * maskWidth, width * (1 - maskX))),
        Math.max(0, Math.min(height * maskHeight, height * (1 - maskY))))

    // The monitor frame only changes on discrete events while paused - a scrub, a
    // paused Lumetri/effect edit - and each bumps previewFrameRevision. Re-grab the
    // capture on those, and on a mask drag, instead of leaving live:true burning a
    // full ShaderEffectSource render every frame while the blur just sits on a
    // still. This is the most expensive per-item overlay in the monitor, so a
    // paused still should cost nothing.
    readonly property int frameRevision: Backend.previewFrameRevision
    onPlayingChanged: {
        // Freeze the capture on the accepted pause frame before turning live
        // updates off; the final Image upload may share this render turn.
        if (!root.playing && capture.visible)
            capture.scheduleUpdate()
    }
    onFrameRevisionChanged: {
        if (!root.playing && capture.visible)
            capture.scheduleUpdate()
    }
    onRegionRectChanged: {
        if (!root.playing && capture.visible)
            capture.scheduleUpdate()
    }

    ShaderEffectSource {
        id: capture
        x: root.regionRect.x
        y: root.regionRect.y
        width: root.regionRect.width
        height: root.regionRect.height
        sourceItem: root.sourceItem
        sourceRect: root.regionRect
        // Live only while the video is playing; otherwise driven by the explicit
        // scheduleUpdate() calls above, so a paused frame is captured once and held.
        live: root.playing
        visible: root.active && root.blurAmount > 0
                 && width >= 2 && height >= 2
        onVisibleChanged: {
            if (visible && !root.playing)
                capture.scheduleUpdate()
        }
    }

    MultiEffect {
        x: root.regionRect.x
        y: root.regionRect.y
        width: root.regionRect.width
        height: root.regionRect.height
        source: capture
        visible: capture.visible
        blurEnabled: true
        blur: Math.max(0.01, Math.min(1, root.blurAmount / 30))
        blurMax: 64
        autoPaddingEnabled: false
    }
}
