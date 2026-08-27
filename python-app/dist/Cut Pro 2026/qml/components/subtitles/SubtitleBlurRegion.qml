//qmllint disable
import QtQuick
import QtQuick.Effects
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

Item {
    id: root

    required property Item sourceItem
    required property Item targetItem
    property bool active: false
    property bool trackingEnabled: true
    property real blurStrength: 32
    property real padding: 10
    property real manualX: 0.30
    property real manualY: 0.35
    property real manualWidth: 0.40
    property real manualHeight: 0.30

    clip: true

    readonly property rect targetRect: {
        if (!targetItem || !targetItem.visible)
            return Qt.rect(0, 0, 0, 0)
        // Make movement an explicit binding dependency. mapToItem() alone does
        // not reliably invalidate this binding when the target is dragged.
        var targetX = targetItem.x
        var targetY = targetItem.y
        var point = targetItem.mapToItem(root, 0, 0)
        var left = Math.max(0, point.x - padding)
        var top = Math.max(0, point.y - padding)
        var right = Math.min(width, point.x + targetItem.width + padding)
        var bottom = Math.min(height, point.y + targetItem.height + padding)
        return Qt.rect(left, top, Math.max(0, right - left), Math.max(0, bottom - top))
    }
    // A centered region keeps the blur effect useful when there is no
    // subtitle track or when subtitle-bound tracking is disabled.
    readonly property rect manualRect: Qt.rect(width * manualX,
                                               height * manualY,
                                               width * manualWidth,
                                               height * manualHeight)
    readonly property rect trackedRect: trackingEnabled
                                        ? targetRect : manualRect

    ShaderEffectSource {
        id: capture
        x: root.trackedRect.x
        y: root.trackedRect.y
        width: root.trackedRect.width
        height: root.trackedRect.height
        sourceItem: root.sourceItem
        sourceRect: Qt.rect(root.trackedRect.x,
                            root.trackedRect.y,
                            root.trackedRect.width,
                            root.trackedRect.height)
        live: true
        visible: root.active && root.trackedRect.width > 0
                 && root.trackedRect.height > 0
    }

    MultiEffect {
        x: root.trackedRect.x
        y: root.trackedRect.y
        width: root.trackedRect.width
        height: root.trackedRect.height
        source: capture
        visible: root.active && root.trackedRect.width > 0
                 && root.trackedRect.height > 0
        blurEnabled: true
        blur: Math.max(0.01, Math.min(1, root.blurStrength / 64))
        blurMax: 64
        autoPaddingEnabled: false
    }
}
