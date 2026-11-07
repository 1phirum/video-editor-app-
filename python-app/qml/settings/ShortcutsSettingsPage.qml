//qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"
import "../components/common"

// The Shortcuts settings page. Rows are sourced from Backend.shortcutActions()
// (the canonical action table + resolved bindings), re-read whenever
// Backend.appSettings changes so a rebind or Reset shows immediately - the
// dialog live-applies "keyboardShortcuts" on every change, so the backend is
// always in step with the draft. Editing writes the whole override map back
// through settingChanged("keyboardShortcuts", ...), which rides the normal
// OK/Cancel flow.
SettingsPage {
    id: root

    readonly property var actionRows: (Backend.appSettings, Backend.shortcutActions())
    readonly property var categoryOrder: {
        let seen = []
        for (let i = 0; i < root.actionRows.length; i++) {
            const c = root.actionRows[i].category
            if (seen.indexOf(c) < 0)
                seen.push(c)
        }
        return seen
    }

    function rowsFor(category) {
        let out = []
        for (let i = 0; i < root.actionRows.length; i++)
            if (root.actionRows[i].category === category)
                out.push(root.actionRows[i])
        return out
    }

    function currentOverrides() {
        const m = root.settings && root.settings.keyboardShortcuts
        return m ? JSON.parse(JSON.stringify(m)) : ({})
    }

    function assignShortcut(id, seq) {
        const m = root.currentOverrides()
        m[id] = seq
        root.settingChanged("keyboardShortcuts", m)
    }

    function clearOverride(id) {
        const m = root.currentOverrides()
        if (m[id] !== undefined) {
            delete m[id]
            root.settingChanged("keyboardShortcuts", m)
        }
    }

    // A click-to-capture key field. While capturing it swallows ShortcutOverride
    // so the window's global Shortcuts (F11, Ctrl+, ...) can't steal the keys the
    // user is trying to bind; Escape cancels, a bare modifier keeps waiting.
    component KeyCaptureField : Rectangle {
        id: field
        property string sequenceText: ""
        property bool capturing: false
        signal captured(string seq)

        implicitWidth: 150
        implicitHeight: 30
        radius: Theme.radiusSm
        color: field.capturing ? Theme.bgCardHeader : Theme.bgPrimary
        border.color: field.capturing || hover.hovered ? Theme.accent : Theme.border
        activeFocusOnTab: true

        Text {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            text: field.capturing ? qsTr("Press keys…")
                                   : (field.sequenceText || qsTr("Unassigned"))
            color: field.capturing ? Theme.accent : Theme.textPrimary
            font.pixelSize: Theme.fsSm
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: {
                field.capturing = true
                field.forceActiveFocus()
            }
        }

        onActiveFocusChanged: if (!activeFocus) field.capturing = false

        Keys.onShortcutOverride: event => { if (field.capturing) event.accepted = true }
        Keys.onPressed: event => {
            if (!field.capturing)
                return
            if (event.key === Qt.Key_Escape) {
                field.capturing = false
                event.accepted = true
                return
            }
            const seq = Backend.sequenceFromEvent(event.key, event.modifiers)
            event.accepted = true
            if (seq.length === 0)
                return
            field.capturing = false
            field.captured(seq)
        }
    }

    SettingsSection {
        title: qsTr("Keyboard Shortcuts")
        help: qsTr("Click a shortcut, then press the keys you want. A key already used by another command in the same place is rejected with a warning. Changes take effect immediately and are kept when you click OK.")
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 2
            Text {
                Layout.fillWidth: true
                text: qsTr("Custom shortcuts persist across restarts.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
                wrapMode: Text.WordWrap
            }
            Button {
                id: resetAllButton
                text: qsTr("Reset all")
                implicitWidth: 96
                implicitHeight: 30
                hoverEnabled: true
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                onClicked: root.settingChanged("keyboardShortcuts", ({}))
                contentItem: Text {
                    text: resetAllButton.text
                    color: resetAllButton.hovered ? Theme.accent : Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: resetAllButton.down ? Theme.hover
                           : resetAllButton.hovered ? Theme.bgCardHeader
                           : Theme.bgPrimary
                    border.color: resetAllButton.hovered ? Theme.accent : Theme.border
                    radius: Theme.radiusSm
                }
            }
        }
    }

    Repeater {
        model: root.categoryOrder
        delegate: SettingsSection {
            required property string modelData
            title: modelData
            Repeater {
                model: root.rowsFor(modelData)
                delegate: SettingsRow {
                    id: shortcutRow
                    required property var modelData
                    label: modelData.label

                    KeyCaptureField {
                        sequenceText: shortcutRow.modelData.sequence
                        onCaptured: seq => {
                            const conflict = Backend.shortcutConflict(shortcutRow.modelData.id, seq)
                            if (conflict.length > 0) {
                                conflictLabel.text = qsTr("Already used by \"%1\"").arg(conflict)
                                conflictLabel.visible = true
                            } else {
                                conflictLabel.visible = false
                                root.assignShortcut(shortcutRow.modelData.id, seq)
                            }
                        }
                    }
                    Text {
                        id: conflictLabel
                        Layout.fillWidth: true
                        visible: false
                        color: Theme.danger
                        font.pixelSize: Theme.fsSm
                        elide: Text.ElideRight
                    }
                    Item { Layout.fillWidth: true; visible: !conflictLabel.visible }
                    Button {
                        id: resetButton
                        text: qsTr("Reset")
                        enabled: shortcutRow.modelData.isCustom
                        opacity: enabled ? 1 : 0.4
                        implicitWidth: 72
                        implicitHeight: 30
                        hoverEnabled: true
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        onClicked: {
                            conflictLabel.visible = false
                            root.clearOverride(shortcutRow.modelData.id)
                        }
                        contentItem: Text {
                            text: resetButton.text
                            color: resetButton.hovered ? Theme.accent : Theme.textPrimary
                            font.pixelSize: Theme.fsSm
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: resetButton.down ? Theme.hover
                                   : resetButton.hovered ? Theme.bgCardHeader
                                   : Theme.bgPrimary
                            border.color: resetButton.hovered ? Theme.accent : Theme.border
                            radius: Theme.radiusSm
                        }
                    }
                }
            }
        }
    }
}
