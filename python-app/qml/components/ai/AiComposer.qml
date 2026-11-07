pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../common"

ColumnLayout {
    id: root
    required property var assistant
    signal submitted(string text)
    property alias draft: prompt.text
    spacing: 8

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 96
        color: Theme.bgPrimary
        radius: Theme.radiusMd
        border.color: prompt.activeFocus ? Theme.accent : Theme.border

        ScrollView {
            anchors.fill: parent
            anchors.margins: 10
            anchors.bottomMargin: 29
            TextArea {
                id: prompt
                objectName: "aiPrompt"
                placeholderText: qsTr("Ask AI to edit your video…")
                placeholderTextColor: Theme.textMuted
                color: Theme.textPrimary
                selectionColor: Theme.accent
                selectedTextColor: "white"
                font.pixelSize: Theme.fsMd
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                padding: 0
                background: null
                Accessible.name: qsTr("Message to AI assistant")
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                            && !(event.modifiers & Qt.ShiftModifier)) {
                        event.accepted = true
                        if (sendButton.enabled) root.submit()
                    }
                }
            }
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.bottom: parent.bottom; anchors.bottomMargin: 9
            text: root.assistant.contextLabel
            color: Theme.textMuted; font.pixelSize: Theme.fsXs
            width: parent.width - 56; elide: Text.ElideRight
        }
        IconButton {
            id: sendButton
            objectName: "aiSend"
            anchors.right: parent.right; anchors.rightMargin: 6
            anchors.bottom: parent.bottom; anchors.bottomMargin: 3
            iconName: "arrow-up-circle"
            restColor: Theme.accent
            enabled: root.assistant.ready && !root.assistant.busy
                     && !root.assistant.awaitingReview && prompt.text.trim().length > 0
            Accessible.name: qsTr("Send request")
            ToolTip.visible: hovered; ToolTip.text: qsTr("Send request")
            onClicked: root.submit()
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        DarkComboBox {
            id: modeBox
            objectName: "aiMode"
            Layout.preferredWidth: 99
            model: ["Ask", "Plan", "Edit"]
            currentIndex: model.indexOf(root.assistant.mode)
            enabled: !root.assistant.busy && (!root.assistant.awaitingReview || root.assistant.mode === "Plan")
            Accessible.name: qsTr("Assistant mode")
            onActivated: root.assistant.mode = model[index]
        }
        DarkComboBox {
            id: modelBox
            objectName: "aiModel"
            Layout.fillWidth: true
            model: root.assistant.models
            currentIndex: root.assistant.models.indexOf(root.assistant.model)
            displayText: root.assistant.model || qsTr("Choose pool model")
            enabled: !root.assistant.busy && !root.assistant.awaitingReview && count > 0
            Accessible.name: qsTr("Pool model")
            onActivated: root.assistant.model = root.assistant.models[index]
        }
    }
    Text {
        Layout.fillWidth: true
        text: root.assistant.mode === "Edit" ? qsTr("Edits are reviewed before applying. Enter to send.")
                : root.assistant.mode === "Ask" ? qsTr("Ask explains and inspects. Shift+Enter for a new line.")
                : qsTr("Plan proposes steps without changing the project.")
        color: Theme.textMuted; font.pixelSize: Theme.fsXs
        wrapMode: Text.WordWrap
    }
    function submit() {
        var text = prompt.text.trim()
        if (root.assistant.send(text)) {
            prompt.clear()
            root.submitted(text)
        }
    }
    function prefill(text) { prompt.text = text; prompt.forceActiveFocus() }
}
