// qmllint disable
// Clip Speed / Duration as a native OS modal window (matches ColorPickerDialog
// and the Settings dialog) rather than an in-canvas Dialog overlay. openFor()
// still takes the clip and the target id list; it now shows the window centered
// on its owner.
//
// Speed and Duration are two views of the same retime: editing either one drives
// the other (duration = sourceDuration * 100 / speed), so a clip can be set to a
// wall-clock length just as easily as to a percentage. Every control here is
// live - the interpolation mode, pitch and ripple all reach Backend.setClipSpeed.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"

NativeModalWindow {
    id: dialog

    dialogTitle: qsTr("Clip Speed / Duration")
    dialogWidth: 360
    dialogHeight: 372

    property string clipId: ""
    property var targetIds: []
    property real sourceDurationMs: 0
    // Set true while one field is written from the other so their own edit
    // handlers do not write back and fight the typing.
    property bool syncing: false

    function openFor(clip, ids) {
        clipId = clip ? String(clip.id) : ""
        targetIds = (ids && ids.length > 0) ? ids : (clipId ? [clipId] : [])
        var eff = clip && clip.effects ? clip.effects : null
        var speed = eff && eff.speed ? Number(eff.speed) : 100
        if (isNaN(speed) || speed <= 0)
            speed = 100
        // The span of source this clip actually uses, which is its timeline
        // length scaled by its current speed - not `sourceDurationMs`, which is
        // the whole media file and is never narrowed by a trim or a split.
        // Reading that made the dialog quote the full source length for every
        // trimmed clip, so a ten-second cut of a 26-hour recording showed as 26
        // hours before you touched anything. Backend::setClipSpeed now computes
        // from the used span too, so the number here is the number it applies.
        sourceDurationMs = clip
            ? Math.max(1, Number(clip.durationMs || 0) * speed / 100)
            : 0
        dialog.syncing = true
        speedField.text = speed.toFixed(2)
        durationField.text = dialog.durationForSpeed(speed).toFixed(3)
        dialog.syncing = false
        pitchCheck.checked = eff ? (eff.maintainPitch !== false) : true
        rippleCheck.checked = false
        var interp = eff && eff.timeInterpolation ? String(eff.timeInterpolation) : "sampling"
        interpBox.currentIndex = interp === "blending" ? 1 : 0
        openNative()
        speedField.forceActiveFocus()
        speedField.selectAll()
    }

    // The speed actually applied on Apply, clamped to the backend's range.
    readonly property real effectiveSpeed: {
        var v = Number(speedField.text)
        return (isNaN(v) || v <= 0) ? 100 : Math.max(1, Math.min(10000, v))
    }

    function durationForSpeed(speed) {
        return speed > 0 ? (sourceDurationMs * 100 / speed) / 1000 : sourceDurationMs / 1000
    }
    function speedForDurationSec(durationSec) {
        var ms = durationSec * 1000
        return ms > 0 ? sourceDurationMs * 100 / ms : 100
    }

    // Editing speed recomputes the duration field, and vice versa. The syncing
    // guard keeps the write from bouncing back.
    function onSpeedEdited() {
        if (dialog.syncing)
            return
        var v = Number(speedField.text)
        if (isNaN(v) || v <= 0)
            return
        dialog.syncing = true
        durationField.text = dialog.durationForSpeed(v).toFixed(3)
        dialog.syncing = false
    }
    function onDurationEdited() {
        if (dialog.syncing)
            return
        var sec = Number(durationField.text)
        if (isNaN(sec) || sec <= 0)
            return
        var speed = Math.max(1, Math.min(10000, dialog.speedForDurationSec(sec)))
        dialog.syncing = true
        speedField.text = speed.toFixed(2)
        dialog.syncing = false
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgSidebar

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Label { text: qsTr("Speed (%)"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            TextField {
                id: speedField
                Layout.fillWidth: true
                color: Theme.textPrimary
                selectByMouse: true
                validator: DoubleValidator { bottom: 1; top: 10000; decimals: 2 }
                background: Rectangle { color: Theme.bgPrimary; border.color: Theme.border; radius: Theme.radiusSm }
                onTextEdited: dialog.onSpeedEdited()
            }

            Label { text: qsTr("Duration (s)"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            TextField {
                id: durationField
                Layout.fillWidth: true
                color: Theme.textPrimary
                selectByMouse: true
                validator: DoubleValidator { bottom: 0.001; top: 1000000; decimals: 3 }
                background: Rectangle { color: Theme.bgPrimary; border.color: Theme.border; radius: Theme.radiusSm }
                onTextEdited: dialog.onDurationEdited()
            }

            Label { text: qsTr("Time Interpolation"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            DarkComboBox {
                id: interpBox
                Layout.fillWidth: true
                property var interpValues: ["sampling", "blending"]
                model: [qsTr("Frame Sampling"), qsTr("Frame Blending")]
            }

            DarkCheckBox {
                id: pitchCheck
                checked: true
                text: qsTr("Maintain audio pitch")
            }
            DarkCheckBox {
                id: rippleCheck
                text: qsTr("Ripple edit, shifting trailing clips")
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                DarkDialogButton { text: qsTr("Cancel"); flat: true; onClicked: dialog.close() }
                DarkDialogButton {
                    text: qsTr("Apply")
                    highlighted: true
                    onClicked: {
                        Backend.applyClipSelectionCommand(dialog.targetIds, "speed",
                            {speed: dialog.effectiveSpeed, maintainPitch: pitchCheck.checked,
                             interpolation: interpBox.interpValues[interpBox.currentIndex],
                             ripple: rippleCheck.checked})
                        dialog.close()
                    }
                }
            }
        }
    }
}
