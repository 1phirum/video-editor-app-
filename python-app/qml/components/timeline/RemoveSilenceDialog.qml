// qmllint disable
// Remove Silences as a native OS modal window, matching Speed / Duration and
// Field Options rather than an in-canvas Dialog overlay.
//
// It is a two-step review instead of one destructive click. This used to fire
// straight off the context menu with hardcoded thresholds and no preview: the
// clip was cut the instant you released the mouse, and if the threshold was
// wrong for the material your only recourse was Ctrl+Z. The scan is now a
// separate step whose result you can see - how many stretches, how much time
// they add up to, where they are - before anything is removed.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"

NativeModalWindow {
    id: dialog

    dialogTitle: qsTr("Remove Silences")
    dialogWidth: 440
    // Grows when the result panel appears, so a native window with a fixed
    // frame does not clip its own content.
    dialogHeight: Math.round(content.implicitHeight + 32)

    property string clipId: ""
    property var targetIds: []
    // The clip's own length, so "time removed" can be shown against something.
    property real clipDurationMs: 0
    // Candidate positions are absolute timeline times; the map below is drawn
    // relative to the clip's own start.
    property real startOffsetMs: 0

    readonly property int candidateCount: Backend.silenceCandidates.length
    readonly property real candidateTotalMs:
        candidateCount > 0 ? Backend.silenceCandidateTotalMs() : 0

    function openFor(clip, ids) {
        clipId = clip ? String(clip.id) : ""
        targetIds = Backend.editableClipIds(ids && ids.length ? ids : [clipId], "audio")
        clipDurationMs = targetIds.reduce(function(total, id) {
            return total + Number(Backend.clipById(String(id)).durationMs || 0)
        }, 0)
        startOffsetMs = 0
        Backend.clearSilenceCandidates()
        openNative()
    }

    // Closing mid-scan used to leave the decode running with nothing to show
    // for it, so the window takes the scan down with it.
    onClosing: {
        if (Backend.silenceScanInProgress)
            Backend.cancelSilenceScan()
        Backend.clearSilenceCandidates()
    }

    function formatDuration(ms) {
        var total = Math.max(0, Math.round(Number(ms) / 1000))
        var m = Math.floor(total / 60)
        var s = total % 60
        return m > 0 ? qsTr("%1m %2s").arg(m).arg(s) : qsTr("%1s").arg(s)
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
                text: qsTr("Find quiet stretches in %1 selected clip(s). Scan first and check the combined result before removing anything.").arg(dialog.targetIds.length)
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 8
                rowSpacing: 8

                Label {
                    text: qsTr("Silence below")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                }
                DarkSlider {
                    id: thresholdSlider
                    Layout.fillWidth: true
                    from: -70; to: -10; value: -32; stepSize: 1
                }
                Label {
                    Layout.preferredWidth: 72
                    text: qsTr("%1 dB").arg(thresholdSlider.value.toFixed(0))
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                }

                Label {
                    text: qsTr("Shorter than")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                }
                DarkSlider {
                    id: minSilenceSlider
                    Layout.fillWidth: true
                    from: 100; to: 3000; value: 500; stepSize: 50
                }
                Label {
                    Layout.preferredWidth: 72
                    text: qsTr("keep %1 ms").arg(minSilenceSlider.value.toFixed(0))
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                }

                Label {
                    text: qsTr("Leave a breath")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                }
                DarkSlider {
                    id: paddingSlider
                    Layout.fillWidth: true
                    from: 0; to: 500; value: 100; stepSize: 10
                }
                Label {
                    Layout.preferredWidth: 72
                    text: qsTr("%1 ms").arg(paddingSlider.value.toFixed(0))
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                }
            }

            DarkCheckBox {
                id: averageCheck
                checked: true
                text: qsTr("Measure average level (better for breaths and room tone)")
            }

            // Scan progress. A multi-hour clip is a real wait, so it reports a
            // fraction and can be stopped.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: Backend.silenceScanInProgress

                Rectangle {
                    Layout.fillWidth: true
                    height: 4
                    radius: 2
                    color: Theme.bgPanel
                    Rectangle {
                        width: parent.width * Backend.silenceScanProgress
                        height: parent.height
                        radius: parent.radius
                        color: Theme.accent
                    }
                }
                Label {
                    text: qsTr("Scanning audio... %1%").arg(Math.round(Backend.silenceScanProgress * 100))
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                }
            }

            // What the scan found, before it is applied.
            Rectangle {
                Layout.fillWidth: true
                visible: !Backend.silenceScanInProgress && dialog.candidateCount > 0
                Layout.preferredHeight: visible ? resultColumn.implicitHeight + 16 : 0
                radius: Theme.radiusSm
                color: Theme.bgPanel
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: resultColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 6

                    Label {
                        text: qsTr("%1 silent stretches - %2 to remove")
                            .arg(dialog.candidateCount)
                            .arg(dialog.formatDuration(dialog.candidateTotalMs))
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsSm
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        visible: dialog.clipDurationMs > 0
                        text: qsTr("Selected duration goes from %1 to %2.")
                            .arg(dialog.formatDuration(dialog.clipDurationMs))
                            .arg(dialog.formatDuration(dialog.clipDurationMs - dialog.candidateTotalMs))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fsSm
                    }

                    // A map of where the cuts land, so an obviously wrong
                    // threshold is visible as a solid block rather than
                    // discovered after the fact.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 18
                        radius: 2
                        color: Theme.bgSidebar
                        border.width: 1
                        border.color: Theme.border
                        clip: true

                        Repeater {
                            model: Backend.silenceCandidates
                            Rectangle {
                                required property var modelData
                                visible: dialog.clipDurationMs > 0
                                color: Theme.accent
                                opacity: 0.75
                                y: 0
                                height: parent.height
                                x: dialog.clipDurationMs > 0
                                   ? parent.width * Number(modelData.reviewStartMs || 0) / dialog.clipDurationMs
                                   : 0
                                width: Math.max(1, dialog.clipDurationMs > 0
                                       ? parent.width * Number(modelData.durationMs) / dialog.clipDurationMs
                                       : 0)
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: !Backend.silenceScanInProgress
                         && dialog.candidateCount === 0
                         && Backend.silenceScanStatus.length > 0
                text: Backend.silenceScanStatus
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 8
                Item { Layout.fillWidth: true }
                DarkDialogButton {
                    text: qsTr("Close")
                    flat: true
                    onClicked: dialog.close()
                }
                DarkDialogButton {
                    text: qsTr("Stop")
                    flat: true
                    visible: Backend.silenceScanInProgress
                    onClicked: Backend.cancelSilenceScan()
                }
                DarkDialogButton {
                    text: dialog.candidateCount > 0 ? qsTr("Scan again") : qsTr("Scan")
                    visible: !Backend.silenceScanInProgress
                    onClicked: Backend.analyzeClipsSilence(dialog.targetIds,
                                                      thresholdSlider.value,
                                                      minSilenceSlider.value,
                                                      paddingSlider.value,
                                                      averageCheck.checked)
                }
                DarkDialogButton {
                    text: qsTr("Remove")
                    highlighted: true
                    visible: !Backend.silenceScanInProgress
                    enabled: dialog.candidateCount > 0
                    onClicked: {
                        if (Backend.applyClipsSilenceRemoval() > 0)
                            dialog.close()
                    }
                }
            }
        }
    }
}
