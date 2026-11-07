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
    property string checkedProvider: ""
    readonly property string provider: String(root.settings.translationProvider || "free")
    // Starting points only. Both lists are replaced by whatever the endpoint
    // itself reports the moment someone presses Check models, because no list
    // compiled into the app can keep up with a gateway's catalogue.
    readonly property var geminiModels: [
        "gemini-2.5-flash", "gemini-2.5-flash-lite", "gemini-2.5-pro"
    ]
    // No built-in list: this endpoint is any OpenAI-compatible gateway, so the
    // only real catalogue is whatever it answers to Check models. Until then the
    // picker offers just "Custom model...", and if a checked endpoint serves no
    // models it stays that way rather than showing names that are not there.
    readonly property var openAiModels: []
    readonly property var geminiDiscovered: root.splitList(root.settings.translationGeminiModels)
    readonly property var openAiDiscovered: root.splitList(root.settings.translationTabitokenModels)
    // The section's name follows the endpoint once it has answered, so an install
    // pointed at any gateway names itself after that gateway. Until then it reads
    // as the generic thing it is - any OpenAI-compatible endpoint, not one site.
    readonly property string openAiLabel: String(root.settings.translationTabitokenLabel || "") || qsTr("OpenAI-compatible")
    readonly property string openAiHost: root.hostOf(root.settings.translationTabitokenBaseUrl)

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

    function settingsFor(providerCode, model, apiKeys, baseUrl) {
        var result = JSON.parse(JSON.stringify(root.settings || {}))
        result.translationProvider = providerCode
        if (providerCode === "gemini") {
            result.geminiModel = model
            result.translationGeminiModel = model
            result.translationGeminiApiKeys = apiKeys
        } else {
            result.translationTabitokenModel = model
            result.translationTabitokenApiKeys = apiKeys
            result.translationTabitokenBaseUrl = baseUrl
        }
        return result
    }

    // Only one of the two readouts is ever the answer to what was just pressed,
    // so each action clears the other section's claim on the status line.
    function runTest(providerCode, model, keys, baseUrl) {
        root.testedProvider = providerCode
        root.checkedProvider = ""
        Backend.testTranslationProvider(root.settingsFor(providerCode, model, keys, baseUrl))
    }

    function runModelCheck(providerCode, model, keys, baseUrl) {
        if (providerCode === "gemini" && root.settings.geminiUsePool !== false) {
            Pool.checkModels()
            return
        }
        root.checkedProvider = providerCode
        root.testedProvider = ""
        Backend.checkTranslationModels(root.settingsFor(providerCode, model, keys, baseUrl))
    }

    Connections {
        target: Backend

        function onTranslationModelsChecked(ok, models, label, baseUrl, message) {
            if (!ok || root.checkedProvider === "")
                return
            var names = models || []
            var joined = names.join("\n")
            if (root.checkedProvider === "gemini") {
                root.settingChanged("translationGeminiModels", joined)
                if (names.length > 0
                        && names.indexOf(String(root.settings.translationGeminiModel || "")) < 0)
                    root.settingChanged("translationGeminiModel", names[0])
                return
            }
            root.settingChanged("translationTabitokenModels", joined)
            // The name and the corrected URL come back from the same answer: the
            // worker had to resolve the host to ask, so what it reached is what
            // gets saved, scheme included.
            if (String(label || "").length > 0)
                root.settingChanged("translationTabitokenLabel", label)
            if (String(baseUrl || "").length > 0)
                root.settingChanged("translationTabitokenBaseUrl", baseUrl)
            // A model kept from an older provider is not in the new catalogue.
            // Leaving it selected would fail on the next translation with a
            // confusing "no such model", so it moves to the first real one.
            if (names.length > 0
                    && names.indexOf(String(root.settings.translationTabitokenModel || "")) < 0)
                root.settingChanged("translationTabitokenModel", names[0])
        }
    }

    PoolSettingsSection {
        settings: root.settings
        onSettingChanged: (key, value) => root.settingChanged(key, value)
    }

    SettingsSection {
        title: qsTr("Translation provider configuration")
        help: qsTr("Gemini and any OpenAI-compatible endpoint are configured here. Check models asks the endpoint what it serves and fills the picker with those names. Choose the provider only when you translate a transcript.")

        TranslationProviderSection {
            title: qsTr("Gemini")
            description: qsTr("Shared Gemini model for transcription and translation")
            providerCode: "gemini"
            retainModelValue: true
            active: false
            expanded: true
            models: root.geminiModels
            discoveredModels: root.settings.geminiUsePool !== false && Pool.models.length > 0 ? Pool.models : root.geminiDiscovered
            modelValue: String(root.settings.geminiModel || root.settings.translationGeminiModel || "gemini-2.5-flash")
            apiKeysValue: String(root.settings.translationGeminiApiKeys || "")
            busy: Backend.translationTestInProgress && root.testedProvider === providerCode
            statusText: root.testedProvider === providerCode ? Backend.translationStatus : ""
            checkingModels: root.settings.geminiUsePool !== false ? Pool.checkingModels : Backend.translationModelsInProgress && root.checkedProvider === providerCode
            modelsStatus: root.settings.geminiUsePool !== false ? Pool.modelStatus : root.checkedProvider === providerCode ? Backend.translationModelsStatus : ""
            onActivateRequested: {}
            onModelEdited: value => root.settingChanged("geminiModel", value)
            onApiKeysEdited: value => root.settingChanged("translationGeminiApiKeys", value)
            onTestRequested: (model, keys, baseUrl) => root.runTest(providerCode, model, keys, baseUrl)
            onModelsRequested: (model, keys, baseUrl) => root.runModelCheck(providerCode, model, keys, baseUrl)
        }

        TranslationProviderSection {
            title: root.openAiLabel
            description: root.openAiHost !== ""
                         ? qsTr("OpenAI-compatible API at %1").arg(root.openAiHost)
                         : qsTr("OpenAI-compatible API")
            providerCode: "openai_compatible"
            active: false
            expanded: true
            showBaseUrl: true
            allowCustomModel: true
            models: root.openAiModels
            discoveredModels: root.openAiDiscovered
            modelValue: String(root.settings.translationTabitokenModel || "claude-opus-5")
            apiKeysValue: String(root.settings.translationTabitokenApiKeys || "")
            baseUrlValue: String(root.settings.translationTabitokenBaseUrl || "https://tabitoken.com/v1")
            busy: Backend.translationTestInProgress && root.testedProvider === providerCode
            statusText: root.testedProvider === providerCode ? Backend.translationStatus : ""
            checkingModels: Backend.translationModelsInProgress && root.checkedProvider === providerCode
            modelsStatus: root.checkedProvider === providerCode ? Backend.translationModelsStatus : ""
            onActivateRequested: {}
            onModelEdited: value => root.settingChanged("translationTabitokenModel", value)
            onApiKeysEdited: value => root.settingChanged("translationTabitokenApiKeys", value)
            onBaseUrlEdited: value => root.settingChanged("translationTabitokenBaseUrl", value)
            onTestRequested: (model, keys, baseUrl) => root.runTest(providerCode, model, keys, baseUrl)
            onModelsRequested: (model, keys, baseUrl) => root.runModelCheck(providerCode, model, keys, baseUrl)
        }
    }

    SettingsSection {
        title: qsTr("Key fallback")
        help: qsTr("Separate multiple keys with commas. Cut Pro automatically tries the next key after rate limits, quota errors, expired credit, or temporary provider failures. Gemini and OpenAI-compatible keys are stored separately.")
    }
}
