// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"

NativeModalWindow {
    id: dialog
    dialogTitle: qsTr("Remove Noise")
    dialogWidth: 560
    dialogHeight: 360
    minimumWidth: 500
    minimumHeight: 340
    property string clipId: ""
    property string clipName: ""
    property var targetIds: []

    function openFor(clip, ids) {
        clipId = clip ? String(clip.id) : ""
        clipName = clip && clip.name ? String(clip.name) : qsTr("Selected clip")
        targetIds = Backend.editableClipIds(ids && ids.length ? ids : [clipId], "audio")
        strength.value = clip && clip.effects && clip.effects.noiseRemovalStrength !== undefined
                ? Number(clip.effects.noiseRemovalStrength) : 100
        openNative()
    }
    onClosing: Backend.cancelNoiseRemoval()

    Rectangle {
        anchors.fill: parent
        color: Theme.bgSidebar
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: dialog.targetIds.length > 1
                    ? qsTr("Clean speech in %1 selected clips").arg(dialog.targetIds.length)
                    : dialog.clipName
                elide: Text.ElideRight
                font.pixelSize: Theme.fsLg
                font.bold: true
                color: Theme.textPrimary
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Reduce background noise while keeping the speaker's voice. Compare a five-second sample before applying. Your original audio is preserved.")
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Noise removal"); color: Theme.textPrimary }
                DarkSlider {
                    id: strength
                    objectName: "noiseRemovalStrength"
                    Layout.fillWidth: true
                    from: 1
                    to: 100
                    stepSize: 1
                    value: 100
                    enabled: !Backend.noiseRemovalBusy
                    onMoved: Backend.stopNoiseRemovalPreview()
                }
                Label {
                    text: Math.round(strength.value) + "%"
                    Layout.preferredWidth: 40
                    color: Theme.textPrimary
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                DarkDialogButton {
                    objectName: "noiseOriginalPreview"
                    Layout.fillWidth: true
                    text: qsTr("Play Original")
                    enabled: dialog.clipId !== "" && !Backend.noiseRemovalBusy
                    onClicked: Backend.previewNoiseRemoval(dialog.clipId, Math.round(strength.value), true)
                }
                DarkDialogButton {
                    objectName: "noiseCleanedPreview"
                    Layout.fillWidth: true
                    text: qsTr("Play Cleaned")
                    enabled: dialog.clipId !== "" && !Backend.noiseRemovalBusy
                    onClicked: Backend.previewNoiseRemoval(dialog.clipId, Math.round(strength.value), false)
                }
                DarkDialogButton {
                    text: qsTr("Stop")
                    enabled: Backend.noiseRemovalPreview !== ""
                    onClicked: Backend.stopNoiseRemovalPreview()
                }
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: Backend.noiseRemovalBusy
                from: 0
                to: 1
                value: Backend.noiseRemovalProgress
            }
            Label {
                objectName: "noiseRemovalStatus"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: Backend.noiseRemovalStatus
                maximumLineCount: 3
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                DarkDialogButton {
                    text: qsTr("Restore Original")
                    enabled: !Backend.noiseRemovalBusy
                    onClicked: Backend.restoreClipsNoiseRemoval(dialog.targetIds)
                }
                Item { Layout.fillWidth: true }
                DarkDialogButton {
                    text: Backend.noiseRemovalBusy ? qsTr("Cancel") : qsTr("Close")
                    onClicked: {
                        if (Backend.noiseRemovalBusy)
                            Backend.cancelNoiseRemoval()
                        else
                            dialog.close()
                    }
                }
                DarkDialogButton {
                    objectName: "noiseRemovalApply"
                    text: qsTr("Apply")
                    highlighted: true
                    enabled: dialog.targetIds.length > 0 && !Backend.noiseRemovalBusy
                    onClicked: Backend.applyClipsNoiseRemoval(dialog.targetIds, Math.round(strength.value))
                }
            }
        }
    }
}
