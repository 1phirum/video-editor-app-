//qmllint disable

import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

CheckBox {
    id: control

    implicitWidth: 22
    implicitHeight: 22
    padding: 0
    spacing: 0
    hoverEnabled: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    indicator: Rectangle {
        anchors.centerIn: parent
        width: 16
        height: 16
        radius: 1
        color: control.checked ? "#b8b8b8" : "transparent"
        border.width: 1
        border.color: control.checked ? "#b8b8b8" : Theme.textMuted

        Image {
            anchors.centerIn: parent
            source: "../../assets/icons/check.svg"
            sourceSize.width: 12
            sourceSize.height: 12
            visible: control.checked
        }
    }

    contentItem: Item {}
}
