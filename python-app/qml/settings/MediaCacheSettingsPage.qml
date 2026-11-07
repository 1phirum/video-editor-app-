import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"

SettingsPage {
    id: root

    SettingsSection {
        title: qsTr("Preview Cache")
        SettingsRow {
            label: qsTr("Location")
            Text {
                Layout.fillWidth: true
                text: Backend.mediaCachePath
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
                elide: Text.ElideMiddle
            }
        }
        SettingsRow {
            label: qsTr("Current size")
            Text {
                text: Backend.mediaCacheSize
                color: Theme.textPrimary
                font.pixelSize: Theme.fsSm
            }
            // What the figure covers. The generated voices are the bulk of it - one
            // file per distinct subtitle line - and the button below deletes them,
            // so the count belongs next to the size rather than in a tooltip.
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: Backend.mediaCacheDetail
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
                elide: Text.ElideRight
            }
        }
        SettingsRow {
            label: qsTr("Previews and generated voices")
            help: qsTr("Removes preview files and every generated subtitle voice, including the voice clips on the timeline. Undoable.")
            Button {
                id: clearCacheButton
                text: Backend.mediaCacheBusy ? qsTr("Clearing...") : qsTr("Clear cache")
                enabled: !Backend.mediaCacheBusy
                implicitWidth: 120
                implicitHeight: 30
                HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                onClicked: Backend.clearMediaCache()
                contentItem: Text {
                    text: clearCacheButton.text
                    color: clearCacheButton.enabled ? Theme.textPrimary : Theme.textSecondary
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
