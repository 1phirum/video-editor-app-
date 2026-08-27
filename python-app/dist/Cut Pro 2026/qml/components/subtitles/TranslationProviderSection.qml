// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../settings"
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

Rectangle {
    id: root

    property string title: "Provider"
    property string description: ""
    property string providerCode: ""
    property bool active: false
    property bool expanded: false
    property bool showBaseUrl: false
    property bool allowCustomModel: false
    property bool selectable: false
    property bool busy: false
    property string statusText: ""
    property string modelValue: ""
    property string apiKeysValue: ""
    property string baseUrlValue: ""
    property var models: []
    property var apiKeys: []
    readonly property string currentModel: selectedModel()
    readonly property string currentApiKeys: collectKeys().join("\n")
    readonly property string currentBaseUrl: baseUrlField.text.trim()

    signal activateRequested()
    signal modelEdited(string value)
    signal apiKeysEdited(string value)
    signal baseUrlEdited(string value)
    signal testRequested(string model, string apiKeys, string baseUrl)

    Layout.fillWidth: true
    implicitHeight: sectionLayout.implicitHeight + 2
    color: Theme.translationRow
    border.color: active ? Theme.accent : Theme.translationRowBorder
    radius: Theme.translationRowRadius

    function selectedModel() {
        return modelSelector.currentText === "Custom model..."
                ? customModelField.text.trim() : modelSelector.currentText
    }

    function parseKeys(value) {
        var result = []
        String(value || "").split(/[,;\n\r]+/).forEach(function(candidate) {
            var key = candidate.trim()
            if (key.length > 0 && result.indexOf(key) < 0)
                result.push(key)
        })
        return result
    }

    function syncModel() {
        var index = models.indexOf(modelValue)
        if (index < 0 && allowCustomModel)
            index = models.indexOf("Custom model...")
        modelSelector.currentIndex = Math.max(0, index)
        if (index >= 0 && modelSelector.currentText === "Custom model...")
            customModelField.text = modelValue
    }

    function syncKeys() {
        var values = apiKeys || []
        if (values.length === 0)
            values = [""]
        while (apiKeysField.count > values.length)
            apiKeysField.remove(apiKeysField.count - 1)
        while (apiKeysField.count < values.length)
            apiKeysField.append({ value: "", revealed: false })
        for (var i = 0; i < values.length; ++i)
            apiKeysField.setProperty(i, "value", String(values[i]))
    }

    function emitKeys() {
        var values = []
        for (var i = 0; i < apiKeysField.count; ++i) {
            var value = String(apiKeysField.get(i).value || "").trim()
            if (value.length > 0 && values.indexOf(value) < 0)
                values.push(value)
        }
        root.apiKeysEdited(values.join("\n"))
    }

    onModelValueChanged: syncModel()
    onModelsChanged: syncModel()
    onApiKeysChanged: syncKeys()
    onApiKeysValueChanged: apiKeys = parseKeys(apiKeysValue)
    onActiveChanged: if (active) expanded = true
    Component.onCompleted: { syncModel(); apiKeys = parseKeys(apiKeysValue) }

    ListModel { id: apiKeysField }

    ColumnLayout {
        id: sectionLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: headerHover.hovered ? Theme.translationRowHover : Theme.translationRow
            radius: Theme.translationRowRadius
            HoverHandler { id: headerHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.expanded = !root.expanded }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 9

                RadioButton {
                    id: providerRadio
                    visible: root.selectable
                    checked: root.active
                    onClicked: root.activateRequested()
                    indicator: Rectangle {
                        implicitWidth: 16
                        implicitHeight: 16
                        x: 0
                        y: Math.round((parent.height - height) / 2)
                        radius: 8
                        color: "transparent"
                        border.width: 1
                        border.color: providerRadio.checked ? Theme.accent : Theme.textMuted
                        Rectangle {
                            anchors.centerIn: parent
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.accent
                            visible: providerRadio.checked
                        }
                    }
                    contentItem: Item {}
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { text: root.title; color: Theme.textPrimary; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: root.description; color: Theme.textMuted; font.pixelSize: Theme.fsXs; elide: Text.ElideRight }
                }

                ToolButton {
                    implicitWidth: 28
                    implicitHeight: 28
                    icon.source: root.expanded ? "../../assets/icons/chevron-up.svg" : "../../assets/icons/chevron-down.svg"
                    icon.width: 14
                    icon.height: 14
                    icon.color: Theme.textSecondary
                    background: Rectangle { color: parent.hovered ? Theme.hover : "transparent"; radius: Theme.radiusSm }
                    onClicked: root.expanded = !root.expanded
                    ToolTip.visible: hovered
                    ToolTip.text: root.expanded ? "Collapse" : "Expand"
                }
            }
        }

        ColumnLayout {
            visible: root.expanded
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Layout.bottomMargin: 12
            spacing: 8

            Text { text: "Model"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            SettingsComboBox {
                id: modelSelector
                Layout.fillWidth: true
                model: root.models
                onActivated: if (currentText !== "Custom model...") root.modelEdited(currentText)
            }
            TextField {
                id: customModelField
                visible: root.allowCustomModel && modelSelector.currentText === "Custom model..."
                Layout.fillWidth: true
                implicitHeight: 32
                placeholderText: "Custom model ID"
                placeholderTextColor: Theme.placeholderText
                text: root.modelValue
                color: Theme.textPrimary
                selectByMouse: true
                onEditingFinished: root.modelEdited(text.trim())
                background: Rectangle { color: Theme.bgPanel; border.color: parent.activeFocus ? Theme.accent : Theme.border; radius: Theme.radiusSm }
            }

            Text { visible: root.showBaseUrl; text: "Base URL"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            TextField {
                id: baseUrlField
                visible: root.showBaseUrl
                Layout.fillWidth: true
                implicitHeight: 32
                text: root.baseUrlValue
                placeholderText: "https://tabitoken.com/v1"
                placeholderTextColor: Theme.placeholderText
                color: Theme.textPrimary
                selectByMouse: true
                onEditingFinished: root.baseUrlEdited(text.trim())
                background: Rectangle { color: Theme.bgPanel; border.color: parent.activeFocus ? Theme.accent : Theme.border; radius: Theme.radiusSm }
            }

            Text { text: "API keys"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: apiKeysField
                    delegate: RowLayout {
                        required property int index
                        required property string value
                        required property bool revealed
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: keyField
                            Layout.fillWidth: true
                            implicitHeight: 32
                            text: value
                            echoMode: revealed ? TextInput.Normal : TextInput.Password
                            placeholderText: index === 0 ? "API key" : "Backup API key"
                            placeholderTextColor: Theme.placeholderText
                            color: Theme.textPrimary
                            selectByMouse: true
                            rightPadding: 36
                            onTextChanged: {
                                if (text !== value) apiKeysField.setProperty(index, "value", text)
                            }
                            onEditingFinished: root.emitKeys()
                            background: Rectangle { color: Theme.bgPanel; border.color: parent.activeFocus ? Theme.accent : Theme.border; radius: Theme.radiusSm }
                            ToolButton {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                implicitWidth: 30
                                implicitHeight: 30
                                icon.source: revealed ? "../../assets/icons/eye-off.svg" : "../../assets/icons/eye.svg"
                                icon.width: 17
                                icon.height: 17
                                icon.color: "white"
                                background: Rectangle { color: parent.hovered ? Theme.hover : "transparent"; radius: Theme.radiusSm }
                                onClicked: apiKeysField.setProperty(index, "revealed", !revealed)
                                ToolTip.visible: hovered
                                ToolTip.text: revealed ? "Hide API key" : "Show API key"
                            }
                        }
                        ToolButton {
                            visible: apiKeysField.count > 1
                            implicitWidth: 30
                            implicitHeight: 30
                            icon.source: "../../assets/icons/trash-2.svg"
                            icon.width: 16
                            icon.height: 16
                            icon.color: Theme.danger
                            background: Rectangle { color: parent.hovered ? Theme.hover : "transparent"; radius: Theme.radiusSm }
                            onClicked: { apiKeysField.remove(index); root.emitKeys() }
                            ToolTip.visible: hovered
                            ToolTip.text: "Remove API key"
                        }
                    }
                }
            }
            Button {
                implicitWidth: 90
                implicitHeight: 30
                text: "Add API"
                onClicked: { apiKeysField.append({ value: "", revealed: false }); root.emitKeys() }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                background: Rectangle {
                    color: parent.down ? Theme.hover : Theme.bgPrimary
                    border.color: Theme.border
                    radius: Theme.radiusSm
                }
                contentItem: Row {
                    spacing: 6
                    anchors.centerIn: parent
                    Image {
                        source: "../../assets/icons/plus.svg"
                        width: 14
                        height: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: parent.parent.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsSm
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    implicitWidth: 120
                    implicitHeight: 30
                    text: root.busy ? "Testing..." : "Test connection"
                    enabled: !root.busy
                    onClicked: root.testRequested(root.selectedModel(), root.collectKeys().join("\n"), baseUrlField.text.trim())
                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
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
                Text { Layout.fillWidth: true; text: root.statusText; color: root.statusText.toLowerCase().indexOf("fail") >= 0 ? Theme.danger : Theme.textMuted; font.pixelSize: Theme.fsXs; elide: Text.ElideRight }
            }
        }
    }

    function collectKeys() {
        var values = []
        for (var i = 0; i < apiKeysField.count; ++i) {
            var value = String(apiKeysField.get(i).value || "").trim()
            if (value.length > 0 && values.indexOf(value) < 0) values.push(value)
        }
        return values
    }
}
