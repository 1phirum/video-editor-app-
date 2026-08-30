//qmllint disable
import QtQuick
import QtQuick.Window
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../subtitles"
import "../timeline"

// Reusable native Qt Quick drag source for project media items. The native
// Drag.source must move through the scene with the pointer; a separate visual
// moving above a stationary source does not enter DropAreas reliably.
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
    property point dragStartWindowPos: Qt.point(0, 0)

    signal clicked(int modifiers)
    signal doubleClicked(int modifiers)
    signal contextRequested()

    // Filmstrip and waveform decoding stands down for the length of the drag, so
    // the cores go to the frame the user is dragging instead of to thumbnails
    // that will be repainted the moment it lands.
    onInternalDraggingChanged: {
        if (root.internalDragging)
            Backend.beginTimelineInteraction()
        else
            Backend.endTimelineInteraction()
    }

    Item {
        id: dragTargetDummy
        width: 1
        height: 1
        x: 0
        y: 0
    }

    Item {
        id: dragProxy
        parent: root.internalDragging && root.Window.window
                ? root.Window.window.contentItem : root
        width: root.width
        height: root.height
        x: root.internalDragging
           ? root.dragStartWindowPos.x + dragTargetDummy.x : 0
        y: root.internalDragging
           ? root.dragStartWindowPos.y + dragTargetDummy.y : 0
        visible: root.internalDragging

        property string dragMediaId: root.dragMediaId
        property string dragMediaKind: root.dragMediaKind
        property string dragMediaName: root.dragMediaName
        property var dragMediaIds: root.dragMediaIds
        property real dragDurationMs: root.dragDurationMs
        property url previewUrl: root.previewUrl
        property var dragOwner: root

        Drag.active: root.internalDragging
        Drag.source: dragProxy
        Drag.keys: ["cutpro-media"]
        Drag.supportedActions: Qt.CopyAction
        Drag.imageSource: root.previewUrl
        Drag.hotSpot.x: root.pressX
        Drag.hotSpot.y: root.pressY
        // On Windows the native drag loop can consume the mouse release, so
        // MouseArea.onReleased is not guaranteed to run. The attached Drag
        // state is authoritative and must always hide the visual proxy.
        Drag.onActiveChanged: {
            if (!Drag.active) {
                root.internalDragging = false
                root.movedDuringPress = false
            }
        }
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
        drag.target: dragTargetDummy
        drag.threshold: Qt.styleHints.startDragDistance
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
            dragTargetDummy.x = 0
            dragTargetDummy.y = 0
            moveVisual(mouse.x, mouse.y)
        }
        onPositionChanged: mouse => {
            if (!pressed || !(mouse.buttons & Qt.LeftButton))
                return
            moveVisual(mouse.x, mouse.y)
            if (drag.active && !root.internalDragging) {
                root.movedDuringPress = true
                if (root.Window.window)
                    root.dragStartWindowPos = root.mapToItem(
                                root.Window.window.contentItem, 0, 0)
                root.internalDragging = true
            }
        }
        onReleased: mouse => {
            var wasDragging = root.internalDragging
            if (wasDragging)
                dragProxy.Drag.drop()
            root.internalDragging = false
            root.movedDuringPress = wasDragging || root.movedDuringPress
            dragTargetDummy.x = 0
            dragTargetDummy.y = 0
        }
        onCanceled: {
            root.internalDragging = false
            root.movedDuringPress = false
            dragTargetDummy.x = 0
            dragTargetDummy.y = 0
        }
        onDoubleClicked: mouse => {
            if (mouse.button !== Qt.LeftButton)
                return
            if (!root.movedDuringPress)
                root.doubleClicked(mouse.modifiers)
            root.movedDuringPress = false
        }
        onClicked: mouse => {
            // The right button did all of its work in onPressed (it opened the
            // context menu). MouseArea still emits clicked() for it on release,
            // and letting that through would re-run the plain-click selection
            // and collapse a rubber-band / multi selection down to the single
            // item under the cursor, right after the menu opened on it.
            if (mouse.button !== Qt.LeftButton)
                return
            if (!root.movedDuringPress)
                root.clicked(mouse.modifiers)
            root.movedDuringPress = false
        }
    }
}
