// qmllint disable
import QtQuick
import QtQuick.Controls
import "../theme"
import "../components/common"

Menu {
    id: root

    required property int selectedCount
    required property bool canAdd
    property bool hasSingleSelection: selectedCount === 1

    signal addToTimelineRequested()
    signal renameRequested()
    signal openRequested()
    signal revealRequested()
    signal copyPathRequested()
    signal selectAllRequested()
    signal importRequested()
    signal deleteRequested()

    implicitWidth: 228

    background: Rectangle {
        color: Theme.menuBg
        border.color: Theme.menuBorder
        radius: Theme.radiusMd
    }

    DarkMenuItem {
        text: root.selectedCount > 1 ? qsTr("Add selected to timeline") : qsTr("Add to timeline")
        enabled: root.canAdd
        onTriggered: root.addToTimelineRequested()
    }

    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }

    DarkMenuItem {
        text: qsTr("Rename…")
        enabled: root.hasSingleSelection
        onTriggered: root.renameRequested()
    }
    DarkMenuItem {
        text: qsTr("Open in default app")
        enabled: root.hasSingleSelection
        onTriggered: root.openRequested()
    }
    DarkMenuItem {
        text: qsTr("Reveal in File Explorer")
        enabled: root.hasSingleSelection
        onTriggered: root.revealRequested()
    }
    DarkMenuItem {
        text: qsTr("Copy file path")
        enabled: root.hasSingleSelection
        onTriggered: root.copyPathRequested()
    }

    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }

    DarkMenuItem {
        text: qsTr("Select all")
        onTriggered: root.selectAllRequested()
    }
    DarkMenuItem {
        text: qsTr("Import media…")
        onTriggered: root.importRequested()
    }

    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }

    DarkMenuItem {
        text: root.selectedCount > 1 ? qsTr("Delete selected media…") : qsTr("Delete media…")
        enabled: root.selectedCount > 0
        onTriggered: root.deleteRequested()
    }
}
