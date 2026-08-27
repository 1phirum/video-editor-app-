import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"

SettingsPage {
    id: root

    SettingsSection {
        title: "Preview Cache"
        SettingsRow {
            label: "Location"
            Text {
                Layout.fillWidth: true
                text: Backend.mediaCachePath
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
                elide: Text.ElideMiddle
            }
        }
        SettingsRow {
            label: "Current size"
            Text {
                text: Backend.mediaCacheSize
                color: Theme.textPrimary
                font.pixelSize: Theme.fsSm
            }
        }
        SettingsRow {
            label: "Generated previews"
            Button {
                id: clearCacheButton
                text: "Clear cache"
                implicitWidth: 120
                implicitHeight: 30
                HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                onClicked: Backend.clearMediaCache()
                contentItem: Text {
                    text: clearCacheButton.text
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: clearCacheButton.down ? Theme.hover : Theme.bgPrimary
                    border.color: Theme.border
                    radius: Theme.radiusSm
                }
            }
        }
    }
}
