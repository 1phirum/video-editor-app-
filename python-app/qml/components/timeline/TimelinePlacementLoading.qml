import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"

Rectangle {
    id: root
    anchors.fill: parent
    z: 1000
    color: Qt.rgba(0.02, 0.03, 0.05, 0.72)
    visible: Backend.timelinePlacementInProgress

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 48)
        height: 190
        radius: Theme.radiusSm
        color: Theme.bgPanel
        border.color: Theme.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                BusyIndicator {
                    running: root.visible
                    implicitWidth: 28
                    implicitHeight: 28
                }
                Text {
                    Layout.fillWidth: true
                    text: "Loading timeline"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsLg
                    font.weight: Font.DemiBold
                }
                Text {
                    text: Math.round(Backend.timelinePlacementProgress * 100) + "%"
                    color: Theme.accent
                    font.pixelSize: Theme.fsLg
                    font.weight: Font.DemiBold
                }
            }

            Text {
                Layout.fillWidth: true
                text: Backend.timelinePlacementStatus
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
                elide: Text.ElideRight
            }

            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 1
                value: Backend.timelinePlacementProgress
            }

            LumetriActionButton {
                Layout.alignment: Qt.AlignRight
                text: "Cancel"
                onClicked: Backend.cancelTimelinePlacement()
            }
        }
    }
}
