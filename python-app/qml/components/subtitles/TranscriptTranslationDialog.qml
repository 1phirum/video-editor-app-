// qmllint disable
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
import "../timeline"
import "../../settings"

NativeModalWindow {
    id: dialog

    property var languageLabels: ["English", "Chinese", "Khmer", "Spanish",
                                  "French", "German", "Japanese", "Korean",
                                  "Vietnamese", "Thai", "Indonesian", "Portuguese",
                                  "Russian", "Italian", "Hindi", "Arabic"]
    property var languageCodes: ["en", "zh-CN", "km", "es", "fr", "de", "ja", "ko",
                                 "vi", "th", "id", "pt", "ru", "it", "hi", "ar"]
    property var appSettings: ({})
    property string activeProvider: "free"
    
    readonly property var geminiModels: [
        "gemini-2.5-flash", "gemini-2.5-flash-lite", "gemini-2.5-pro"
    ]
    readonly property var tabitokenModels: [
        "claude-opus-4-8", "claude-opus-4-8-thinking",
        "claude-opus-5", "claude-opus-5-thinking"
    ]

    // The catalogue Settings > Translation last read off the endpoint. The names
    // above are only what to offer before anyone has asked it, so this dialog
    // never offers a model the configured provider does not serve.
    readonly property var geminiChoices: {
        if (dialog.appSettings.geminiUsePool !== false && Pool.models.length > 0) {
            var choices = Pool.models.slice()
            var current = dialog.appSettings.geminiModel || dialog.appSettings.translationGeminiModel || "gemini-2.5-flash"
            if (choices.indexOf(current) < 0) choices.unshift(current)
            return choices
        }
        var discovered = dialog.splitList(dialog.appSettings.translationGeminiModels)
        return discovered.length > 0 ? discovered : dialog.geminiModels
    }
    readonly property var openAiChoices: {
        var discovered = dialog.splitList(dialog.appSettings.translationTabitokenModels)
        return discovered.length > 0 ? discovered : dialog.tabitokenModels
    }
    // Same name and host the settings page shows, so the card here does not still
    // say Tabitoken for an install pointed somewhere else.
    readonly property string openAiTitle: String(dialog.appSettings.translationTabitokenLabel || "") || "Tabitoken"
    readonly property string openAiDetail: {
        var host = dialog.hostOf(dialog.appSettings.translationTabitokenBaseUrl)
        return "OpenAI-compatible API" + (host !== "" ? " at " + host : "")
    }

    function splitList(value) {
        var result = []
        String(value || "").split(/[\n,;]+/).forEach(function(entry) {
            var name = entry.trim()
            if (name.length > 0 && result.indexOf(name) < 0)
                result.push(name)
        })
        return result
    }

    function hostOf(url) {
        var text = String(url || "").trim()
        if (text.length === 0)
            return ""
        return text.replace(/^[a-zA-Z][a-zA-Z0-9+.-]*:\/\//, "").split("/")[0].split("?")[0]
    }

    signal translationRequested(string languageCode)
    signal translationConfiguredRequested(string languageCode, var settings)
    signal providerTestRequested(var settings)

    dialogTitle: qsTr("Translate transcript")
    dialogWidth: Theme.translationDialogWidth
    dialogHeight: 530

    function open() {
        appSettings = Backend.appSettings
        activeProvider = String(appSettings.translationProvider || "free")
        openNative()
    }
    Rectangle {
        anchors.fill: parent
        color: Theme.translationDialogSurface
        border.color: Theme.translationRowBorder
        border.width: 1
        radius: Theme.translationDialogRadius
    }

    function currentSettings() {
        var result = JSON.parse(JSON.stringify(appSettings || {}))
        result.translationProvider = activeProvider
        return result
    }

    function providerDetail(code) {
        if (code === "gemini")
            return String(appSettings.geminiModel || appSettings.translationGeminiModel || "gemini-2.5-flash")
        if (code === "openai_compatible")
            return String(appSettings.translationTabitokenModel || "claude-opus-5")
        return "No API key required"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 14
            Layout.bottomMargin: 14
            spacing: 9

            Text { text: qsTr("Language"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            WhisperComboBox {
                id: languageSelector
                Layout.fillWidth: true
                model: dialog.languageLabels
                // The target language was always English again on reopen, which is
                // wrong for the one workflow this dialog exists for: a track gets
                // translated into the same language over and over.
                currentIndex: Math.max(0, dialog.languageCodes.indexOf(
                                           String(Backend.uiStateValue("translate/language", "en"))))
                onActivated: Backend.setUiState("translate/language",
                                                dialog.languageCodes[currentIndex])
                popupWidth: width
            }

            Text {
                Layout.topMargin: 5
                text: qsTr("Translation service")
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }

            Repeater {
                model: [
                    { code: "free", title: qsTr("Free translation"), description: qsTr("Google free translation") },
                    { code: "gemini", title: "Gemini", description: qsTr("Google Gemini API") },
                    { code: "openai_compatible", title: dialog.openAiTitle, description: dialog.openAiDetail }
                ]
                delegate: Rectangle {
                    id: providerCard
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    color: dialog.activeProvider === modelData.code ? Theme.translationRowHover : Theme.translationRow
                    border.width: 1
                    border.color: dialog.activeProvider === modelData.code ? Theme.accent : Theme.translationRowBorder
                    radius: Theme.translationRowRadius

                    HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: dialog.activeProvider = providerCard.modelData.code }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 11

                        Rectangle {
                            width: 18
                            height: 18
                            radius: 9
                            color: "transparent"
                            border.width: 1
                            border.color: dialog.activeProvider === providerCard.modelData.code ? Theme.accent : Theme.textMuted
                            Rectangle {
                                anchors.centerIn: parent
                                width: 10
                                height: 10
                                radius: 5
                                color: Theme.accent
                                visible: dialog.activeProvider === providerCard.modelData.code
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: providerCard.modelData.title; color: Theme.textPrimary; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                            Text { text: providerCard.modelData.description; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                        }

                        ColumnLayout {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            spacing: 2
                            Text {
                                visible: dialog.activeProvider === providerCard.modelData.code
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("Selected")
                                color: Theme.accent
                                font.pixelSize: Theme.fsXs
                            }
                            SettingsComboBox {
                                visible: dialog.activeProvider === providerCard.modelData.code && providerCard.modelData.code !== "free"
                                Layout.alignment: Qt.AlignRight
                                implicitWidth: 160
                                implicitHeight: 28
                                model: providerCard.modelData.code === "gemini" ? dialog.geminiChoices : dialog.openAiChoices
                                currentIndex: {
                                    var current = dialog.providerDetail(providerCard.modelData.code)
                                    return Math.max(0, model.indexOf(current))
                                }
                                onActivated: {
                                    if (providerCard.modelData.code === "gemini") {
                                        var shared = JSON.parse(JSON.stringify(Backend.appSettings || {}))
                                        shared.geminiModel = currentText
                                        shared.transcriptionGeminiModel = currentText
                                        shared.translationGeminiModel = currentText
                                        Backend.applyAppSettings(shared)
                                        dialog.appSettings = shared
                                    } else if (providerCard.modelData.code === "openai_compatible") {
                                        dialog.appSettings.translationTabitokenModel = currentText
                                    }
                                }
                            }
                            Text {
                                visible: !(dialog.activeProvider === providerCard.modelData.code && providerCard.modelData.code !== "free")
                                Layout.maximumWidth: 150
                                Layout.alignment: Qt.AlignRight
                                text: dialog.providerDetail(providerCard.modelData.code)
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fsXs
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: dialog.activeProvider === "free"
                      ? qsTr("Free translation does not use configured API keys.")
                      : dialog.activeProvider === "gemini" && dialog.appSettings.geminiUsePool !== false
                        ? dialog.appSettings.geminiBulkProcessing !== false
                          ? qsTr("Pool rotates ready Google accounts. Bulk processing runs text batches across ready accounts. Change the mode in Settings > Translation.")
                          : qsTr("Pool rotates ready Google accounts. Single processing runs one text batch at a time. Change the mode in Settings > Translation.")
                        : qsTr("API keys, model, and fallback order are managed in Settings > Translation.")
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
            }
            Item { Layout.fillHeight: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: Theme.bgSidebar
            radius: Theme.translationDialogRadius
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Cancel")
                    implicitWidth: 82
                    implicitHeight: 32
                    onClicked: dialog.close()
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.down ? Theme.hover : Theme.bgPrimary
                        border.color: Theme.textMuted
                        radius: height / 2
                    }
                }
                Button {
                    text: qsTr("Translate")
                    implicitWidth: 92
                    implicitHeight: 32
                    onClicked: {
                        dialog.translationConfiguredRequested(
                            dialog.languageCodes[languageSelector.currentIndex],
                            dialog.currentSettings())
                        dialog.close()
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: Theme.fsSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.down ? Theme.hover : Theme.accent
                        border.color: Theme.accent
                        radius: height / 2
                    }
                }
            }
        }
    }
}
