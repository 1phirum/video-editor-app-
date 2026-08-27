//qmllint disable
import QtQuick

Item {
    id: root

    required property Item focusTarget
    property bool shortcutsEnabled: true
    signal selectAllRequested()
    signal pasteRequested()
    signal deleteRequested()
    signal renameRequested()

    Shortcut {
        sequence: "Ctrl+A"
        context: Qt.WindowShortcut
        enabled: root.shortcutsEnabled && root.focusTarget
                 && root.focusTarget.activeFocus
        onActivated: root.selectAllRequested()
    }

    Shortcut {
        sequence: "Delete"
        context: Qt.WindowShortcut
        enabled: root.shortcutsEnabled && root.focusTarget
                 && root.focusTarget.activeFocus
        onActivated: root.deleteRequested()
    }

    Shortcut {
        sequence: "F2"
        context: Qt.WindowShortcut
        enabled: root.shortcutsEnabled && root.focusTarget
                 && root.focusTarget.activeFocus
        onActivated: root.renameRequested()
    }

    Shortcut {
        sequence: "Ctrl+V"
        context: Qt.WindowShortcut
        enabled: root.shortcutsEnabled && root.focusTarget
                 && root.focusTarget.activeFocus
        onActivated: root.pasteRequested()
    }
}
