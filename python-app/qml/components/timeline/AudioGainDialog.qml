// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"

Dialog {
    id: dialog
    modal: true
    width: 300
    title: qsTr("Audio Gain")
    standardButtons: Dialog.NoButton

    property string clipId: ""
    property var targetIds: []

    function openFor(clip, ids) {
        clipId = clip ? String(clip.id) : ""
        targetIds = (ids && ids.length > 0) ? ids : (clipId ? [clipId] : [])
        var eff = clip && clip.effects ? clip.effects : null
        var db = eff && eff.volumeDb !== undefined ? Number(eff.volumeDb) : 0
        gainField.text = db.toFixed(1)
        open()
        gainField.forceActiveFocus()
        gainField.selectAll()
    }

    readonly property real gainDb: {
        var v = Number(gainField.text)
        return isNaN(v) ? 0 : Math.max(-60, Math.min(12, v))
    }

    background: Rectangle {
        color: Theme.bgSidebar
        border.width: 1
        border.color: Theme.border
        radius: Theme.radiusMd
    }

    contentItem: ColumnLayout {
        spacing: 10

        Label { text: qsTr("Set Gain (dB)"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
        TextField {
            id: gainField
            Layout.fillWidth: true
            color: Theme.textPrimary
            validator: DoubleValidator { bottom: -60; top: 12; decimals: 1 }
            background: Rectangle { color: Theme.bgPrimary; border.color: Theme.border; radius: Theme.radiusSm }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            DarkDialogButton { text: qsTr("Cancel"); flat: true; onClicked: dialog.close() }
            DarkDialogButton {
                text: qsTr("Apply")
                highlighted: true
                onClicked: {
                    Backend.applyClipSelectionCommand(dialog.targetIds, "setting",
                                                      {key: "volumeDb", value: dialog.gainDb})
                    dialog.close()
                }
            }
        }
    }
}
