//qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

Dialog {
    id: root

    property string subtitleText: ""

    width: Math.min(440, parent ? parent.width - 32 : 440)
    height: Math.min(520, parent ? parent.height - 32 : 520)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: "#99000000" }

    background: Rectangle {
        color: Theme.bgPanel
        border.color: Theme.border
        radius: Theme.radiusSm
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            color: Theme.bgSidebar

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    text: "Subtitle settings"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsLg
                    font.weight: Font.DemiBold
                }
                Text {
                    text: root.subtitleText.length > 0 ? root.subtitleText : "Selected subtitle"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsXs
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    width: parent.width
                }
            }
        }

        CaptionStylePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Theme.bgSidebar

            Button {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: "Done"
                implicitWidth: 76
                implicitHeight: 30
                HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pixelSize: Theme.fsSm
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.down ? Qt.darker(Theme.accent, 1.2) : Theme.accent
                    radius: Theme.radiusSm
                }
                onClicked: root.close()
            }
        }
    }
}
