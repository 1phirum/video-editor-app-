pragma ComponentBehavior: Bound
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

Dialog {
    id: root
    title: "SRT Connection Settings"
    modal: true
    width: 460
    padding: 0
    standardButtons: Dialog.Ok | Dialog.Cancel

    property var settings: Backend.colorSettings
    property string transmitMode: settings.transmitMode || "Listener"

    onOpened: {
        settings = Backend.colorSettings
        transmitMode = settings.transmitMode || "Listener"
        streamId.text = settings.transmitStreamId || ""
        address.text = settings.transmitAddress || ""
        port.value = settings.transmitPort || 4201
        passphrase.text = settings.transmitPassphrase || ""
        latency.value = settings.transmitLatencyMs || 0
        quality.currentIndex = Math.max(0, ["Low", "Medium", "High"].indexOf(settings.transmitQuality || "Medium"))
    }

    onAccepted: {
        Backend.setColorSetting("transmitMode", transmitMode)
        Backend.setColorSetting("transmitStreamId", streamId.text)
        Backend.setColorSetting("transmitAddress", address.text)
        Backend.setColorSetting("transmitPort", port.value)
        Backend.setColorSetting("transmitPassphrase", passphrase.text)
        Backend.setColorSetting("transmitLatencyMs", latency.value)
        Backend.setColorSetting("transmitQuality", quality.currentText)
    }

    header: Rectangle {
        implicitHeight: 44
        color: Theme.bgSidebar
        Text { anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter; text: root.title; color: Theme.textPrimary; font.pixelSize: Theme.fsLg; font.weight: Font.DemiBold }
    }
    background: Rectangle { color: Theme.bgPanel; border.color: Theme.border; radius: Theme.radiusSm }
    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
        padding: 10
        alignment: Qt.AlignRight
        onAccepted: root.accept()
        onRejected: root.reject()
        background: Rectangle { color: Theme.bgSidebar }
        delegate: Button {
            id: footerButton
            implicitWidth: 72
            implicitHeight: 30
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            contentItem: Text { text: footerButton.text; color: Theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: Theme.fsSm }
            background: Rectangle { color: footerButton.hovered ? Theme.hover : Theme.bgPrimary; border.color: Theme.border; radius: Theme.radiusSm }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16

        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: ["Listener", "Rendezvous", "Caller"]
                delegate: Button {
                    id: modeButton
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    checkable: true
                    checked: root.transmitMode === modelData
                    implicitHeight: 30
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    onClicked: root.transmitMode = modelData
                    contentItem: Text { text: modeButton.text; color: modeButton.checked ? "white" : Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: Theme.fsSm }
                    background: Rectangle { color: modeButton.checked ? Theme.accent : (modeButton.hovered ? Theme.hover : Theme.bgPrimary); border.color: Theme.border; radius: Theme.radiusSm }
                }
            }
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 9
            Layout.fillWidth: true
            Text { text: "Stream ID"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            TextField { id: streamId; Layout.fillWidth: true; placeholderTextColor: Theme.placeholderText }
            Text { text: "Address"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            TextField { id: address; Layout.fillWidth: true; placeholderText: "Host name or IP address"; placeholderTextColor: Theme.placeholderText }
            Text { text: "Port"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            SpinBox { id: port; from: 1; to: 65535; value: 4201; editable: true; Layout.fillWidth: true }
            Text { text: "Passphrase"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            TextField { id: passphrase; echoMode: TextInput.Password; Layout.fillWidth: true; placeholderTextColor: Theme.placeholderText }
            Text { text: "Latency"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            SpinBox { id: latency; from: 0; to: 10000; value: 0; editable: true; Layout.fillWidth: true }
            Text { text: "Quality"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            WhisperComboBox { id: quality; model: ["Low", "Medium", "High"]; Layout.fillWidth: true }
        }
    }
}
