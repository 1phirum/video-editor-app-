pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"
import "../pool"
import "../components/subtitles"

SettingsSection {
    id: root
    property var settings: ({})
    signal settingChanged(string key, var value)
    readonly property bool usePool: settings.geminiUsePool !== false
    readonly property bool bulkProcessing: settings.geminiBulkProcessing !== false
    readonly property var accounts: Pool.state.accounts || []
    readonly property int ready: Pool.state.readyCount || 0
    readonly property var accountLabels: {
        var labels = [qsTr("Automatic rotation · %1 ready accounts").arg(root.ready)]
        for (var account of root.accounts)
            labels.push(account.email + " · " + (account.enabled === false ? qsTr("Disabled") : account.status === "ready" ? qsTr("Ready") : account.status === "cooldown" ? qsTr("Cooling down") : qsTr("Needs attention")))
        return labels
    }
    title: qsTr("Account Pool")
    help: qsTr("Google accounts share their Gemini quota. Turn bulk processing on to distribute work across ready accounts, or off to process one video or text batch at a time. Each account handles one active request at a time.")

    component ActionButton: Button {
        implicitHeight: 30
        implicitWidth: implicitContentWidth + 24
        contentItem: Text {
            text: parent.text; color: Theme.textPrimary; font.pixelSize: Theme.fsSm
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            opacity: parent.enabled ? 1 : 0.5
        }
        background: Rectangle {
            color: parent.down ? Theme.hover : Theme.bgPrimary
            border.color: Theme.border; radius: Theme.radiusSm
            opacity: parent.enabled ? 1 : 0.5
        }
    }
    PoolWindow { id: poolWindow; ownerWindow: root.Window.window }
    Connections {
        target: Pool
        function onModelsChecked(ok, models) {
            if (!ok) return
            root.settingChanged("translationGeminiModels", models.join("\n"))
            var current = root.settings.geminiModel || root.settings.transcriptionGeminiModel || "gemini-2.5-flash"
            if (models.indexOf(current) < 0)
                root.settingChanged("geminiModel", models.indexOf("gemini-2.5-flash") >= 0 ? "gemini-2.5-flash" : models[0])
        }
    }
    SettingsRow {
        label: qsTr("Gemini source")
        SettingsComboBox {
            model: [qsTr("Account Pool (Google sign-in)"), qsTr("API keys")]
            currentIndex: root.usePool ? 0 : 1
            onActivated: root.settingChanged("geminiUsePool", currentIndex === 0)
        }
        ActionButton { text: qsTr("Open Pool"); onClicked: poolWindow.openNative() }
    }
    SettingsRow {
        label: qsTr("Connected accounts")
        SettingsComboBox {
            id: accountList
            Layout.fillWidth: true
            model: root.accountLabels
            currentIndex: 0
            onActivated: currentIndex = 0
            delegate: ItemDelegate {
                required property int index
                width: accountList.popup.width - 2
                height: 34
                enabled: index === 0
                contentItem: Text {
                    text: accountList.textAt(parent.index)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle { color: Theme.bgPanel }
            }
        }
    }
    SettingsRow {
        label: qsTr("Bulk processing")
        help: qsTr("Shared by transcription and translation. On: selected videos and text batches run across ready accounts. Off: each job processes one video or text batch at a time. Applies to new jobs after pressing OK.")
        CaptionSwitch {
            objectName: "geminiBulkProcessingSwitch"
            enabled: root.usePool
            checked: root.bulkProcessing
            text: root.bulkProcessing ? qsTr("Bulk · parallel") : qsTr("Single · one at a time")
            onToggled: root.settingChanged("geminiBulkProcessing", checked)
        }
    }
    SettingsRow {
        label: qsTr("Account checks")
        ActionButton {
            text: qsTr("Verify accounts")
            enabled: root.accounts.length > 0 && !Pool.busy && !Pool.checkingModels
            onClicked: Pool.verifyAll()
        }
        ActionButton {
            text: Pool.checkingModels ? qsTr("Checking models…") : qsTr("Check all models")
            enabled: root.accounts.length > 0 && !Pool.busy && !Pool.checkingModels && root.usePool
            onClicked: Pool.checkModels()
        }
        ActionButton {
            text: qsTr("+ Add account")
            enabled: !Pool.busy
            onClicked: { poolWindow.openNative(); Pool.addAccount() }
        }
    }
    Text {
        Layout.fillWidth: true
        text: root.usePool ? root.bulkProcessing
                           ? qsTr("%1 ready accounts · Up to %2 videos or text batches at once · API keys remain the fallback.").arg(root.ready).arg(Math.max(1, Math.min(8, root.ready)))
                           : qsTr("%1 ready accounts · One video or text batch at a time · Accounts still rotate automatically.").arg(root.ready)
                           : qsTr("Transcription and translation use the configured Gemini API keys.")
        color: Theme.textMuted; font.pixelSize: Theme.fsSm; wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        visible: root.usePool && Pool.models.length > 0 && Pool.models.indexOf(root.settings.geminiModel || root.settings.transcriptionGeminiModel || "gemini-2.5-flash") < 0
        text: qsTr("The saved Gemini model is absent from the checked Pool catalogue. Check all models or choose an available model below.")
        color: Theme.danger; font.pixelSize: Theme.fsSm; wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        visible: Pool.modelStatus !== ""
        text: Pool.modelStatus
        color: Theme.textSecondary; font.pixelSize: Theme.fsSm
        wrapMode: Text.Wrap
    }
    onVisibleChanged: if (visible) Pool.refresh()
}
