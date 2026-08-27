// qmllint disable

import QtQuick
import QtQuick.Layouts
import "../theme"

ColumnLayout {
    id: root
    property string title: ""
    default property alias sectionData: body.data

    Layout.fillWidth: true
    spacing: 8

    Text {
        text: root.title
        color: Theme.textPrimary
        font.pixelSize: Theme.fsMd
        font.weight: Font.DemiBold
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
