// qmllint disable
import QtQuick
import QtQuick.Layouts
import "../../theme"

// A single row in the home screen's left rail: icon + label, with an optional
// count badge and a selected/hover state. Emits activated() on click.
Rectangle {
    id: control

    property string iconName: ""
    property string label: ""
    property bool selected: false
    property int badge: -1

    signal activated()

    implicitHeight: 38
    radius: Theme.radiusMd
    color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
           : hover.hovered ? Theme.hover
           : "transparent"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 10

        Image {
            source: control.iconName === "" ? "" : "../../assets/icons/" + control.iconName + ".svg"
            sourceSize.width: 16
            sourceSize.height: 16
            opacity: control.selected ? 1.0 : 0.85
        }

        Text {
            Layout.fillWidth: true
            text: control.label
            color: control.selected ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: Theme.fsLg
            font.weight: control.selected ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        Rectangle {
            visible: control.badge >= 0
            Layout.preferredWidth: Math.max(20, badgeText.implicitWidth + 10)
            Layout.preferredHeight: 18
            radius: 9
            color: Theme.bgPrimary
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: control.badge
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: control.activated()
    }
}
