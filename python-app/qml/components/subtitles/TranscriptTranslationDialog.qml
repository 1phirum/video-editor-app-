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

    property var languageLabels: ["English", "Chinese", "Khmer", "Spanish"]
    property var languageCodes: ["en", "zh-CN", "km", "es"]
    property var appSettings: ({})
    property string activeProvider: "free"
    
    readonly property var geminiModels: [
        "gemini-3-flash", "gemini-3.1-pro", "gemini-3.1-flash-lite",
        "gemini-3.5-flash", "gemini-3.5-flash-lite",
        "gemini-3.6-flash", "gemini-3.7-flash"
    ]
    readonly property var tabitokenModels: [
        "claude-opus-4-8", "claude-opus-4-8-thinking",
        "claude-opus-5", "claude-opus-5-thinking"
    ]

    signal translationRequested(string languageCode)
    signal translationConfiguredRequested(string languageCode, var settings)
    signal providerTestRequested(var settings)

    dialogTitle: "Translate transcript"
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
            return String(appSettings.translationGeminiModel || "gemini-2.5-flash")
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

            Text { text: "Language"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            WhisperComboBox {
                id: languageSelector
                Layout.fillWidth: true
                model: dialog.languageLabels
                currentIndex: 0
                popupWidth: width
            }

            Text {
                Layout.topMargin: 5
                text: "Translation service"
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }

            Repeater {
                model: [
                    { code: "free", title: "Free translation", description: "Google free translation" },
                    { code: "gemini", title: "Gemini", description: "Google Gemini API" },
                    { code: "openai_compatible", title: "Tabitoken", description: "OpenAI-compatible API" }
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
                                text: "Selected"
                                color: Theme.accent
                                font.pixelSize: Theme.fsXs
                            }
                            SettingsComboBox {
                                visible: dialog.activeProvider === providerCard.modelData.code && providerCard.modelData.code !== "free"
                                Layout.alignment: Qt.AlignRight
                                implicitWidth: 160
                                implicitHeight: 28
                                model: providerCard.modelData.code === "gemini" ? dialog.geminiModels : dialog.tabitokenModels
                                currentIndex: {
                                    var current = dialog.providerDetail(providerCard.modelData.code)
                                    return Math.max(0, model.indexOf(current))
                                }
                                onActivated: {
                                    if (providerCard.modelData.code === "gemini") {
                                        dialog.appSettings.translationGeminiModel = currentText
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
                      ? "Free translation does not use configured API keys."
                      : "API keys, model, and fallback order are managed in Settings > Translation."
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
                    text: "Cancel"
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
                    text: "Translate"
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
