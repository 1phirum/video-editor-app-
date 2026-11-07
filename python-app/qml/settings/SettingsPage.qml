//qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    property var settings: ({})
    signal settingChanged(string key, var value)
    default property alias pageData: content.data

    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
        id: content
        width: Math.max(0, root.availableWidth - 28)
        x: 14
        y: 14
        spacing: 20
    }
}
