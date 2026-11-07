// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"

// Full-window activation gate. Opaque, covers everything, and is shown whenever
// the app is not licensed (see main.qml). It shows this machine's Machine ID
// (which the user sends to the vendor) and a field to paste the returned key.
// Parented to Overlay.overlay at a very high z, so it intercepts all input to
// the app beneath it.
Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.bgPrimary

    property string errorText: ""

    // Swallow clicks/keys so nothing behind the gate reacts.
    MouseArea { anchors.fill: parent; hoverEnabled: true }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(520, root.width - 64)
        spacing: 18

        // Brand
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12
            Rectangle {
                width: 40; height: 40; radius: 9
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.accent }
                    GradientStop { position: 1.0; color: Qt.darker(Theme.accent, 1.6) }
                }
                Image {
                    anchors.centerIn: parent
                    source: "../../assets/icons/film.svg"
                    sourceSize.width: 20; sourceSize.height: 20
                }
            }
            Text {
                text: qsTr("Activate Cut Pro")
                color: Theme.textPrimary
                font.pixelSize: 22
                font.weight: Font.DemiBold
            }
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Send your Machine ID to your vendor to receive a license key, then paste the key below to activate. Each key works on one machine only.")
            color: Theme.textMuted
            font.pixelSize: Theme.fsMd
        }

        // Machine ID + copy
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                text: qsTr("YOUR MACHINE ID")
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
                font.letterSpacing: 1.2
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: Theme.radiusSm
                    color: Theme.bgPanel
                    border.color: Theme.outline
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        verticalAlignment: Text.AlignVCenter
                        text: Backend.machineId
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsLg
                        font.family: "monospace"
                        elide: Text.ElideRight
                    }
                }
                Button {
                    id: copyBtn
                    text: qsTr("Copy")
                    implicitWidth: 78
                    implicitHeight: 40
                    hoverEnabled: true
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    onClicked: {
                        machineIdClipboard.text = Backend.machineId
                        machineIdClipboard.selectAll()
                        machineIdClipboard.copy()
                        copyBtn.text = qsTr("Copied")
                        copyResetTimer.restart()
                    }
                    contentItem: Text {
                        text: copyBtn.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsMd
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: copyBtn.down ? Theme.hover
                               : copyBtn.hovered ? Theme.bgCardHeader : Theme.bgCard
                        border.color: Theme.outline
                    }
                }
            }
            // Hidden field used only as a clipboard shim (TextEdit.copy()).
            TextEdit {
                id: machineIdClipboard
                visible: false
                width: 0; height: 0
            }
            Timer {
                id: copyResetTimer
                interval: 1500
                onTriggered: copyBtn.text = qsTr("Copy")
            }
        }

        // Key entry
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                text: qsTr("LICENSE KEY")
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
                font.letterSpacing: 1.2
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 44
                radius: Theme.radiusSm
                color: Theme.bgPanel
                border.color: keyField.activeFocus ? Theme.accent : Theme.outline
                TextField {
                    id: keyField
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    background: null
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsLg
                    font.family: "monospace"
                    placeholderText: qsTr("CP1-XXXXXXXX-XXXXXXXX-...")
                    placeholderTextColor: Theme.textMuted
                    selectionColor: Theme.accent
                    onAccepted: root.tryActivate()
                    onTextChanged: root.errorText = ""
                }
            }
            Text {
                Layout.fillWidth: true
                visible: root.errorText.length > 0
                text: root.errorText
                color: Theme.danger
                font.pixelSize: Theme.fsSm
                wrapMode: Text.WordWrap
            }
        }

        // Activate
        Button {
            id: activateBtn
            Layout.fillWidth: true
            implicitHeight: 46
            enabled: keyField.text.trim().length > 0
            hoverEnabled: true
            HoverHandler { cursorShape: activateBtn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
            onClicked: root.tryActivate()
            contentItem: Text {
                text: qsTr("Activate")
                color: "#ffffff"
                font.pixelSize: Theme.fsLg
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusSm
                color: !activateBtn.enabled ? Qt.darker(Theme.accent, 2.2)
                       : activateBtn.down ? Qt.darker(Theme.accent, 1.2)
                       : activateBtn.hovered ? Qt.lighter(Theme.accent, 1.1)
                       : Theme.accent
            }
        }
    }

    function tryActivate() {
        if (keyField.text.trim().length === 0)
            return
        if (!Backend.activateLicense(keyField.text)) {
            root.errorText = Backend.lastError && Backend.lastError.length > 0
                    ? Backend.lastError
                    : qsTr("This license key is not valid for this machine.")
        }
        // On success Backend.licensed flips and main.qml hides this overlay.
    }
}
