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

    // The overflow (⋯) button stays pinned to the right; the tab strip scrolls
    // underneath it, so it is always reachable no matter how narrow the panel.
    IconButton {
        id: overflowBtn
        visible: root.showOverflow
        iconName: root.overflowIcon
        boxSize: 24
        glyphSize: 14
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.overflowClicked()
    }

    // Horizontally scrollable tab strip. When every label fits, the Row is
    // narrower than the Flickable and sits still (left-aligned, as before);
    // once the panel is too narrow the strip flicks/drags (and mouse-wheel
    // scrolls) so tabs are never clipped or overlapped by the ⋯ button.
    Flickable {
        id: tabsFlick
        anchors.left: parent.left
        anchors.leftMargin: 6
        anchors.right: overflowBtn.visible ? overflowBtn.left : parent.right
        anchors.rightMargin: overflowBtn.visible ? 2 : 6
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        contentWidth: tabsRow.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        readonly property real maxContentX: Math.max(0, contentWidth - width)

        Row {
            id: tabsRow
            height: tabsFlick.height
            spacing: 0

            Repeater {
                model: root.tabs
                delegate: Item {
                    id: tabItem
                    required property int index
                    required property string modelData
                    readonly property bool isActive: root.currentIndex === index

                    height: tabsRow.height
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
        }
        // A vertical wheel/trackpad scroll pans the strip sideways, so a mouse
        // over the tabs moves them without needing a click-drag.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                const delta = event.angleDelta.y !== 0
                    ? event.angleDelta.y : event.angleDelta.x
                tabsFlick.contentX = Math.max(
                    0, Math.min(tabsFlick.contentX - delta,
                                tabsFlick.maxContentX))
            }
        }
    }
    // Edge fades that appear only while there is more strip hidden in that
    // direction — the standard hint that a row can be scrolled. Purely visual:
    // no pointer handlers, so taps still reach the tabs beneath them. Kept 1px
    // clear of the bottom so the seam stays continuous.
    Rectangle {
        anchors.left: tabsFlick.left
        anchors.top: tabsFlick.top
        anchors.bottom: tabsFlick.bottom
        anchors.bottomMargin: 1
        width: 18
        visible: tabsFlick.contentX > 0.5
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.bgSidebar }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
    Rectangle {
        anchors.right: tabsFlick.right
        anchors.top: tabsFlick.top
        anchors.bottom: tabsFlick.bottom
        anchors.bottomMargin: 1
        width: 18
        visible: tabsFlick.contentX < tabsFlick.maxContentX - 0.5
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.bgSidebar }
        }
    }
}
