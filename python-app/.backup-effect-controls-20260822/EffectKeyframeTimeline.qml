//qmllint disable
import QtQuick
import QtQuick.Layouts
import CutPro 1.0
import "../theme"

Rectangle {
    id: root
    property string clipId: ""
    property var properties: ["positionX", "positionY", "scale", "rotation", "opacity", "speed", "volumeDb"]
    color: Theme.bgTimeline
    border.color: Theme.border

    function labelFor(key) {
        var names = {positionX: "Position X", positionY: "Position Y", scale: "Scale", rotation: "Rotation", opacity: "Opacity", speed: "Speed", volumeDb: "Level"}
        return names[key] || key
    }
    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 0
        Text { text: "00:00     00:05     00:10     00:15"; color: Theme.textMuted; font.family: Theme.monoFont; font.pixelSize: Theme.fsXs; height: 24 }
        Repeater {
            model: root.properties
            delegate: Item {
                required property string modelData
                width: parent.width; height: 30
                Text { anchors.left: parent.left; width: 76; text: root.labelFor(modelData); color: Theme.textMuted; font.pixelSize: Theme.fsXs; elide: Text.ElideRight }
                Rectangle { anchors.left: parent.left; anchors.leftMargin: 82; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; height: 1; color: Theme.border }
                Repeater {
                    model: Backend.keyframeEngine.keyframesFor(root.clipId, modelData)
                    delegate: Rectangle {
                        required property var modelData
                        width: 8; height: 8; rotation: 45; color: Theme.accent
                        x: 82 + Math.max(0, Math.min(1, Number(modelData.timeMs) / Math.max(1, Backend.durationMs))) * Math.max(0, parent.width - 90)
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea { anchors.fill: parent; onClicked: Backend.playheadMs = modelData.timeMs }
                    }
                }
            }
        }
        Rectangle {
            x: 82 + Math.max(0, Math.min(1, Backend.playheadMs / Math.max(1, Backend.durationMs))) * Math.max(0, parent.width - 90)
            y: 24; width: 1; height: parent.height - 24; color: Theme.accent
        }
    }
}
