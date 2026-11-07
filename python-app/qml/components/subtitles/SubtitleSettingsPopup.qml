//qmllint disable
// Subtitle settings as a native OS modal window (matches the Settings dialog),
// instead of an in-canvas Popup. openNative() shows it centered on the owner
// window; the native title bar carries "Subtitle settings" and the caption
// text preview sits in a slim content header.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../common"

NativeModalWindow {
    id: root

    property string subtitleText: ""

    dialogTitle: qsTr("Subtitle settings")
    dialogWidth: 460
    dialogHeight: 640

    // Kept so existing call sites (subtitleSettingsPopup.open()) still work.
    function open() {
        openNative()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgPanel

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                color: Theme.bgSidebar

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.subtitleText.length > 0 ? root.subtitleText
                                                       : qsTr("Selected subtitle")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    elide: Text.ElideRight
                    maximumLineCount: 1
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
                    id: doneButton
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Done")
                    implicitWidth: 76
                    implicitHeight: 30
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    contentItem: Text {
                        text: doneButton.text
                        color: "white"
                        font.pixelSize: Theme.fsSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: doneButton.down ? Qt.darker(Theme.accent, 1.2) : Theme.accent
                        radius: Theme.radiusSm
                    }
                    onClicked: root.close()
                }
            }
        }
    }
}
