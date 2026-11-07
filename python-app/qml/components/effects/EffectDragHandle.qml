// qmllint disable
import QtQuick
import CutPro 1.0
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

Item {
    id: root

    property string dragEffectId: ""
    property string dragEffectName: ""
    property string dragEffectIcon: "sliders-horizontal"
    property string dragMediaType: "video"
    property bool internalDragging: false
    property bool movedDuringPress: false
    property real pressX: 0
    property real pressY: 0

    // The timeline opens its effect lane while this is set. An audio effect has
    // nothing to do there, so dragging one leaves the lane closed rather than
    // offering a row that would refuse the drop.
    function announceDrag(active) {
        Backend.effectDragActive = active && root.dragMediaType !== "audio"
                                  && root.dragEffectId !== ""
    }

    Component.onDestruction: Backend.effectDragActive = false

    signal clicked()
    signal doubleClicked()

    Item {
        id: dragProxy
        width: 1
        height: 1

        property string dragEffectId: root.dragEffectId
        property string dragEffectName: root.dragEffectName
        property string dragMediaType: root.dragMediaType

        Drag.active: root.internalDragging
        Drag.source: dragProxy
        Drag.keys: ["cutpro-effect"]
        Drag.supportedActions: Qt.CopyAction
        Drag.imageSource: "../../assets/icons/" + root.dragEffectIcon + ".svg"
        Drag.hotSpot.x: root.pressX
        Drag.hotSpot.y: root.pressY
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: true
        onPressed: mouse => {
            root.pressX = mouse.x
            root.pressY = mouse.y
            root.internalDragging = false
            root.movedDuringPress = false
            dragProxy.x = 0
            dragProxy.y = 0
        }
        onPositionChanged: mouse => {
            if (!pressed)
                return
            var deltaX = mouse.x - root.pressX
            var deltaY = mouse.y - root.pressY
            dragProxy.x = deltaX
            dragProxy.y = deltaY
            if (!root.internalDragging
                    && Math.abs(deltaX) + Math.abs(deltaY) >= 4) {
                // Before Drag.active, so the lane is already there to be entered.
                root.announceDrag(true)
                root.internalDragging = true
                root.movedDuringPress = true
            }
        }
        onClicked: {
            if (!root.movedDuringPress)
                root.clicked()
        }
        onDoubleClicked: {
            if (!root.movedDuringPress)
                root.doubleClicked()
        }
        onReleased: {
            if (root.internalDragging)
                dragProxy.Drag.drop()
            root.internalDragging = false
            dragProxy.x = 0
            dragProxy.y = 0
            // Deferred, because the drop defers the bar it creates: clearing now
            // would close the lane for one frame before the bar arrives to keep
            // it open.
            Qt.callLater(root.announceDrag, false)
        }
        onCanceled: {
            if (root.internalDragging)
                dragProxy.Drag.cancel()
            root.internalDragging = false
            dragProxy.x = 0
            dragProxy.y = 0
            root.announceDrag(false)
        }
    }
}
