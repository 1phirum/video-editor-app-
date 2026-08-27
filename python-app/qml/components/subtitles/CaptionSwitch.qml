//qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

Switch {
    id: control

    implicitHeight: 30
    spacing: 8
    hoverEnabled: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    indicator: Rectangle {
        x: control.leftPadding
        y: Math.round((control.height - height) / 2)
        implicitWidth: 30
        implicitHeight: 16
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.bgPrimary
        border.color: control.checked ? Theme.accent : Theme.textMuted

        Rectangle {
            width: 12
            height: 12
            radius: 6
            x: control.checked ? parent.width - width - 2 : 2
            anchors.verticalCenter: parent.verticalCenter
            color: control.checked ? "white" : Theme.textSecondary

            Behavior on x { NumberAnimation { duration: 90 } }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        color: control.enabled ? Theme.textSecondary : Theme.textMuted
        font.pixelSize: Theme.fsSm
        verticalAlignment: Text.AlignVCenter
    }
}
