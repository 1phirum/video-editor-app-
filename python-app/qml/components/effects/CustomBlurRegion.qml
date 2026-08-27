import QtQuick
import QtQuick.Effects
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

    ShaderEffectSource {
        id: capture
        x: root.regionRect.x
        y: root.regionRect.y
        width: root.regionRect.width
        height: root.regionRect.height
        sourceItem: root.sourceItem
        sourceRect: root.regionRect
        live: true
        visible: root.active && root.blurAmount > 0
                 && width >= 2 && height >= 2
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
