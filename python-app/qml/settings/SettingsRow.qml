import QtQuick
import QtQuick.Layouts
import "../theme"
import "../components/common"

RowLayout {
    id: root
    property string label: ""
    // Optional per-row help, revealed by a ⓘ next to the label on hover.
    property string help: ""
    default property alias controlData: controlSlot.data

    Layout.fillWidth: true
    Layout.minimumHeight: 34
    spacing: 14

    RowLayout {
        // A plain fixed-width label column, left-aligned, with the ⓘ hugging the
        // label text. fillWidth is forced false: a Layout nested in a Layout
        // defaults it to true, which would let this cell steal space and push
        // the control to the far right.
        Layout.fillWidth: false
        Layout.preferredWidth: 190
        spacing: 5
        Text {
            text: root.label
            color: Theme.textSecondary
            font.pixelSize: Theme.fsSm
            verticalAlignment: Text.AlignVCenter
        }
        InfoTip { text: root.help; Layout.alignment: Qt.AlignVCenter }
        Item { Layout.fillWidth: true }
    }

    RowLayout {
        id: controlSlot
        Layout.fillWidth: true
        spacing: 8
    }
}
