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
    title: "Basic Correction"
    checkable: true
    sectionEnabled: panel.clipValue("basicEnabled", true)
    onSectionToggled: enabled => panel.setClip("basicEnabled", enabled)
    summary: panel.activeClip && panel.activeClip.name ? String(panel.activeClip.name) : ""

    LumetriSettingRow {
        label: "Input LUT"
        RowLayout {
            Layout.fillWidth: true
            LumetriTextField {
                Layout.fillWidth: true
                text: root.panel.sourceColor.inputLut || "None"
                onEditingFinished: root.panel.setMedia("inputLut", text)
            }
            LumetriActionButton { text: "Browse"; onClicked: root.panel.browseInputLut() }
        }
    }
    Text { text: "White Balance"; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
    LumetriSlider { label: "Temperature"; value: root.panel.clipValue("temperature", 0); onValueCommitted: value => root.panel.setClip("temperature", value) }
    LumetriSlider { label: "Tint"; value: root.panel.clipValue("tint", 0); onValueCommitted: value => root.panel.setClip("tint", value) }
    Text { text: "Tone"; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
    LumetriSlider { label: "Exposure"; from: -5; to: 5; decimals: 1; value: root.panel.clipValue("exposure", 0); onValueCommitted: value => root.panel.setClip("exposure", value) }
    LumetriSlider { label: "Contrast"; value: root.panel.clipValue("contrast", 0); onValueCommitted: value => root.panel.setClip("contrast", value) }
    LumetriSlider { label: "Highlights"; value: root.panel.clipValue("highlights", 0); onValueCommitted: value => root.panel.setClip("highlights", value) }
    LumetriSlider { label: "Shadows"; value: root.panel.clipValue("shadows", 0); onValueCommitted: value => root.panel.setClip("shadows", value) }
    LumetriSlider { label: "Whites"; value: root.panel.clipValue("whites", 0); onValueCommitted: value => root.panel.setClip("whites", value) }
    LumetriSlider { label: "Blacks"; value: root.panel.clipValue("blacks", 0); onValueCommitted: value => root.panel.setClip("blacks", value) }
    LumetriSlider { label: "HDR Specular"; value: root.panel.clipValue("hdrSpecular", 0); onValueCommitted: value => root.panel.setClip("hdrSpecular", value) }
    LumetriSlider { label: "Saturation"; from: 0; to: 200; value: root.panel.clipValue("saturation", 100); onValueCommitted: value => root.panel.setClip("saturation", value) }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        LumetriActionButton {
            text: "Reset"
            onClicked: root.panel.resetKeys([
                "temperature", "tint", "exposure", "contrast", "highlights",
                "shadows", "whites", "blacks", "hdrSpecular", "saturation"
            ])
        }
    }
}
