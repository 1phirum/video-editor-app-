//qmllint disable
import QtQuick
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

LumetriSection {
    id: root
    required property var panel
    title: qsTr("HSL Secondary")
    checkable: true
    sectionEnabled: panel.clipValue("hslSecondaryEnabled", true)
    onSectionToggled: enabled => panel.setClip("hslSecondaryEnabled", enabled)
    expanded: false

    Text { text: qsTr("Key"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
    LumetriSlider { label: qsTr("Hue center"); from: 0; to: 360; value: root.panel.clipValue("hslHueCenter", 0); suffix: "deg"; onValueCommitted: value => root.panel.setClip("hslHueCenter", value) }
    LumetriSlider { label: qsTr("Hue width"); from: 0; to: 180; value: root.panel.clipValue("hslHueWidth", 30); suffix: "deg"; onValueCommitted: value => root.panel.setClip("hslHueWidth", value) }
    LumetriSlider { label: qsTr("Saturation low"); from: 0; to: 100; value: root.panel.clipValue("hslSaturationMin", 0); onValueCommitted: value => root.panel.setClip("hslSaturationMin", Math.min(value, root.panel.clipValue("hslSaturationMax", 100))) }
    LumetriSlider { label: qsTr("Saturation high"); from: 0; to: 100; value: root.panel.clipValue("hslSaturationMax", 100); onValueCommitted: value => root.panel.setClip("hslSaturationMax", Math.max(value, root.panel.clipValue("hslSaturationMin", 0))) }
    LumetriSlider { label: qsTr("Luma low"); from: 0; to: 100; value: root.panel.clipValue("hslLumaMin", 0); onValueCommitted: value => root.panel.setClip("hslLumaMin", Math.min(value, root.panel.clipValue("hslLumaMax", 100))) }
    LumetriSlider { label: qsTr("Luma high"); from: 0; to: 100; value: root.panel.clipValue("hslLumaMax", 100); onValueCommitted: value => root.panel.setClip("hslLumaMax", Math.max(value, root.panel.clipValue("hslLumaMin", 0))) }
    LumetriSlider { label: qsTr("Denoise"); from: 0; to: 100; value: root.panel.clipValue("hslDenoise", 0); onValueCommitted: value => root.panel.setClip("hslDenoise", value) }
    LumetriSlider { label: qsTr("Blur"); from: 0; to: 100; value: root.panel.clipValue("hslBlur", 0); onValueCommitted: value => root.panel.setClip("hslBlur", value) }
    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
    Text { text: qsTr("Correction"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
    LumetriSlider { label: qsTr("Hue"); from: -180; to: 180; value: root.panel.clipValue("hslCorrectionHue", 0); suffix: "deg"; onValueCommitted: value => root.panel.setClip("hslCorrectionHue", value) }
    LumetriSlider { label: qsTr("Saturation"); value: root.panel.clipValue("hslCorrectionSaturation", 0); onValueCommitted: value => root.panel.setClip("hslCorrectionSaturation", value) }
    LumetriSlider { label: qsTr("Luma"); value: root.panel.clipValue("hslCorrectionLuma", 0); onValueCommitted: value => root.panel.setClip("hslCorrectionLuma", value) }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        LumetriActionButton {
            text: qsTr("Reset")
            onClicked: root.panel.resetKeys(["hslHueCenter", "hslHueWidth", "hslSaturationMin", "hslSaturationMax", "hslLumaMin", "hslLumaMax", "hslDenoise", "hslBlur", "hslCorrectionHue", "hslCorrectionSaturation", "hslCorrectionLuma"])
        }
    }
}
