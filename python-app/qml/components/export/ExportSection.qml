pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

Rectangle {
    id: root

    property string title: ""
    property string summary: ""
    property bool expanded: true
    default property alias contentData: sectionBody.data

    implicitHeight: sectionLayout.implicitHeight
    color: Theme.bgPanel
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        id: sectionLayout
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 38
            color: headerHover.hovered ? Theme.hover : Theme.bgSidebar

            HoverHandler {
                id: headerHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler { onTapped: root.expanded = !root.expanded }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 8

                Image {
                    source: "../../assets/icons/"
                            + (root.expanded ? "chevron-down" : "chevron-right")
                            + ".svg"
                    sourceSize.width: 13
                    sourceSize.height: 13
                    Layout.preferredWidth: 13
                    Layout.preferredHeight: 13
                    opacity: 0.72
                }

                Text {
                    text: root.title
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsMd
                    font.weight: Font.DemiBold
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: root.summary !== ""
                    text: root.summary
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsXs
                    elide: Text.ElideRight
                    Layout.maximumWidth: 220
                }
            }
        }

        ColumnLayout {
            id: sectionBody
            visible: root.expanded
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            spacing: 9
        }
    }
}
