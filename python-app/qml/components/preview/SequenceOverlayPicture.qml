import QtQuick
import CutPro 1.0

Item {
    id: root
    property var clip: ({})
    readonly property var media: Backend.mediaById(String(clip.mediaId || ""))
    readonly property var fx: clip.effects || ({})
    property real frameWidth: parent ? parent.width : 0
    property real frameHeight: parent ? parent.height : 0
    property real scalePercent: Number(fx.scale === undefined ? 100 : fx.scale)
    property real positionX: Number(fx.positionX || 0)
    property real positionY: Number(fx.positionY || 0)
    readonly property real mediaAR: media && media.width > 0 && media.height > 0 ? media.width / media.height : 1
    readonly property real frameAR: frameWidth / Math.max(1, frameHeight)
    readonly property real fitW: mediaAR > frameAR ? frameWidth : frameHeight * mediaAR
    readonly property real fitH: fitW / mediaAR
    width: fitW * scalePercent / 100
    height: fitH * scalePercent / 100
    x: (frameWidth - width) / 2 + positionX / 100 * frameWidth
    y: (frameHeight - height) / 2 + positionY / 100 * frameHeight
    opacity: Number(fx.opacity === undefined ? 100 : fx.opacity) / 100

    Image {
        anchors.fill: parent
        source: {
            if (!root.media || !root.media.path) return ""
            var path = String(root.media.path).replace(/\\/g, "/")
            return (path.charAt(0) === "/" ? "file://" : "file:///") + encodeURI(path)
        }
        fillMode: Image.Stretch
        asynchronous: true
        cache: true
    }
}
