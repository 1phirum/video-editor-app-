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
        title: "Project Auto Save"
        SettingsRow {
            label: "Enable auto save"
            CaptionSwitch {
                checked: root.settings.autoSaveEnabled !== false
                onToggled: root.settingChanged("autoSaveEnabled", checked)
            }
        }
        SettingsRow {
            label: "Save every"
            SettingsSpinBox {
                from: 1
                to: 120
                value: Number(root.settings.autoSaveIntervalMinutes || 5)
                enabled: root.settings.autoSaveEnabled !== false
                onValueModified: root.settingChanged("autoSaveIntervalMinutes", value)
            }
            Text {
                text: "minutes"
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
        }
    }
}
