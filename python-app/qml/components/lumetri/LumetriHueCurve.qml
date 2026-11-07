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
    property real value: 0
    property real from: -100
    property real to: 100
    property string mode: "Hue Vs Sat"
    signal valueCommitted(real value)
    implicitWidth: 188
    implicitHeight: 78
    property real editValue: value

    onValueChanged: if (!mouse.pressed) editValue = value

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = Theme.bgPrimary
            ctx.fillRect(0, 0, width, height)
            var gradient = ctx.createLinearGradient(0, 0, width, 0)
            gradient.addColorStop(0, "#e45d5d")
            gradient.addColorStop(0.18, "#e5d35d")
            gradient.addColorStop(0.38, "#62d178")
            gradient.addColorStop(0.58, "#5bb9df")
            gradient.addColorStop(0.8, "#826dde")
            gradient.addColorStop(1, "#e45d5d")
            ctx.fillStyle = gradient
            ctx.globalAlpha = 0.28
            ctx.fillRect(0, 0, width, height)
            ctx.globalAlpha = 1
            ctx.strokeStyle = Theme.border
            ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
            ctx.strokeStyle = Theme.accent
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(0, height / 2)
            var range = Math.max(Math.abs(root.from), Math.abs(root.to))
            ctx.lineTo(width / 2, height / 2 - root.editValue / range * (height * 0.42))
            ctx.lineTo(width, height / 2)
            ctx.stroke()
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        function moveTo(py) {
            root.editValue = Math.max(root.from, Math.min(root.to, (0.5 - py / height) * (root.to - root.from)))
            canvas.requestPaint()
        }
        onPressed: moveTo(mouseY)
        onPositionChanged: if (pressed) moveTo(mouseY)
        onReleased: root.valueCommitted(root.editValue)
    }
}
