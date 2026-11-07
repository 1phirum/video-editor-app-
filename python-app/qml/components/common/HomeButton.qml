// qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"

// The pill button used in the home screen's dialogs. `primary` fills it with
// the accent (or danger red); a secondary button is a quiet outline.
Button {
    id: control

    property bool primary: false
    property bool danger: false

    implicitHeight: 34
    implicitWidth: Math.max(84, label.implicitWidth + 28)
    flat: true
    hoverEnabled: false

    HoverHandler {
        id: hover
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    background: Rectangle {
        radius: 17
        readonly property color base: control.danger ? Theme.danger : Theme.accent
        color: !control.primary
               ? (hover.hovered ? Theme.hover : "transparent")
               : control.down ? Qt.darker(base, 1.25)
               : hover.hovered ? Qt.lighter(base, 1.1)
               : base
        border.color: control.primary ? "transparent" : Theme.outline
        border.width: control.primary ? 0 : 1
        opacity: control.enabled ? 1.0 : 0.5
    }

    contentItem: Text {
        id: label
        text: control.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.primary ? "#ffffff" : Theme.textPrimary
        font.pixelSize: Theme.fsLg
        font.weight: Font.DemiBold
        opacity: control.enabled ? 1.0 : 0.6
    }
}
