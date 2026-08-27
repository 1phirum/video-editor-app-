//qmllint disable
import QtQuick
import QtQuick.Layouts
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

    implicitWidth: 84
    implicitHeight: 84

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
            var outerRadius = Math.max(1, Math.min(width, height) / 2 - 4)
            var innerRadius = outerRadius * 0.55
            var thickness = outerRadius - innerRadius
            var midRadius = innerRadius + thickness / 2
            
            ctx.lineWidth = thickness
            for (var i = 0; i < 360; ++i) {
                var start = (i - 1) * Math.PI / 180
                var end = (i + 1.5) * Math.PI / 180
                ctx.beginPath()
                ctx.arc(cx, cy, midRadius, start, end)
                // Red at top (-90 degrees) -> Hue 0
                var hue = 1.0 - ((i + 90) % 360) / 360.0
                ctx.strokeStyle = Qt.hsla(hue, 1.0, 0.5, 1.0)
                ctx.stroke()
            }
            
            ctx.lineWidth = 1
            ctx.strokeStyle = Theme.border
            ctx.beginPath()
            ctx.arc(cx, cy, outerRadius, 0, Math.PI * 2)
            ctx.stroke()
            
            ctx.beginPath()
            ctx.arc(cx, cy, innerRadius, 0, Math.PI * 2)
            ctx.stroke()
        }
    }

    Item {
        id: hoverOverlay
        anchors.fill: parent
        opacity: mouse.containsMouse || mouse.pressed || (root.editX !== 0 || root.editY !== 0) ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 150 } }

        Rectangle {
            width: 9
            height: 1
            color: "#ffffff"
            anchors.centerIn: parent
        }
        Rectangle {
            width: 1
            height: 9
            color: "#ffffff"
            anchors.centerIn: parent
        }

        Repeater {
            model: [0, 90, 180, 270]
            delegate: Rectangle {
                width: 4
                height: 1
                color: "#ffffff"
                x: parent.width / 2 + Math.cos(modelData * Math.PI / 180) * ((Math.min(parent.width, parent.height) / 2 - 4) * 0.55) - (modelData === 0 ? 0 : modelData === 180 ? width : width / 2)
                y: parent.height / 2 + Math.sin(modelData * Math.PI / 180) * ((Math.min(parent.width, parent.height) / 2 - 4) * 0.55) - height / 2
                rotation: modelData
            }
        }
    }

    Rectangle {
        id: puck
        width: 10
        height: 10
        radius: 5
        color: "transparent"
        border.color: "#ffffff"
        border.width: 1.5
        visible: hoverOverlay.opacity > 0 && (root.editX !== 0 || root.editY !== 0 || mouse.pressed)
        
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: width / 2
            color: "transparent"
            border.color: "#000000"
            border.width: 1
        }
        
        x: root.width / 2 + root.editX / root.radiusLimit * (root.width / 2 - 10) - width / 2
        y: root.height / 2 - root.editY / root.radiusLimit * (root.height / 2 - 10) - height / 2
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
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
        
        onDoubleClicked: {
            root.editX = 0
            root.editY = 0
            root.positionCommitted(0, 0)
        }
    }
}
