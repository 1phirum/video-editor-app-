import QtQuick
import QtQuick.Layouts
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

LumetriSection {
    id: root
    required property var panel
    title: "Vignette"
    checkable: true
    sectionEnabled: panel.clipValue("vignetteEnabled", true)
    onSectionToggled: enabled => panel.setClip("vignetteEnabled", enabled)
    expanded: false

    LumetriSlider { label: "Amount"; from: -100; to: 100; value: root.panel.clipValue("vignette", 0); onValueCommitted: value => root.panel.setClip("vignette", value) }
    LumetriSlider { label: "Midpoint"; from: 0; to: 100; value: root.panel.clipValue("vignetteMidpoint", 50); onValueCommitted: value => root.panel.setClip("vignetteMidpoint", value) }
    LumetriSlider { label: "Roundness"; from: -100; to: 100; value: root.panel.clipValue("vignetteRoundness", 0); onValueCommitted: value => root.panel.setClip("vignetteRoundness", value) }
    LumetriSlider { label: "Feather"; from: 0; to: 100; value: root.panel.clipValue("vignetteFeather", 50); onValueCommitted: value => root.panel.setClip("vignetteFeather", value) }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        LumetriActionButton {
            text: "Reset"
            onClicked: root.panel.resetKeys(["vignette", "vignetteMidpoint", "vignetteRoundness", "vignetteFeather"])
        }
    }
}
