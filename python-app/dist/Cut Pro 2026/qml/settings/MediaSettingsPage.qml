import QtQuick
import "../theme"

SettingsPage {
    id: root

    SettingsSection {
        title: "Still Images"
        SettingsRow {
            label: "Default duration"
            SettingsSpinBox {
                from: 1
                to: 60
                value: Math.round(Number(root.settings.defaultImageDurationMs || 5000) / 1000)
                onValueModified: root.settingChanged("defaultImageDurationMs", value * 1000)
            }
            Text {
                text: "seconds"
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
        }
    }
}
