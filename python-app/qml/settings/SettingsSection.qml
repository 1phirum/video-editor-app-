// qmllint disable

import QtQuick
import QtQuick.Layouts
import "../theme"
import "../components/common"

ColumnLayout {
    id: root
    property string title: ""
    // When set, a small ⓘ appears after the title and reveals this text on
    // hover - the home for a section's "how it works" prose that used to sit as
    // an always-visible paragraph under the controls.
    property string help: ""
    default property alias sectionData: body.data

    Layout.fillWidth: true
    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Text {
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fsMd
            font.weight: Font.DemiBold
        }
        InfoTip { text: root.help; Layout.alignment: Qt.AlignVCenter }
        Item { Layout.fillWidth: true }
    }

    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: Theme.border
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: 4
    }
}
