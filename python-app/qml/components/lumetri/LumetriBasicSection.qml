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
    title: qsTr("Basic Correction")
    checkable: true
    sectionEnabled: panel.clipValue("basicEnabled", true)
    onSectionToggled: enabled => panel.setClip("basicEnabled", enabled)
    summary: panel.activeClip && panel.activeClip.name ? String(panel.activeClip.name) : ""

    LumetriSettingRow {
        label: qsTr("Input LUT")
        RowLayout {
            Layout.fillWidth: true
            LumetriTextField {
                Layout.fillWidth: true
                text: root.panel.sourceColor.inputLut || "None"
                onEditingFinished: root.panel.setMedia("inputLut", text)
            }
            LumetriActionButton { text: qsTr("Browse"); onClicked: root.panel.browseInputLut() }
        }
    }
    Text { text: qsTr("White Balance"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
    LumetriSlider { label: qsTr("Temperature"); help: qsTr("Warm or cool the picture. Right adds orange (warmer), left adds blue (cooler)."); value: root.panel.clipValue("temperature", 0); onValueCommitted: value => root.panel.setClip("temperature", value) }
    LumetriSlider { label: qsTr("Tint"); help: qsTr("Corrects a green or magenta colour cast. Nudge until skin tones look natural."); value: root.panel.clipValue("tint", 0); onValueCommitted: value => root.panel.setClip("tint", value) }
    Text { text: qsTr("Tone"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
    LumetriSlider { label: qsTr("Exposure"); help: qsTr("Overall brightness. Right makes everything brighter, left makes it darker."); from: -5; to: 5; decimals: 1; value: root.panel.clipValue("exposure", 0); onValueCommitted: value => root.panel.setClip("exposure", value) }
    LumetriSlider { label: qsTr("Contrast"); help: qsTr("Difference between light and dark. More contrast = punchier; less = flatter."); value: root.panel.clipValue("contrast", 0); onValueCommitted: value => root.panel.setClip("contrast", value) }
    LumetriSlider { label: qsTr("Highlights"); help: qsTr("Brightness of the brightest areas only. Pull left to recover blown-out skies."); value: root.panel.clipValue("highlights", 0); onValueCommitted: value => root.panel.setClip("highlights", value) }
    LumetriSlider { label: qsTr("Shadows"); help: qsTr("Brightness of the darkest areas only. Push right to reveal detail in shadows."); value: root.panel.clipValue("shadows", 0); onValueCommitted: value => root.panel.setClip("shadows", value) }
    LumetriSlider { label: qsTr("Whites"); help: qsTr("Sets the brightest point of the image (the white clipping level)."); value: root.panel.clipValue("whites", 0); onValueCommitted: value => root.panel.setClip("whites", value) }
    LumetriSlider { label: qsTr("Blacks"); help: qsTr("Sets the darkest point of the image (the black clipping level)."); value: root.panel.clipValue("blacks", 0); onValueCommitted: value => root.panel.setClip("blacks", value) }
    LumetriSlider { label: qsTr("HDR Specular"); help: qsTr("Boosts the brightest highlights on HDR footage. Leave at 0 for normal video."); value: root.panel.clipValue("hdrSpecular", 0); onValueCommitted: value => root.panel.setClip("hdrSpecular", value) }
    LumetriSlider { label: qsTr("Saturation"); help: qsTr("Colour intensity. 0 is black-and-white, 100 is normal, 200 is very vivid."); from: 0; to: 200; value: root.panel.clipValue("saturation", 100); onValueCommitted: value => root.panel.setClip("saturation", value) }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        LumetriActionButton {
            text: qsTr("Reset")
            onClicked: root.panel.resetKeys([
                "temperature", "tint", "exposure", "contrast", "highlights",
                "shadows", "whites", "blacks", "hdrSpecular", "saturation"
            ])
        }
    }
}
