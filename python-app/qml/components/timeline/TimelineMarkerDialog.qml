// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"

Dialog {
    id: dialog
    modal: true
    width: 320
    title: markerId === "" ? "Add Marker" : "Edit Marker"
    standardButtons: Dialog.NoButton

    property string markerId: ""
    property int positionMs: 0
    property string markerName: "Marker"
    property string markerColor: "#59a7ff"
    signal saveRequested(string markerId, int positionMs, string name, string color)
    signal removeRequested(string markerId)

    function edit(marker) {
        markerId = marker ? String(marker.id) : ""
        positionMs = marker ? Number(marker.positionMs) : 0
        markerName = marker && marker.name ? String(marker.name) : "Marker"
        markerColor = marker && marker.color ? String(marker.color) : "#59a7ff"
        nameField.text = markerName
        timeField.text = (positionMs / 1000).toFixed(3)
        var index = colorBox.colorModel.indexOf(markerColor)
        colorBox.currentIndex = index >= 0 ? index : 0
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    background: Rectangle {
        color: Theme.bgSidebar
        border.width: 1
        border.color: Theme.border
        radius: Theme.radiusMd
    }

    contentItem: ColumnLayout {
        spacing: 10

        Label { text: "Name"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
        TextField {
            id: nameField
            Layout.fillWidth: true
            color: Theme.textPrimary
            placeholderText: "Marker"
            placeholderTextColor: Theme.placeholderText
            background: Rectangle { color: Theme.bgPrimary; border.color: Theme.border; radius: Theme.radiusSm }
        }

        Label { text: "Time (seconds)"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
        TextField {
            id: timeField
            Layout.fillWidth: true
            color: Theme.textPrimary
            validator: DoubleValidator { bottom: 0; decimals: 3 }
            background: Rectangle { color: Theme.bgPrimary; border.color: Theme.border; radius: Theme.radiusSm }
        }

        Label { text: "Color"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
        ComboBox {
            id: colorBox
            Layout.fillWidth: true
            property var colorModel: ["#59a7ff", "#65d46e", "#f4cf58", "#f78181", "#c58cff"]
            model: colorModel
            contentItem: RowLayout {
                spacing: 8
                Rectangle {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    radius: 2
                    color: colorBox.currentText
                }
                Text { text: colorBox.currentText; color: Theme.textPrimary; font.pixelSize: Theme.fsSm }
            }
            background: Rectangle { color: Theme.bgPrimary; border.color: Theme.border; radius: Theme.radiusSm }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                text: "Delete"
                visible: dialog.markerId !== ""
                flat: true
                onClicked: {
                    dialog.removeRequested(dialog.markerId)
                    dialog.close()
                }
            }
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; flat: true; onClicked: dialog.close() }
            Button {
                text: "Save"
                highlighted: true
                onClicked: {
                    var seconds = Number(timeField.text)
                    dialog.saveRequested(dialog.markerId,
                                         Math.max(0, Math.round(seconds * 1000)),
                                         nameField.text,
                                         colorBox.currentText)
                    dialog.close()
                }
            }
        }
    }
}
