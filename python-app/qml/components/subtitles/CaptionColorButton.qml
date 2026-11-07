import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

Button {
    id: control

    property color swatchColor: "white"
    property string toolTipText: ""

    implicitWidth: 42
    implicitHeight: 30
    padding: 0
    hoverEnabled: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    ToolTip.visible: control.hovered
    ToolTip.text: control.toolTipText

    background: Rectangle {
        color: control.swatchColor
        opacity: control.enabled ? 1 : 0.35
        border.width: control.hovered && control.enabled ? 2 : 1
        border.color: control.hovered && control.enabled
                      ? Theme.accent : Theme.border
        radius: Theme.radiusSm
    }
}
