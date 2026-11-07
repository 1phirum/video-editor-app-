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

    property string glyph: ""
    property string toolTipText: ""
    property bool glyphBold: false
    property bool glyphItalic: false

    implicitWidth: 30
    implicitHeight: 30
    padding: 0
    checkable: true
    hoverEnabled: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    ToolTip.visible: control.hovered
    ToolTip.text: control.toolTipText

    contentItem: Text {
        text: control.glyph
        color: control.checked ? "white" : Theme.textSecondary
        font.pixelSize: Theme.fsLg
        font.bold: control.glyphBold
        font.italic: control.glyphItalic
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        color: control.checked ? Theme.accent
             : control.down ? Qt.darker(Theme.hover, 1.2)
             : control.hovered ? Theme.hover : Theme.bgPrimary
        border.color: control.checked ? Theme.accent : Theme.border
        radius: Theme.radiusSm
    }
}
