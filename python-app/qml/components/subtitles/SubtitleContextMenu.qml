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
    // Set from the caller right before popup() so Ungroup can reflect group
    // state; a Q_INVOKABLE lookup here would go stale on this long-lived Menu.
    property var clipData: ({})
    readonly property bool hasGroup: Boolean(clipData && clipData.groupId)

    signal settingsRequested(string subtitleId)
    signal deleteRequested(string subtitleId, bool ripple)
    signal groupRequested(string subtitleId)
    signal ungroupRequested(string subtitleId)

    width: 220
    padding: 4

    background: Rectangle {
        color: Theme.menuBg
        border.color: Theme.menuBorder
        radius: Theme.radiusSm
    }

    // DarkMenuItem, not MenuItem behind Menu's `delegate`: see the note in
    // TranscriptContextMenu. A one-row menu shows the fault every single time,
    // because that row is the current one the moment the menu opens.
    DarkMenuItem {
        text: qsTr("Subtitle settings...")
        onTriggered: root.settingsRequested(root.subtitleId)
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        text: qsTr("Delete Clip")
        onTriggered: root.deleteRequested(root.subtitleId, false)
    }
    DarkMenuItem {
        text: qsTr("Ripple Delete")
        onTriggered: root.deleteRequested(root.subtitleId, true)
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        text: qsTr("Group")
        onTriggered: root.groupRequested(root.subtitleId)
    }
    DarkMenuItem {
        text: qsTr("Ungroup")
        enabled: root.hasGroup
        onTriggered: root.ungroupRequested(root.subtitleId)
    }
}
