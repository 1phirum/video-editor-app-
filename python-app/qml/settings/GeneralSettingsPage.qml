import QtQuick

SettingsPage {
    id: root

    SettingsSection {
        title: "Startup"
        SettingsRow {
            label: "Open workspace"
            SettingsComboBox {
                model: ["Import", "Edit", "Export"]
                currentIndex: Math.max(0, model.indexOf(root.settings.startupWorkspace))
                onActivated: root.settingChanged("startupWorkspace", currentText)
            }
        }
        SettingsRow {
            label: "Workspace layout"
            SettingsComboBox {
                model: ["ESSENTIALS", "VERTICAL"]
                currentIndex: Math.max(0, model.indexOf(root.settings.startupLayout))
                onActivated: root.settingChanged("startupLayout", currentText)
            }
        }
    }

    SettingsSection {
        title: "Project Panel"
        SettingsRow {
            label: "Default media view"
            SettingsComboBox {
                model: ["list", "grid"]
                currentIndex: Math.max(0, model.indexOf(root.settings.defaultMediaView))
                onActivated: root.settingChanged("defaultMediaView", currentText)
            }
        }
    }
}
