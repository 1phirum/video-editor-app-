// qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"

// Dialog footer button with rounded corners. Honours the standard `highlighted`
// (primary, accent-filled) and default (secondary, outlined) roles so a dialog
// can pair one Apply against one Cancel without restyling each.
Button {
    id: control
    padding: 0
    implicitHeight: 30
    implicitWidth: Math.max(88, contentItem.implicitWidth + 34)
    hoverEnabled: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Text {
        text: control.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Theme.fsMd
        color: control.highlighted
               ? "#ffffff"
               : (control.enabled ? Theme.textSecondary : Theme.textMuted)
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.highlighted
               ? (control.down ? Qt.darker(Theme.accent, 1.2)
                                : (control.hovered ? Qt.lighter(Theme.accent, 1.1)
                                                   : Theme.accent))
               : (control.down || control.hovered ? Theme.hover : "transparent")
        border.width: control.highlighted ? 0 : 1
        border.color: control.highlighted ? "transparent" : Theme.border
        opacity: control.enabled ? 1.0 : 0.5
    }
}
