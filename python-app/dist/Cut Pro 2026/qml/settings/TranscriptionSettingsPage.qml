import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

SettingsPage {
    id: root
    property var languageLabels: ["Auto detect", "English", "Chinese", "Khmer",
                                  "Spanish", "French", "German", "Japanese",
                                  "Korean", "Vietnamese"]
    property var languageCodes: ["auto", "en", "zh", "km", "es", "fr", "de",
                                 "ja", "ko", "vi"]

    SettingsSection {
        title: "Whisper"
        SettingsRow {
            label: "Default model"
            SettingsComboBox {
                model: ["tiny", "base", "small", "medium", "large-v3", "turbo"]
                currentIndex: Math.max(0, model.indexOf(root.settings.transcriptionModel))
                onActivated: root.settingChanged("transcriptionModel", currentText)
            }
        }
        SettingsRow {
            label: "Default language"
            SettingsComboBox {
                model: root.languageLabels
                currentIndex: Math.max(0, root.languageCodes.indexOf(
                                           root.settings.transcriptionLanguage))
                onActivated: root.settingChanged(
                                 "transcriptionLanguage",
                                 root.languageCodes[currentIndex])
            }
        }
        SettingsRow {
            label: "Python executable"
            TextField {
                Layout.fillWidth: true
                implicitHeight: 32
                text: String(root.settings.pythonExecutable || "python")
                color: Theme.textPrimary
                font.pixelSize: Theme.fsSm
                selectByMouse: true
                onEditingFinished: root.settingChanged("pythonExecutable", text)
                background: Rectangle {
                    color: Theme.bgPrimary
                    border.color: parent.activeFocus ? Theme.accent : Theme.border
                    radius: Theme.radiusSm
                }
            }
        }
    }
}
