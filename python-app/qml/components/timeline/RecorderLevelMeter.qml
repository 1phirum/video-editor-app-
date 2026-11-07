import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"

ColumnLayout {
    id: meter
    objectName: "recorderLevelMeter"
    spacing: 10
    readonly property real floorDb: -60
    property real shownDb: floorDb
    property real peakDb: floorDb
    property bool clipped: false
    readonly property bool live: shownDb > floorDb + 0.5
    readonly property real fraction: Math.max(0, Math.min(1, (shownDb - floorDb) / -floorDb))

    function push(left, right) {
        var amplitude = Math.max(0, Number(left) || 0, Number(right) || 0)
        // This is the current input. Peak history belongs to the separate
        // reading below, never to a fall animation on the live bar.
        shownDb = amplitude > 0.000001
                ? Math.max(floorDb, Math.min(0, 20 * Math.log(amplitude) / Math.LN10))
                : floorDb
        peakDb = Math.max(peakDb, shownDb)
        clipped = amplitude >= 0.99
    }
    function reset() { shownDb = floorDb; resetPeak() }
    function resetPeak() { peakDb = floorDb; clipped = false }

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Input level"); color: Theme.textSecondary; font.pixelSize: Theme.fsLg }
        Item { Layout.fillWidth: true }
        Rectangle {
            width: 6; height: 6; radius: 3
            color: meter.clipped ? Theme.danger : meter.live ? Theme.meterGreen : Theme.textMuted
        }
        Label {
            text: meter.clipped ? qsTr("Too loud") : meter.live ? qsTr("Speaking") : qsTr("Silent")
            color: meter.clipped ? Theme.danger : Theme.textSecondary
            font.pixelSize: Theme.fsSm
        }
    }
    Label {
        text: meter.live ? qsTr("%1 dB").arg(meter.shownDb.toFixed(1)) : qsTr("−∞ dB")
        color: Theme.textPrimary; font.pixelSize: 28; font.bold: true
    }
    Row {
        Layout.fillWidth: true
        Layout.preferredHeight: 12
        spacing: 3
        Repeater {
            model: 36
            Rectangle {
                required property int index
                width: Math.max(1, (parent.width - 35 * parent.spacing) / 36)
                height: 12; radius: 2
                color: index / 36 < meter.fraction && meter.live
                       ? (index >= 33 ? Theme.meterRed : index >= 26 ? Theme.meterYellow : Theme.meterGreen)
                       : Theme.bgCardHeader
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Repeater {
            model: ["−60", "−36", "−24", "−12", "0"]
            Label {
                required property string modelData
                required property int index
                text: modelData; color: Theme.textMuted; font.pixelSize: Theme.fsXs
                Layout.fillWidth: true
                horizontalAlignment: index === 4 ? Text.AlignRight : Text.AlignLeft
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Peak   %1").arg(meter.peakDb > meter.floorDb ? meter.peakDb.toFixed(1) + " dB" : "—")
            color: Theme.textSecondary; font.pixelSize: Theme.fsSm
        }
        Item { Layout.fillWidth: true }
        Button {
            text: qsTr("Reset peak"); flat: true
            onClicked: meter.resetPeak()
            contentItem: Text { text: parent.text; color: Theme.accent; font.pixelSize: Theme.fsSm }
            background: Item {}
        }
    }
}
