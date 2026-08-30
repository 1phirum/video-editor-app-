import QtQuick
import CutPro 1.0
import "../../theme"
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

Item {
    id: root

    property bool active: false
    property real maskX: 0.30
    property real maskY: 0.35
    property real maskWidth: 0.40
    property real maskHeight: 0.30
    property real draftX: 0
    property real draftY: 0
    property real draftWidth: 0
    property real draftHeight: 0
    property bool dragging: false
    signal maskCommitted(real x, real y, real width, real height)

    visible: active

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function syncFromMask() {
        if (dragging || width <= 0 || height <= 0)
            return
        draftX = clamp(maskX, 0, 0.98) * width
        draftY = clamp(maskY, 0, 0.98) * height
        draftWidth = Math.min(width - draftX,
                              Math.max(16, clamp(maskWidth, 0.02, 1) * width))
        draftHeight = Math.min(height - draftY,
                               Math.max(16, clamp(maskHeight, 0.02, 1) * height))
    }

    onMaskXChanged: syncFromMask()
    onMaskYChanged: syncFromMask()
    onMaskWidthChanged: syncFromMask()
    onMaskHeightChanged: syncFromMask()
    onWidthChanged: syncFromMask()
    onHeightChanged: syncFromMask()
    onActiveChanged: if (active) syncFromMask()
    Component.onCompleted: syncFromMask()

    Rectangle {
        x: root.draftX
        y: root.draftY
        width: root.draftWidth
        height: root.draftHeight
        color: "transparent"
        border.width: 2
        border.color: Theme.accent

        Repeater {
            model: [
                { x: -4, y: -4 },
                { x: parent.width - 4, y: -4 },
                { x: -4, y: parent.height - 4 },
                { x: parent.width - 4, y: parent.height - 4 }
            ]
            delegate: Rectangle {
                required property var modelData
                x: modelData.x
                y: modelData.y
                width: 8
                height: 8
                color: Theme.bgPrimary
                border.width: 1
                border.color: Theme.accent
            }
        }
    }

    MouseArea {
        id: pointer
        objectName: "customBlurPointer"
        anchors.fill: parent
        enabled: root.active
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        preventStealing: true
        property string editMode: "draw"
        property real pressX: 0
        property real pressY: 0
        property real startX: 0
        property real startY: 0
        property real startWidth: 0
        property real startHeight: 0

        function hitMode(px, py) {
            var tolerance = 10
            var left = root.draftX
            var right = root.draftX + root.draftWidth
            var top = root.draftY
            var bottom = root.draftY + root.draftHeight
            var nearLeft = Math.abs(px - left) <= tolerance
            var nearRight = Math.abs(px - right) <= tolerance
            var nearTop = Math.abs(py - top) <= tolerance
            var nearBottom = Math.abs(py - bottom) <= tolerance
            if (nearLeft && nearTop) return "topLeft"
            if (nearRight && nearTop) return "topRight"
            if (nearLeft && nearBottom) return "bottomLeft"
            if (nearRight && nearBottom) return "bottomRight"
            if (nearLeft && py >= top && py <= bottom) return "left"
            if (nearRight && py >= top && py <= bottom) return "right"
            if (nearTop && px >= left && px <= right) return "top"
            if (nearBottom && px >= left && px <= right) return "bottom"
            if (px >= left && px <= right && py >= top && py <= bottom)
                return "move"
            return "draw"
        }

        function cursorFor(mode) {
            if (mode === "move") return "CrossArrow"
            if (mode === "left" || mode === "right") return "ScaleHorizontal"
            if (mode === "top" || mode === "bottom") return "ScaleVertical"
            if (mode === "topLeft" || mode === "bottomRight")
                return "ScaleTLBR"
            if (mode === "topRight" || mode === "bottomLeft")
                return "ScaleTRBL"
            return "Precise"
        }

        AppCursor.name: cursorFor(pressed ? editMode : hitMode(mouseX, mouseY))

        onPressed: mouse => {
            root.syncFromMask()
            editMode = hitMode(mouse.x, mouse.y)
            pressX = mouse.x
            pressY = mouse.y
            startX = root.draftX
            startY = root.draftY
            startWidth = root.draftWidth
            startHeight = root.draftHeight
            root.dragging = true
            if (editMode === "draw") {
                root.draftX = root.clamp(mouse.x, 0, root.width)
                root.draftY = root.clamp(mouse.y, 0, root.height)
                root.draftWidth = 0
                root.draftHeight = 0
            }
        }

        onPositionChanged: mouse => {
            if (!pressed)
                return
            var px = root.clamp(mouse.x, 0, root.width)
            var py = root.clamp(mouse.y, 0, root.height)
            var dx = px - pressX
            var dy = py - pressY
            if (editMode === "draw") {
                root.draftX = Math.min(pressX, px)
                root.draftY = Math.min(pressY, py)
                root.draftWidth = Math.abs(px - pressX)
                root.draftHeight = Math.abs(py - pressY)
            } else if (editMode === "move") {
                root.draftX = root.clamp(startX + dx, 0,
                                         root.width - startWidth)
                root.draftY = root.clamp(startY + dy, 0,
                                         root.height - startHeight)
            } else {
                var left = startX
                var right = startX + startWidth
                var top = startY
                var bottom = startY + startHeight
                if (editMode.indexOf("Left") >= 0 || editMode === "left")
                    left = root.clamp(px, 0, right - 16)
                if (editMode.indexOf("Right") >= 0 || editMode === "right")
                    right = root.clamp(px, left + 16, root.width)
                if (editMode.indexOf("top") === 0 || editMode === "top")
                    top = root.clamp(py, 0, bottom - 16)
                if (editMode.indexOf("bottom") === 0 || editMode === "bottom")
                    bottom = root.clamp(py, top + 16, root.height)
                root.draftX = left
                root.draftY = top
                root.draftWidth = right - left
                root.draftHeight = bottom - top
            }
        }

        onReleased: {
            if (root.draftWidth < 16 || root.draftHeight < 16) {
                root.draftWidth = Math.min(96, root.width - root.draftX)
                root.draftHeight = Math.min(64, root.height - root.draftY)
            }
            root.dragging = false
            root.maskCommitted(root.draftX / root.width,
                               root.draftY / root.height,
                               root.draftWidth / root.width,
                               root.draftHeight / root.height)
        }
    }
}
