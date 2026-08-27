pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

Rectangle {
    id: root

    property string title: ""
    property string summary: ""
    property bool expanded: true
    property bool checkable: false
    property bool sectionEnabled: true
    signal sectionToggled(bool enabled)
    default property alias contentData: body.data

    Layout.fillWidth: true
    implicitHeight: header.height + 1
                    + (root.expanded ? body.implicitHeight + 20 : 0)
    color: Theme.bgPanel
    border.width: 0

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        color: headerHover.hovered ? Theme.hover : Theme.bgPanel

        HoverHandler {
            id: headerHover
            cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.rightMargin: root.checkable ? 34 : 0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            TapHandler {
                enabled: root.enabled
                onTapped: root.expanded = !root.expanded
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 7

            Image {
                source: "../../assets/icons/" + (root.expanded ? "chevron-down" : "chevron-right") + ".svg"
                sourceSize.width: 13
                sourceSize.height: 13
                fillMode: Image.PreserveAspectFit
                Layout.preferredWidth: 13
                Layout.preferredHeight: 13
                opacity: root.enabled ? 0.78 : 0.32
            }
            Text {
                text: root.title
                color: root.enabled ? Theme.textPrimary : Theme.textMuted
                font.pixelSize: Theme.fsMd
                font.weight: root.expanded ? Font.DemiBold : Font.Normal
                Layout.alignment: Qt.AlignVCenter
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: root.summary !== ""
                text: root.summary
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
                elide: Text.ElideRight
                Layout.maximumWidth: Math.min(170, root.width * 0.4)
                Layout.alignment: Qt.AlignVCenter
            }
            LumetriCheckBox {
                visible: root.checkable
                checked: root.sectionEnabled
                onToggled: root.sectionToggled(checked)
                ToolTip.visible: hovered
                ToolTip.text: checked ? "Disable section" : "Enable section"
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }
    }

    ColumnLayout {
        id: body
        x: 14
        y: header.height + 10
        width: Math.max(0, parent.width - 28)
        visible: root.expanded
        enabled: !root.checkable || root.sectionEnabled
        opacity: enabled ? 1 : 0.38
        spacing: 8
    }
}
