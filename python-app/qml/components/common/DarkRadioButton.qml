// qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"

// Radio counterpart to DarkCheckBox: a circular indicator pinned at the left
// edge with the label offset exactly one gap past it, so the dot never sits
// under the text. Works inside a ButtonGroup like any RadioButton.
RadioButton {
    id: control
    padding: 0
    spacing: 8
    hoverEnabled: true
    implicitHeight: Math.max(18, contentItem.implicitHeight)

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    indicator: Rectangle {
        x: 0
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 16
        height: 16
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: control.checked
                      ? Theme.accent
                      : (control.hovered ? Theme.textSecondary : Theme.textMuted)

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: width / 2
            color: Theme.accent
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        leftPadding: control.indicator.width + control.spacing
        color: control.enabled ? Theme.textSecondary : Theme.textMuted
        font.pixelSize: Theme.fsSm
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
