// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"
import "../components/subtitles"

SettingsPage {
    id: root
    property var languageLabels: [qsTr("Auto detect"), qsTr("English"), qsTr("Chinese"), qsTr("Khmer"),
                                  qsTr("Spanish"), qsTr("French"), qsTr("German"), qsTr("Japanese"),
                                  qsTr("Korean"), qsTr("Vietnamese")]
    property var languageCodes: ["auto", "en", "zh", "km", "es", "fr", "de",
                                 "ja", "ko", "vi"]
    // "gemini" is not a checkpoint. It routes the job to the cloud worker
    // instead, and the model behind it is the Gemini section's own row.
    readonly property var modelValues: ["tiny", "base", "small", "medium",
                                        "large-v3", "turbo", "gemini"]
    readonly property var modelLabels: ["tiny", "base", "small", "medium",
                                        "large-v3", "turbo", "Gemini AI"]
    readonly property var geminiModels: {
        var values = root.settings.geminiUsePool !== false && Pool.models.length > 0
                     ? Pool.models.slice() : String(root.settings.translationGeminiModels || "").split(/[\n,;]+/).filter(function(value) { return value.trim() !== "" })
        if (values.length === 0) values = ["gemini-2.5-flash", "gemini-2.5-pro"]
        var current = root.settings.geminiModel || root.settings.transcriptionGeminiModel || "gemini-2.5-flash"
        if (values.indexOf(current) < 0) values.unshift(current)
        return values
    }
    property bool keyTested: false

    // Split the way the worker splits them, so this is the number of keys a run
    // would actually have to fall back through. The keys themselves stay in
    // Settings > Translation and are never shown here.
    readonly property int geminiKeyCount: {
        var parts = String(root.settings.translationGeminiApiKeys || "")
                        .split(/[,;\r\n]+/)
        var total = 0
        for (var i = 0; i < parts.length; ++i)
            if (parts[i].trim().length > 0)
                total += 1
        return total
    }

    // The check is the translation round trip, which is the same endpoint and the
    // same keys, so the model has to be named in the field that path reads.
    function geminiTestSettings() {
        var result = JSON.parse(JSON.stringify(root.settings || {}))
        result.translationProvider = "gemini"
        result.geminiModel = String(root.settings.geminiModel
                                   || root.settings.transcriptionGeminiModel
                                   || "gemini-2.5-flash")
        result.translationGeminiModel = result.geminiModel
        return result
    }

    SettingsSection {
        title: qsTr("Engine")
        SettingsRow {
            label: qsTr("Default model")
            SettingsComboBox {
                model: root.modelLabels
                currentIndex: Math.max(0, root.modelValues.indexOf(
                                           String(root.settings.transcriptionModel
                                                  || "small")))
                onActivated: root.settingChanged(
                                 "transcriptionModel",
                                 root.modelValues[currentIndex])
            }
        }
        SettingsRow {
            label: qsTr("Default language")
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
            label: qsTr("Python executable")
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

    PoolSettingsSection {
        settings: root.settings
        onSettingChanged: (key, value) => root.settingChanged(key, value)
    }

    SettingsSection {
        title: qsTr("Gemini")
        help: qsTr("Picked by choosing Gemini AI as the model. The audio is uploaded in eight minute windows and the lines come back timed, so nothing is downloaded to this machine and the speed does not depend on it.")

        SettingsRow {
            label: qsTr("Shared Gemini model")
            SettingsComboBox {
                model: root.geminiModels
                currentIndex: Math.max(0, model.indexOf(
                                           String(root.settings.geminiModel || root.settings.transcriptionGeminiModel
                                                  || "gemini-2.5-flash")))
                onActivated: root.settingChanged("geminiModel",
                                                 currentText)
            }
        }

        SettingsRow {
            label: qsTr("Recap movie")
            help: qsTr("When on, transcribing with Gemini writes a movie recap instead of a plain transcript: the lines follow each scene, name who is speaking, and add short connective narration so the story reads on its own. Same single pass, no extra cost.")
            CaptionSwitch {
                checked: root.settings.transcriptionRecapMovie === true
                onToggled: root.settingChanged("transcriptionRecapMovie", checked)
            }
        }

        SettingsRow {
            label: qsTr("Fallback API keys")
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.geminiKeyCount > 0
                          ? (root.geminiKeyCount === 1
                             ? qsTr("1 key, shared with Translation")
                             : qsTr("%1 keys, shared with Translation").arg(root.geminiKeyCount))
                          : root.settings.geminiUsePool !== false
                            ? qsTr("Optional · Pool uses your Google accounts")
                            : qsTr("No key yet - add one under Translation")
                    color: root.geminiKeyCount > 0 ? Theme.textSecondary
                           : root.settings.geminiUsePool !== false ? Theme.textMuted : Theme.danger
                    font.pixelSize: Theme.fsSm
                }
                Button {
                    implicitWidth: 110
                    implicitHeight: 30
                    text: Backend.translationTestInProgress ? qsTr("Testing...")
                                                            : qsTr("Test key")
                    enabled: !Backend.translationTestInProgress
                             && root.geminiKeyCount > 0
                    onClicked: {
                        root.keyTested = true
                        Backend.testTranslationProvider(root.geminiTestSettings())
                    }
                    HoverHandler {
                        cursorShape: parent.enabled ? Qt.PointingHandCursor
                                                    : Qt.ArrowCursor
                    }
                    background: Rectangle {
                        color: parent.down ? Theme.hover : Theme.bgPrimary
                        border.color: Theme.border
                        radius: Theme.radiusSm
                        opacity: parent.enabled ? 1.0 : 0.5
                    }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: parent.enabled ? 1.0 : 0.5
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.keyTested && String(Backend.translationStatus) !== ""
            wrapMode: Text.WordWrap
            text: String(Backend.translationStatus)
            color: String(Backend.translationStatus).toLowerCase()
                       .indexOf("fail") >= 0 ? Theme.danger : Theme.textMuted
            font.pixelSize: Theme.fsXs
        }
    }
}
