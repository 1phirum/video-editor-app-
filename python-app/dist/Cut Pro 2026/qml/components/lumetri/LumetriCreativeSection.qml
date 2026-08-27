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
    title: "Creative"
    checkable: true
    sectionEnabled: panel.clipValue("creativeEnabled", true)
    onSectionToggled: enabled => panel.setClip("creativeEnabled", enabled)
    expanded: false

    LumetriSettingRow {
        label: "Look"
        WhisperComboBox {
            property var choices: ["None", "Faded Film", "Warm", "Cool"]
            model: choices
            currentIndex: Math.max(0, choices.indexOf(root.panel.clipValue("look", "None")))
            Layout.fillWidth: true
            onActivated: root.panel.setClip("look", currentText)
        }
    }
    LumetriSlider { label: "Intensity"; from: 0; to: 200; value: root.panel.clipValue("lookIntensity", 100); onValueCommitted: value => root.panel.setClip("lookIntensity", value) }
    LumetriSlider { label: "Faded Film"; from: 0; to: 100; value: root.panel.clipValue("fade", 0); onValueCommitted: value => root.panel.setClip("fade", value) }
    LumetriSlider { label: "Sharpen"; from: 0; to: 100; value: root.panel.clipValue("sharpen", 0); onValueCommitted: value => root.panel.setClip("sharpen", value) }
    LumetriSlider { label: "Vibrance"; value: root.panel.clipValue("vibrance", 0); onValueCommitted: value => root.panel.setClip("vibrance", value) }
    LumetriSlider { label: "Saturation"; from: 0; to: 200; value: root.panel.clipValue("creativeSaturation", 100); onValueCommitted: value => root.panel.setClip("creativeSaturation", value) }
    
    RowLayout {
        Layout.fillWidth: true
        spacing: 16
        
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            LumetriTintWheel {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 84
                Layout.preferredHeight: 84
                valueX: root.panel.clipValue("creativeShadowTintX", 0)
                valueY: root.panel.clipValue("creativeShadowTintY", 0)
                onPositionCommitted: (x, y) => {
                    root.panel.setClip("creativeShadowTintX", x)
                    root.panel.setClip("creativeShadowTintY", y)
                }
            }
            Text { text: "Shadow Tint"; color: Theme.textSecondary; font.pixelSize: Theme.fsXs; Layout.alignment: Qt.AlignHCenter }
        }
        
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            LumetriTintWheel {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 84
                Layout.preferredHeight: 84
                valueX: root.panel.clipValue("creativeHighlightTintX", 0)
                valueY: root.panel.clipValue("creativeHighlightTintY", 0)
                onPositionCommitted: (x, y) => {
                    root.panel.setClip("creativeHighlightTintX", x)
                    root.panel.setClip("creativeHighlightTintY", y)
                }
            }
            Text { text: "Highlight Tint"; color: Theme.textSecondary; font.pixelSize: Theme.fsXs; Layout.alignment: Qt.AlignHCenter }
        }
    }

    LumetriSlider { label: "Tint Balance"; value: root.panel.clipValue("tintBalance", 0); onValueCommitted: value => root.panel.setClip("tintBalance", value) }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        LumetriActionButton {
            text: "Reset"
            onClicked: root.panel.resetKeys(["look", "lookIntensity", "fade", "sharpen", "vibrance", "creativeSaturation", "creativeShadowTintX", "creativeShadowTintY", "creativeHighlightTintX", "creativeHighlightTintY", "tintBalance"])
        }
    }
}
