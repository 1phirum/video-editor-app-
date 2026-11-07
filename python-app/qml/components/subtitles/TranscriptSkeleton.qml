//qmllint disable 
import QtQuick
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

Item {
    id: root

    opacity: 0.55

    SequentialAnimation on opacity {
        loops: Animation.Infinite
        NumberAnimation { to: 0.28; duration: 650; easing.type: Easing.InOutQuad }
        NumberAnimation { to: 0.62; duration: 650; easing.type: Easing.InOutQuad }
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 7

        Repeater {
            model: [0.76, 0.58, 0.88, 0.66, 0.82, 0.48]
            delegate: Item {
                required property real modelData
                width: parent.width
                height: 48

                Rectangle {
                    x: 0
                    y: 4
                    width: 126
                    height: 8
                    radius: 2
                    color: Theme.border
                }
                Rectangle {
                    x: 0
                    y: 22
                    width: parent.width * modelData
                    height: 11
                    radius: 2
                    color: Theme.hover
                }
                Rectangle {
                    x: 0
                    y: 39
                    width: parent.width * Math.max(0.3, modelData - 0.22)
                    height: 6
                    radius: 2
                    color: Theme.border
                    opacity: 0.7
                }
            }
        }
    }
}
