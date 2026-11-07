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
    width: 320
    title: qsTr("Field Options")
    standardButtons: Dialog.NoButton

    property string clipId: ""
    property var targetIds: []

    function openFor(clip, ids) {
        clipId = clip ? String(clip.id) : ""
        targetIds = (ids && ids.length > 0) ? ids : (clipId ? [clipId] : [])
        var eff = clip && clip.effects ? clip.effects : null
        reverseCheck.checked = eff ? (eff.reverseFieldDominance === true) : false
        var mode = eff && eff.fieldMode ? String(eff.fieldMode) : "none"
        noneRadio.checked = mode === "none"
        deintRadio.checked = mode === "deinterlace"
        flickerRadio.checked = mode === "flicker"
        if (!noneRadio.checked && !deintRadio.checked && !flickerRadio.checked)
            noneRadio.checked = true
        open()
    }

    background: Rectangle {
        color: Theme.bgSidebar
        border.width: 1
        border.color: Theme.border
        radius: Theme.radiusMd
    }

    contentItem: ColumnLayout {
        spacing: 10

        DarkCheckBox {
            id: reverseCheck
            text: qsTr("Reverse Field Dominance")
        }

        Label { text: qsTr("Field Processing"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }

        ButtonGroup { id: fieldGroup }
        DarkRadioButton {
            id: noneRadio
            checked: true
            ButtonGroup.group: fieldGroup
            text: qsTr("None")
        }
        DarkRadioButton {
            id: deintRadio
            ButtonGroup.group: fieldGroup
            text: qsTr("Always Deinterlace")
        }
        DarkRadioButton {
            id: flickerRadio
            ButtonGroup.group: fieldGroup
            text: qsTr("Flicker Removal")
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
                    var mode = deintRadio.checked ? "deinterlace"
                             : (flickerRadio.checked ? "flicker" : "none")
                    Backend.applyClipSelectionCommand(dialog.targetIds, "field",
                                                      {mode: mode, reverse: reverseCheck.checked})
                    dialog.close()
                }
            }
        }
    }
}
