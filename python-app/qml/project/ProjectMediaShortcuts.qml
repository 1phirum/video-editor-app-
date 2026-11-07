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
        sequence: (Backend.appSettings, Backend.shortcut("media.selectAll"))
        context: Qt.WindowShortcut
        enabled: root.shortcutsEnabled && root.focusTarget
                 && root.focusTarget.activeFocus
        onActivated: root.selectAllRequested()
    }

    Shortcut {
        sequence: (Backend.appSettings, Backend.shortcut("media.delete"))
        context: Qt.WindowShortcut
        enabled: root.shortcutsEnabled && root.focusTarget
                 && root.focusTarget.activeFocus
        onActivated: root.deleteRequested()
    }

    Shortcut {
        sequence: (Backend.appSettings, Backend.shortcut("media.rename"))
        context: Qt.WindowShortcut
        enabled: root.shortcutsEnabled && root.focusTarget
                 && root.focusTarget.activeFocus
        onActivated: root.renameRequested()
    }

    Shortcut {
        sequence: (Backend.appSettings, Backend.shortcut("media.paste"))
        context: Qt.WindowShortcut
        enabled: root.shortcutsEnabled && root.focusTarget
                 && root.focusTarget.activeFocus
        onActivated: root.pasteRequested()
    }
}
