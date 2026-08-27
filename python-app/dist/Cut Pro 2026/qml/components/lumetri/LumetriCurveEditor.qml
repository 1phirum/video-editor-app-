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
    property var points: []
    property color curveColor: Theme.accent
    property bool flatIdentity: false
    property bool hueBackground: false
    signal pointsCommitted(var points)

    implicitWidth: 188
    implicitHeight: 124
    property var editPoints: defaultPoints()

    function defaultPoints() {
        return flatIdentity ? [{ x: 0, y: 0.5 }, { x: 1, y: 0.5 }]
                            : [{ x: 0, y: 0 }, { x: 1, y: 1 }]
    }
    function clonePoints(source) {
        var result = []
        if (source && source.length >= 2) {
            for (var i = 0; i < source.length; ++i)
                result.push({ x: Number(source[i].x), y: Number(source[i].y) })
        } else {
            result = defaultPoints()
        }
        result.sort(function(a, b) { return a.x - b.x })
        return result
    }
    function syncPoints() {
        if (!mouse.pressed) {
            editPoints = clonePoints(points)
            canvas.requestPaint()
        }
    }
    onPointsChanged: syncPoints()
    onFlatIdentityChanged: syncPoints()
    Component.onCompleted: syncPoints()

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = Theme.bgPrimary
            ctx.fillRect(0, 0, width, height)
            if (root.hueBackground) {
                var hue = ctx.createLinearGradient(0, 0, width, 0)
                hue.addColorStop(0, "#e25a5a")
                hue.addColorStop(0.17, "#e3cf59")
                hue.addColorStop(0.34, "#59c876")
                hue.addColorStop(0.51, "#58c6cb")
                hue.addColorStop(0.68, "#5f82de")
                hue.addColorStop(0.84, "#b05fd7")
                hue.addColorStop(1, "#e25a5a")
                ctx.globalAlpha = 0.22
                ctx.fillStyle = hue
                ctx.fillRect(0, 0, width, height)
                ctx.globalAlpha = 1
            }
            ctx.strokeStyle = Theme.border
            ctx.lineWidth = 1
            for (var i = 1; i < 4; ++i) {
                var gx = width * i / 4
                var gy = height * i / 4
                ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, height); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke()
            }
            var pts = root.editPoints
            if (!pts || pts.length < 2)
                return
            ctx.strokeStyle = root.curveColor
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(pts[0].x * width, (1 - pts[0].y) * height)
            for (var p = 1; p < pts.length; ++p)
                ctx.lineTo(pts[p].x * width, (1 - pts[p].y) * height)
            ctx.stroke()
            for (var j = 0; j < pts.length; ++j) {
                ctx.beginPath()
                ctx.arc(pts[j].x * width, (1 - pts[j].y) * height,
                        mouse.selected === j ? 5 : 4, 0, Math.PI * 2)
                ctx.fillStyle = j === mouse.selected ? "white" : root.curveColor
                ctx.fill()
                ctx.strokeStyle = Theme.bgPrimary
                ctx.stroke()
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        property int selected: -1

        function nearest(px, py) {
            var best = -1
            var distance = 13
            for (var i = 0; i < root.editPoints.length; ++i) {
                var dx = root.editPoints[i].x * width - px
                var dy = (1 - root.editPoints[i].y) * height - py
                var candidate = Math.sqrt(dx * dx + dy * dy)
                if (candidate < distance) { distance = candidate; best = i }
            }
            return best
        }
        function addPoint(px, py) {
            if (root.editPoints.length >= 16)
                return -1
            var next = root.clonePoints(root.editPoints)
            next.push({ x: Math.max(0, Math.min(1, px / width)),
                        y: Math.max(0, Math.min(1, 1 - py / height)) })
            next.sort(function(a, b) { return a.x - b.x })
            root.editPoints = next
            return nearest(px, py)
        }
        function movePoint(px, py) {
            if (selected < 0)
                return
            var next = root.clonePoints(root.editPoints)
            var minX = selected > 0 ? next[selected - 1].x + 0.005 : 0
            var maxX = selected + 1 < next.length ? next[selected + 1].x - 0.005 : 1
            next[selected] = {
                x: Math.max(minX, Math.min(maxX, px / width)),
                y: Math.max(0, Math.min(1, 1 - py / height))
            }
            root.editPoints = next
            canvas.requestPaint()
        }
        onPressed: mouseEvent => {
            selected = nearest(mouseEvent.x, mouseEvent.y)
            var control = (mouseEvent.modifiers & Qt.ControlModifier) !== 0
            if (control && selected >= 0 && root.editPoints.length > 2) {
                var next = root.clonePoints(root.editPoints)
                next.splice(selected, 1)
                root.editPoints = next
                selected = -1
                canvas.requestPaint()
                root.pointsCommitted(root.clonePoints(root.editPoints))
                return
            }
            if (selected < 0)
                selected = addPoint(mouseEvent.x, mouseEvent.y)
            movePoint(mouseEvent.x, mouseEvent.y)
        }
        onPositionChanged: mouseEvent => {
            if (pressed)
                movePoint(mouseEvent.x, mouseEvent.y)
        }
        onReleased: {
            if (selected >= 0)
                root.pointsCommitted(root.clonePoints(root.editPoints))
            selected = -1
            canvas.requestPaint()
        }
        onDoubleClicked: {
            root.editPoints = root.defaultPoints()
            root.pointsCommitted(root.clonePoints(root.editPoints))
            canvas.requestPaint()
        }
    }
}
