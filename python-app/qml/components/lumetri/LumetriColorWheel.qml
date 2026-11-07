//qmllint disable
import QtQuick
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

Item {
    id: root
    property real valueX: 0
    property real valueY: 0
    property real radiusLimit: 100
    signal positionCommitted(real x, real y)
    property real editX: valueX
    property real editY: valueY

    onValueXChanged: if (!mouse.pressed) editX = valueX
    onValueYChanged: if (!mouse.pressed) editY = valueY

    implicitWidth: 92
    implicitHeight: 92

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var cx = width / 2
            var cy = height / 2
            var radius = Math.max(1, Math.min(width, height) / 2 - 4)
            for (var i = 0; i < 360; ++i) {
                var start = (i - 1) * Math.PI / 180
                var end = (i + 1.5) * Math.PI / 180
                ctx.beginPath()
                ctx.moveTo(cx, cy)
                ctx.arc(cx, cy, radius, start, end)
                ctx.closePath()
                ctx.fillStyle = Qt.hsla(i / 360, 1.0, 0.5, 1.0)
                ctx.fill()
            }
            var shade = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius)
            shade.addColorStop(0, "rgba(128, 128, 128, 1)")
            shade.addColorStop(0.15, "rgba(128, 128, 128, 1)")
            shade.addColorStop(1, "rgba(128, 128, 128, 0)")
            ctx.beginPath()
            ctx.arc(cx, cy, radius, 0, Math.PI * 2)
            ctx.fillStyle = shade
            ctx.fill()
            ctx.strokeStyle = Theme.border
            ctx.lineWidth = 1
            ctx.stroke()
        }
    }

    Rectangle {
        id: puck
        width: 14
        height: 14
        radius: 7
        color: Theme.bgPrimary
        border.color: "#e0e0e0"
        border.width: 2
        x: root.width / 2 + root.editX / root.radiusLimit * (root.width / 2 - 10) - width / 2
        y: root.height / 2 - root.editY / root.radiusLimit * (root.height / 2 - 10) - height / 2

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: width / 2
            color: "transparent"
            border.color: Theme.bgPrimary
            border.width: 1
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        function moveTo(px, py) {
            var dx = px - width / 2
            var dy = height / 2 - py
            var maxR = Math.max(1, Math.min(width, height) / 2 - 10)
            var length = Math.sqrt(dx * dx + dy * dy)
            if (length > maxR) {
                dx *= maxR / length
                dy *= maxR / length
            }
            root.editX = Math.round(dx / maxR * root.radiusLimit)
            root.editY = Math.round(dy / maxR * root.radiusLimit)
        }
        onPressed: moveTo(mouseX, mouseY)
        onPositionChanged: if (pressed) moveTo(mouseX, mouseY)
        onReleased: root.positionCommitted(root.editX, root.editY)
    }
}
