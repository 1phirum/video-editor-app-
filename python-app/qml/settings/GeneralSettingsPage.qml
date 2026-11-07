import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CutPro 1.0
import "../theme"

SettingsPage {
    id: root

    FolderDialog {
        id: workspaceFolderDialog
        title: qsTr("Choose workspace folder")
        onAccepted: Backend.chooseWorkspaceLocation(selectedFolder.toString())
    }

    SettingsSection {
        title: qsTr("Language")
        SettingsRow {
            label: qsTr("Interface language")
            SettingsComboBox {
                // Endonyms - shown identically in every UI language, so not qsTr'd.
                // `values` carries the persisted code; the model carries the label.
                property var values: ["en", "km"]
                model: ["English", "ខ្មែរ"]
                currentIndex: Math.max(0, values.indexOf(root.settings.language))
                onActivated: (index) => root.settingChanged("language", values[index])
            }
        }
    }

    SettingsSection {
        title: qsTr("Startup")
        SettingsRow {
            label: qsTr("Open workspace")
            SettingsComboBox {
                // Split display label (translatable) from stored value (never translated).
                property var values: ["Home", "Import", "Edit", "Export"]
                model: [qsTr("Home"), qsTr("Import"), qsTr("Edit"), qsTr("Export")]
                currentIndex: Math.max(0, values.indexOf(root.settings.startupWorkspace))
                onActivated: (index) => root.settingChanged("startupWorkspace", values[index])
            }
        }
        SettingsRow {
            label: qsTr("Workspace layout")
            SettingsComboBox {
                property var values: ["ESSENTIALS", "VERTICAL", "EFFECT EDITOR"]
                model: [qsTr("ESSENTIALS"), qsTr("VERTICAL"), qsTr("EFFECT EDITOR")]
                currentIndex: Math.max(0, values.indexOf(root.settings.startupLayout))
                onActivated: (index) => root.settingChanged("startupLayout", values[index])
            }
        }
    }

    SettingsSection {
        title: qsTr("Workspace")
        SettingsRow {
            label: qsTr("Projects folder")
            Text {
                Layout.fillWidth: true
                text: Backend.workspaceLocation
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
                elide: Text.ElideMiddle
            }
            Button {
                id: changeFolderButton
                text: qsTr("Change...")
                implicitWidth: 96
                implicitHeight: 30
                hoverEnabled: true
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                onClicked: workspaceFolderDialog.open()
                contentItem: Text {
                    text: changeFolderButton.text
                    color: changeFolderButton.hovered ? Theme.accent : Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: changeFolderButton.down ? Theme.hover
                           : changeFolderButton.hovered ? Theme.bgCardHeader
                           : Theme.bgPrimary
                    border.color: changeFolderButton.hovered ? Theme.accent : Theme.border
                    radius: Theme.radiusSm
                }
            }
        }
    }

    SettingsSection {
        title: qsTr("Project Panel")
        SettingsRow {
            label: qsTr("Default media view")
            SettingsComboBox {
                property var values: ["list", "grid"]
                model: [qsTr("list"), qsTr("grid")]
                currentIndex: Math.max(0, values.indexOf(root.settings.defaultMediaView))
                onActivated: (index) => root.settingChanged("defaultMediaView", values[index])
            }
        }
    }
}
