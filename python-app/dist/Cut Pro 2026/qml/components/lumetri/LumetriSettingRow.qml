import QtQuick
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

RowLayout {
    id: root
    property string label: ""
    property string description: ""
    default property alias fieldData: field.data

    Layout.fillWidth: true
    Layout.minimumHeight: 30
    spacing: 12

    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 110
        Layout.preferredWidth: 150
        spacing: 2
        Text { text: root.label; color: Theme.textSecondary; font.pixelSize: Theme.fsSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Text { visible: root.description !== ""; text: root.description; color: Theme.textMuted; font.pixelSize: Theme.fsXs; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    RowLayout {
        id: field
        Layout.minimumWidth: 120
        Layout.preferredWidth: 156
        Layout.maximumWidth: 190
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        spacing: 5
    }
}
