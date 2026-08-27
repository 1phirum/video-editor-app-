//qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

Menu {
    id: root

    property string subtitleId: ""
    signal settingsRequested(string subtitleId)

    implicitWidth: 220

    delegate: DarkMenuItem {}

    background: Rectangle {
        color: Theme.bgPanel
        border.color: Theme.border
        radius: Theme.radiusSm
    }

    MenuItem {
        text: "Subtitle settings..."
        onTriggered: root.settingsRequested(root.subtitleId)
    }
}
