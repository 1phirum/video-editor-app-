// qmllint disable
// Scene Edit Detection as a native OS modal window, matching Speed / Duration,
// Field Options and Remove Silences rather than an in-canvas Dialog overlay.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"

NativeModalWindow {
    id: dialog

    dialogTitle: qsTr("Scene Edit Detection")
    dialogWidth: 400
    // Grows when the progress row appears, so the native frame never clips it.
    dialogHeight: Math.round(content.implicitHeight + 32)

    property string clipId: ""
    property var targetIds: []

    function openFor(clip, ids) {
        clipId = clip ? String(clip.id) : ""
        targetIds = Backend.editableClipIds(ids && ids.length ? ids : [clipId], "movingVideo")
        openNative()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgSidebar

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 10

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Analyze %1 selected clip(s) and mark where the picture cuts to a new shot.").arg(dialog.targetIds.length)
            color: Theme.textSecondary
            font.pixelSize: Theme.fsSm
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Sensitivity"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            DarkSlider {
                id: threshSlider
                Layout.fillWidth: true
                from: 0.1; to: 0.9; value: 0.4; stepSize: 0.05
            }
            Label {
                text: threshSlider.value.toFixed(2)
                color: Theme.textPrimary
                font.pixelSize: Theme.fsSm
            }
        }

        // Without a floor on shot length a flashy or hand-held source detects
        // thousands of cuts and leaves the timeline full of slivers.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Shortest shot"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            DarkSlider {
                id: minShotSlider
                Layout.fillWidth: true
                from: 0; to: 5000; value: 1000; stepSize: 100
            }
            Label {
                text: minShotSlider.value < 50
                      ? qsTr("off")
                      : qsTr("%1.%2 s").arg(Math.floor(minShotSlider.value / 1000))
                                       .arg(Math.round((minShotSlider.value % 1000) / 100))
                color: Theme.textPrimary
                font.pixelSize: Theme.fsSm
            }
        }

        DarkCheckBox {
            id: applyCutsCheck
            checked: true
            text: qsTr("Apply a cut at each detected cut point")
        }
        DarkCheckBox {
            id: markersCheck
            text: qsTr("Create a clip marker at each detected cut point")
        }

        // The pass decodes the clip's whole used span. On a long source that is
        // minutes of work, so it reports how far it has got and can be stopped.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: Backend.sceneDetectInProgress

            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: Theme.bgPanel
                Rectangle {
                    width: parent.width * Backend.sceneDetectProgress
                    height: parent.height
                    radius: parent.radius
                    color: Theme.accent
                }
            }
            Label {
                text: qsTr("%1%").arg(Math.round(Backend.sceneDetectProgress * 100))
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
        }

        Label {
            Layout.fillWidth: true
            visible: Backend.sceneDetectStatus.length > 0
            wrapMode: Text.WordWrap
            text: Backend.sceneDetectStatus
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            DarkDialogButton { text: qsTr("Close"); flat: true; onClicked: dialog.close() }
            DarkDialogButton {
                text: qsTr("Stop")
                flat: true
                visible: Backend.sceneDetectInProgress
                onClicked: Backend.cancelSceneDetect()
            }
            DarkDialogButton {
                text: qsTr("Analyze")
                highlighted: true
                enabled: !Backend.sceneDetectInProgress
                         && (applyCutsCheck.checked || markersCheck.checked)
                onClicked: Backend.detectClipsSceneEdits(dialog.targetIds, threshSlider.value,
                                                    applyCutsCheck.checked,
                                                    markersCheck.checked,
                                                    minShotSlider.value)
            }
        }
    }
    }

    // Closing used to leave ffmpeg decoding the whole clip with nothing to show
    // for it and no way to stop it.
    onClosing: {
        if (Backend.sceneDetectInProgress)
            Backend.cancelSceneDetect()
    }
}
