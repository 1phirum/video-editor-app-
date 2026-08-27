//qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

RowLayout {
    id: root
    property string value: "normal"
    signal modeSelected(string value)
    Layout.fillWidth: true
    Layout.preferredHeight: 30
    spacing: 8
    Text { text: "Blend Mode"; color: Theme.textSecondary; font.pixelSize: Theme.fsXs; Layout.preferredWidth: 82 }
    ComboBox {
        id: combo
        Layout.fillWidth: true
        model: ["Normal", "Dissolve", "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color", "Lighten", "Screen", "Color Dodge", "Linear Dodge (Add)", "Lighter Color", "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Difference", "Exclusion", "Subtract", "Divide", "Hue", "Saturation", "Color", "Luminosity"]
        currentIndex: Math.max(0, model.indexOf(root.value === "normal" ? "Normal" : root.value))
        onActivated: root.modeSelected(String(model[currentIndex]).replace(/[^A-Za-z]/g, ""))
        contentItem: Text { leftPadding: 8; text: combo.displayText; color: Theme.textPrimary; font.pixelSize: Theme.fsXs; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { color: Theme.bgPrimary; border.color: Theme.border; radius: Theme.radiusSm }
    }
}
