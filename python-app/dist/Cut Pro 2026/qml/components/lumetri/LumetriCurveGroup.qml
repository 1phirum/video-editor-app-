pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

ColumnLayout {
    id: root

    property string title: ""
    property bool expanded: false
    property bool groupEnabled: true
    property bool hueBackground: true
    property bool flatIdentity: true
    property color curveColor: Theme.textPrimary
    property var points: []
    property Component editorHeader: null
    signal enabledToggled(bool enabled)
    signal pointsCommitted(var points)

    Layout.fillWidth: true
    spacing: 0

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 32
        color: groupHover.hovered ? Theme.hover : Theme.bgPanel

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            spacing: 5
            Image {
                source: "../../assets/icons/"
                        + (root.expanded ? "chevron-down" : "chevron-right")
                        + ".svg"
                sourceSize.width: 11
                sourceSize.height: 11
                Layout.preferredWidth: 13
                opacity: 0.75
            }
            Text {
                Layout.fillWidth: true
                text: root.title
                color: root.groupEnabled ? Theme.textSecondary : Theme.textMuted
                font.pixelSize: Theme.fsXs
                font.weight: Font.DemiBold
            }
            LumetriCheckBox {
                checked: root.groupEnabled
                onToggled: root.enabledToggled(checked)
            }
        }

        HoverHandler { id: groupHover }
        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.rightMargin: 34
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: root.expanded = !root.expanded
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 8
        Layout.bottomMargin: 8
        visible: root.expanded
        enabled: root.groupEnabled
        opacity: root.groupEnabled ? 1 : 0.38
        spacing: 7

        Loader {
            Layout.fillWidth: true
            active: root.editorHeader !== null
            sourceComponent: root.editorHeader
        }

        LumetriCurveEditor {
            Layout.fillWidth: true
            flatIdentity: root.flatIdentity
            hueBackground: root.hueBackground
            curveColor: root.curveColor
            points: root.points
            onPointsCommitted: points => root.pointsCommitted(points)
        }
    }
}
