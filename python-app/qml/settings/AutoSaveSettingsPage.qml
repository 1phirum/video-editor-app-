import QtQuick
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"
import "../theme"

SettingsPage {
    id: root

    SettingsSection {
        title: qsTr("Project Auto Save")
        SettingsRow {
            label: qsTr("Enable auto save")
            CaptionSwitch {
                checked: root.settings.autoSaveEnabled !== false
                onToggled: root.settingChanged("autoSaveEnabled", checked)
            }
        }
        SettingsRow {
            label: qsTr("Save every")
            SettingsSpinBox {
                from: 1
                to: 120
                value: Number(root.settings.autoSaveIntervalMinutes || 5)
                enabled: root.settings.autoSaveEnabled !== false
                onValueModified: root.settingChanged("autoSaveIntervalMinutes", value)
            }
            Text {
                text: qsTr("minutes")
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
        }
    }
}
