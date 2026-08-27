//qmllint disable
import QtQuick
import QtQuick.Window
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../subtitles"
import "../timeline"

// Reusable native Qt Quick drag source for project media items. The proxy
// keeps the delegate in its ListView/GridView while MouseArea.drag activates
// the attached Drag object.
Item {
    id: root

    property string dragMediaId: ""
    property string dragMediaKind: ""
    property string dragMediaName: ""
    property var dragMediaIds: []
    property real dragDurationMs: 5000
    property url previewUrl: ""
    property bool internalDragging: false
    property bool movedDuringPress: false
    property real pressX: 0
    property real pressY: 0

    signal clicked(int modifiers)
    signal doubleClicked(int modifiers)
    signal contextRequested()

    Item {
        id: dragProxy
        width: 1
        height: 1
        x: 0
        y: 0

        property string dragMediaId: root.dragMediaId
        property string dragMediaKind: root.dragMediaKind
        property string dragMediaName: root.dragMediaName
        property var dragMediaIds: root.dragMediaIds
        property real dragDurationMs: root.dragDurationMs
        property url previewUrl: root.previewUrl

        Drag.active: root.internalDragging
        Drag.source: dragProxy
        Drag.keys: ["cutpro-media"]
        Drag.supportedActions: Qt.CopyAction
        Drag.imageSource: root.previewUrl
        Drag.hotSpot.x: root.pressX
        Drag.hotSpot.y: root.pressY
    }

    Item {
        id: dragVisual
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 100000
        width: 172
        height: 58
        visible: root.internalDragging
        enabled: false

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSm
            color: "#292929"
            border.width: 2
            border.color: Theme.accent
            opacity: 0.96
        }
        Image {
            id: dragThumbnail
            anchors.left: parent.left
            anchors.leftMargin: 5
            anchors.top: parent.top
            anchors.topMargin: 5
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 5
            width: 76
            source: root.previewUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        Image {
            anchors.centerIn: dragThumbnail
            width: 22
            height: 22
            visible: root.previewUrl.toString() === ""
            source: "../../assets/icons/"
                    + (root.dragMediaKind === "audio" ? "music" : "film")
                    + ".svg"
            opacity: 0.75
        }
        Text {
            anchors.left: dragThumbnail.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            text: root.dragMediaName || "Media"
            color: Theme.textPrimary
            font.pixelSize: Theme.fsSm
            elide: Text.ElideMiddle
        }

        // Count badge (like CapCut) — shows number of selected items
        Rectangle {
            id: dragCountBadge
            x: dragVisual.width - width / 2
            y: -height / 2
            width: Math.max(22, dragCountText.implicitWidth + 10)
            height: 22
            radius: 11
            color: Theme.accent
            visible: root.dragMediaIds.length > 1
            z: 100

            Text {
                id: dragCountText
                anchors.centerIn: parent
                text: root.dragMediaIds.length
                color: "#ffffff"
                font.pixelSize: 12
                font.bold: true
            }
        }
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        function moveVisual(mouseX, mouseY) {
            if (!dragVisual.parent)
                return
            var point = root.mapToItem(dragVisual.parent,
                                       mouseX + 16, mouseY + 16)
            dragVisual.x = point.x
            dragVisual.y = point.y
        }
        onPressed: mouse => {
            if (mouse.button === Qt.RightButton) {
                root.contextRequested()
                mouse.accepted = true
                return
            }
            root.pressX = mouse.x
            root.pressY = mouse.y
            root.internalDragging = false
            root.movedDuringPress = false
            dragProxy.x = 0
            dragProxy.y = 0
            moveVisual(mouse.x, mouse.y)
        }
        onPositionChanged: mouse => {
            if (!pressed)
                return
            var deltaX = mouse.x - root.pressX
            var deltaY = mouse.y - root.pressY
            dragProxy.x = deltaX
            dragProxy.y = deltaY
            moveVisual(mouse.x, mouse.y)
            if (!root.internalDragging
                    && Math.abs(deltaX) + Math.abs(deltaY) >= 4) {
                root.internalDragging = true
                root.movedDuringPress = true
            }
        }
        onClicked: mouse => {
            if (!root.movedDuringPress)
                root.clicked(mouse.modifiers)
        }
        onDoubleClicked: mouse => {
            if (!root.movedDuringPress)
                root.doubleClicked(mouse.modifiers)
        }
        onReleased: {
            if (root.internalDragging)
                dragProxy.Drag.drop()
            root.internalDragging = false
            dragProxy.x = 0
            dragProxy.y = 0
        }
        onCanceled: {
            if (root.internalDragging)
                dragProxy.Drag.cancel()
            root.internalDragging = false
            dragProxy.x = 0
            dragProxy.y = 0
        }
    }
}
