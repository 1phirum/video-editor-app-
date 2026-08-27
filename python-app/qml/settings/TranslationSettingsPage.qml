// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
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

    property string testedProvider: ""
    readonly property string provider: String(root.settings.translationProvider || "free")
    readonly property var geminiModels: [
        "gemini-2.0-flash", "gemini-2.0-flash-lite",
        "gemini-2.5-flash", "gemini-2.5-flash-lite", "gemini-2.5-pro",
        "gemini-3-flash", "gemini-3.1-pro", "gemini-3.1-flash-lite",
        "gemini-3.5-flash", "gemini-3.5-flash-lite",
        "gemini-3.6-flash", "gemini-3.7-flash"
    ]
    readonly property var tabitokenModels: [
        "claude-opus-4-8", "claude-opus-4-8-thinking",
        "claude-opus-5", "claude-opus-5-thinking", "Custom model..."
    ]

    function settingsFor(providerCode, model, apiKeys, baseUrl) {
        var result = JSON.parse(JSON.stringify(root.settings || {}))
        result.translationProvider = providerCode
        if (providerCode === "gemini") {
            result.translationGeminiModel = model
            result.translationGeminiApiKeys = apiKeys
        } else {
            result.translationTabitokenModel = model
            result.translationTabitokenApiKeys = apiKeys
            result.translationTabitokenBaseUrl = baseUrl
        }
        return result
    }

    SettingsSection {
        title: "Translation provider configuration"

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Configure Gemini and Tabitoken here. Choose the provider only when you translate a transcript."
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
        }

        TranslationProviderSection {
            title: "Gemini"
            description: "Google Gemini API with automatic key fallback"
            providerCode: "gemini"
            active: false
            expanded: true
            models: root.geminiModels
            modelValue: String(root.settings.translationGeminiModel || "gemini-2.5-flash")
            apiKeysValue: String(root.settings.translationGeminiApiKeys || "")
            busy: Backend.translationTestInProgress && root.testedProvider === providerCode
            statusText: root.testedProvider === providerCode ? Backend.translationStatus : ""
            onActivateRequested: {}
            onModelEdited: value => root.settingChanged("translationGeminiModel", value)
            onApiKeysEdited: value => root.settingChanged("translationGeminiApiKeys", value)
            onTestRequested: (model, keys, baseUrl) => {
                root.testedProvider = providerCode
                Backend.testTranslationProvider(root.settingsFor(providerCode, model, keys, baseUrl))
            }
        }

        TranslationProviderSection {
            title: "Tabitoken"
            description: "OpenAI-compatible API at tabitoken.com"
            providerCode: "openai_compatible"
            active: false
            expanded: true
            showBaseUrl: true
            allowCustomModel: true
            models: root.tabitokenModels
            modelValue: String(root.settings.translationTabitokenModel || "claude-opus-5")
            apiKeysValue: String(root.settings.translationTabitokenApiKeys || "")
            baseUrlValue: String(root.settings.translationTabitokenBaseUrl || "https://tabitoken.com/v1")
            busy: Backend.translationTestInProgress && root.testedProvider === providerCode
            statusText: root.testedProvider === providerCode ? Backend.translationStatus : ""
            onActivateRequested: {}
            onModelEdited: value => root.settingChanged("translationTabitokenModel", value)
            onApiKeysEdited: value => root.settingChanged("translationTabitokenApiKeys", value)
            onBaseUrlEdited: value => root.settingChanged("translationTabitokenBaseUrl", value)
            onTestRequested: (model, keys, baseUrl) => {
                root.testedProvider = providerCode
                Backend.testTranslationProvider(root.settingsFor(providerCode, model, keys, baseUrl))
            }
        }
    }

    SettingsSection {
        title: "Key fallback"
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Separate multiple keys with commas. Cut Pro automatically tries the next key after rate limits, quota errors, expired credit, or temporary provider failures. Gemini and Tabitoken keys are stored separately."
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
        }
    }
}
