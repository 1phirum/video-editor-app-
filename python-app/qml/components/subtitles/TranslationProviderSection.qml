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

    property string title: qsTr("Provider")
    property string description: ""
    property string providerCode: ""
    property bool active: false
    property bool expanded: false
    property bool showBaseUrl: false
    property bool allowCustomModel: false
    property bool retainModelValue: false
    property bool selectable: false
    property bool busy: false
    property string statusText: ""
    property string modelValue: ""
    property string apiKeysValue: ""
    property string baseUrlValue: ""
    property var models: []
    // What the endpoint answered when it was last asked, newest first press
    // wins. Empty until someone presses Check models.
    property var discoveredModels: []
    property bool checkingModels: false
    property string modelsStatus: ""
    property var apiKeys: []
    readonly property string currentModel: selectedModel()
    readonly property string currentApiKeys: collectKeys().join("\n")
    readonly property string currentBaseUrl: baseUrlField.text.trim()

    // What the combo box offers: the catalogue the endpoint published, and the
    // built-in list only until it has published one. "Custom model..." stays
    // last so a name the catalogue does not advertise can still be typed.
    readonly property var modelChoices: {
        var names = []
        var discovered = root.discoveredModels || []
        for (var i = 0; i < discovered.length; ++i) {
            var name = String(discovered[i]).trim()
            if (name.length > 0 && names.indexOf(name) < 0)
                names.push(name)
        }
        if (names.length === 0) {
            var builtIn = root.models || []
            for (var j = 0; j < builtIn.length; ++j) {
                var fallback = String(builtIn[j])
                if (fallback.length > 0 && names.indexOf(fallback) < 0)
                    names.push(fallback)
            }
        }
        if (root.retainModelValue && root.modelValue !== "" && names.indexOf(root.modelValue) < 0)
            names.unshift(root.modelValue)
        if (root.allowCustomModel && names.indexOf("Custom model...") < 0)
            names.push("Custom model...")
        return names
    }

    // The names themselves, under the picker, because a count alone does not
    // tell you whether the model you wanted is in there.
    readonly property string modelsSummary: {
        var names = root.discoveredModels || []
        if (names.length === 0)
            return ""
        var extra = names.length - 8
        return names.length + (names.length === 1 ? " model on " : " models on ")
                + root.title + ": " + names.slice(0, 8).join(", ")
                + (extra > 0 ? " +" + extra + " more" : "")
    }

    signal activateRequested()
    signal modelEdited(string value)
    signal apiKeysEdited(string value)
    signal baseUrlEdited(string value)
    signal testRequested(string model, string apiKeys, string baseUrl)
    signal modelsRequested(string model, string apiKeys, string baseUrl)

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
        var choices = root.modelChoices
        var index = choices.indexOf(modelValue)
        if (index < 0 && allowCustomModel)
            index = choices.indexOf("Custom model...")
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
    // modelChoices covers both models and discoveredModels, so it is the only
    // handler needed - and reading it from onModelsChanged would read the list
    // from before the change.
    onModelChoicesChanged: syncModel()
    onApiKeysChanged: syncKeys()
    onApiKeysValueChanged: apiKeys = parseKeys(apiKeysValue)
    onActiveChanged: if (active) expanded = true
    Component.onCompleted: { syncModel(); apiKeys = parseKeys(apiKeysValue) }

    ListModel { id: apiKeysField }

    // Both action buttons share one look, declared once so the hover and
    // disabled states cannot drift apart between them.
    component ActionButton: Button {
        implicitHeight: 30
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
                    ToolTip.text: root.expanded ? qsTr("Collapse") : qsTr("Expand")
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

            Text { text: qsTr("Model"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            SettingsComboBox {
                id: modelSelector
                Layout.fillWidth: true
                model: root.modelChoices
                onActivated: if (currentText !== "Custom model...") root.modelEdited(currentText)
            }
            Text {
                Layout.fillWidth: true
                visible: root.modelsSummary !== ""
                text: root.modelsSummary
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
            }
            TextField {
                id: customModelField
                visible: root.allowCustomModel && modelSelector.currentText === "Custom model..."
                Layout.fillWidth: true
                implicitHeight: 32
                placeholderText: qsTr("Custom model ID")
                placeholderTextColor: Theme.placeholderText
                text: root.modelValue
                color: Theme.textPrimary
                selectByMouse: true
                onEditingFinished: root.modelEdited(text.trim())
                background: Rectangle { color: Theme.bgPanel; border.color: parent.activeFocus ? Theme.accent : Theme.border; radius: Theme.radiusSm }
            }

            RowLayout {
                visible: root.showBaseUrl
                Layout.fillWidth: true
                spacing: 6
                Text { text: qsTr("Base URL"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
                InfoTip {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Any OpenAI-compatible endpoint works here. The scheme may be left off, and ")
                          + qsTr("Check models renames this section after the site it reaches.")
                }
                Item { Layout.fillWidth: true }
            }
            TextField {
                id: baseUrlField
                visible: root.showBaseUrl
                Layout.fillWidth: true
                implicitHeight: 32
                text: root.baseUrlValue
                placeholderText: qsTr("https://your-provider.com/v1")
                placeholderTextColor: Theme.placeholderText
                color: Theme.textPrimary
                selectByMouse: true
                onEditingFinished: root.baseUrlEdited(text.trim())
                background: Rectangle { color: Theme.bgPanel; border.color: parent.activeFocus ? Theme.accent : Theme.border; radius: Theme.radiusSm }
            }

            Text { text: qsTr("API keys"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
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
                            placeholderText: index === 0 ? qsTr("API key") : qsTr("Backup API key")
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
                                ToolTip.text: revealed ? qsTr("Hide API key") : qsTr("Show API key")
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
                            ToolTip.text: qsTr("Remove API key")
                        }
                    }
                }
            }
            Button {
                implicitWidth: 90
                implicitHeight: 30
                text: qsTr("Add API")
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
                ActionButton {
                    implicitWidth: 140
                    text: root.checkingModels ? qsTr("Checking...") : qsTr("Check models")
                    enabled: !root.checkingModels
                    onClicked: root.modelsRequested(root.selectedModel(),
                                                    root.collectKeys().join("\n"),
                                                    baseUrlField.text.trim())
                }
                ActionButton {
                    implicitWidth: 120
                    text: root.busy ? qsTr("Testing...") : qsTr("Test connection")
                    enabled: !root.busy
                    onClicked: root.testRequested(root.selectedModel(), root.collectKeys().join("\n"), baseUrlField.text.trim())
                }
                Text {
                    Layout.fillWidth: true
                    text: root.modelsStatus !== "" ? root.modelsStatus : root.statusText
                    color: text.toLowerCase().indexOf("fail") >= 0 || text.toLowerCase().indexOf("could not") >= 0
                           ? Theme.danger : Theme.textMuted
                    font.pixelSize: Theme.fsXs
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
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
