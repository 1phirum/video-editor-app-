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
        color: Theme.bgSidebar
        border.color: Theme.border
        radius: Theme.radiusMd
    }

    DarkMenuItem {
        text: root.selectedCount > 1 ? "Add selected to timeline" : "Add to timeline"
        enabled: root.canAdd
        onTriggered: root.addToTimelineRequested()
    }

    MenuSeparator {}

    DarkMenuItem {
        text: "Rename…"
        enabled: root.hasSingleSelection
        onTriggered: root.renameRequested()
    }
    DarkMenuItem {
        text: "Open in default app"
        enabled: root.hasSingleSelection
        onTriggered: root.openRequested()
    }
    DarkMenuItem {
        text: "Reveal in File Explorer"
        enabled: root.hasSingleSelection
        onTriggered: root.revealRequested()
    }
    DarkMenuItem {
        text: "Copy file path"
        enabled: root.hasSingleSelection
        onTriggered: root.copyPathRequested()
    }

    MenuSeparator {}

    DarkMenuItem {
        text: "Select all"
        onTriggered: root.selectAllRequested()
    }
    DarkMenuItem {
        text: "Import media…"
        onTriggered: root.importRequested()
    }

    MenuSeparator {}

    DarkMenuItem {
        text: root.selectedCount > 1 ? "Delete selected media…" : "Delete media…"
        enabled: root.selectedCount > 0
        onTriggered: root.deleteRequested()
    }
}
