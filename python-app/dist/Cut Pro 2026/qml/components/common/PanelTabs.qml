pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "../../theme"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

// Shared panel tab strip (mirrors agent-app PanelTabs.tsx).
// The 2px underline is anchored to the active tab's own width, so it always
// lines up with the label — no hardcoded x offsets.
Rectangle {
    id: root

    property var tabs: []
    property int currentIndex: 0
    property color underlineColor: Theme.accent
    property bool showOverflow: true
    property string overflowIcon: "more-horizontal"
    signal tabClicked(int index)
    signal overflowClicked()

    implicitHeight: Theme.panelTabHeight
    color: Theme.bgSidebar

    // Bottom seam
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 0

        Repeater {
            model: root.tabs
            delegate: Item {
                id: tabItem
                required property int index
                required property string modelData
                readonly property bool isActive: root.currentIndex === index

                Layout.fillHeight: true
                implicitWidth: label.implicitWidth + 20  // px-2.5 both sides

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: tabItem.modelData
                    font.pixelSize: Theme.fsMd
                    color: tabItem.isActive ? Theme.textPrimary : Theme.textMuted
                }

                // Active underline spans the full tab width (inset-x-0).
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    color: root.underlineColor
                    visible: tabItem.isActive
                }

                TapHandler {
                    cursorShape: Qt.PointingHandCursor
                    onTapped: {
                        root.currentIndex = tabItem.index
                        root.tabClicked(tabItem.index)
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        IconButton {
            visible: root.showOverflow
            iconName: root.overflowIcon
            boxSize: 24
            glyphSize: 14
            Layout.alignment: Qt.AlignVCenter
            onClicked: root.overflowClicked()
        }
    }
}
