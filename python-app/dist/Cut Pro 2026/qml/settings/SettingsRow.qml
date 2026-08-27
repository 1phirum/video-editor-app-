import QtQuick
import QtQuick.Layouts
import "../theme"

RowLayout {
    id: root
    property string label: ""
    default property alias controlData: controlSlot.data

    Layout.fillWidth: true
    Layout.minimumHeight: 34
    spacing: 14

    Text {
        Layout.preferredWidth: 190
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fsSm
        verticalAlignment: Text.AlignVCenter
    }

    RowLayout {
        id: controlSlot
        Layout.fillWidth: true
        spacing: 8
    }
}
