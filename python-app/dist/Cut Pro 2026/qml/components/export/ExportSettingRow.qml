import QtQuick
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

RowLayout {
    id: root

    property string label: ""
    property string description: ""
    default property alias fieldData: fieldSlot.data

    Layout.fillWidth: true
    spacing: 12

    ColumnLayout {
        Layout.preferredWidth: 132
        Layout.minimumWidth: 112
        spacing: 2

        Text {
            text: root.label
            color: Theme.textSecondary
            font.pixelSize: Theme.fsSm
        }

        Text {
            visible: root.description !== ""
            text: root.description
            color: Theme.textMuted
            font.pixelSize: Theme.fsXs
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    RowLayout {
        id: fieldSlot
        Layout.fillWidth: true
        spacing: 6
    }
}
