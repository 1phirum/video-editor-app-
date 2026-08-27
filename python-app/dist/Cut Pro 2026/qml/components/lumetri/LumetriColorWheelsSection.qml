pragma ComponentBehavior: Bound

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
    title: "Color Wheels & Match"
    checkable: true
    sectionEnabled: panel.clipValue("colorWheelsEnabled", true)
    onSectionToggled: enabled => panel.setClip("colorWheelsEnabled", enabled)
    expanded: false

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: [
                { name: "Shadows", prefix: "shadow" },
                { name: "Midtones", prefix: "midtone" },
                { name: "Highlights", prefix: "highlight" }
            ]
            delegate: ColumnLayout {
                id: wheelColumn
                required property var modelData
                Layout.fillWidth: true
                spacing: 3
                Text { text: wheelColumn.modelData.name; color: Theme.textSecondary; font.pixelSize: Theme.fsXs; Layout.alignment: Qt.AlignHCenter }
                LumetriColorWheel {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.max(62, Math.min(92, (root.width - 46) / 3))
                    Layout.preferredHeight: width
                    valueX: root.panel.clipValue(wheelColumn.modelData.prefix + "WheelX", 0)
                    valueY: root.panel.clipValue(wheelColumn.modelData.prefix + "WheelY", 0)
                    onPositionCommitted: (x, y) => {
                        root.panel.setClip(wheelColumn.modelData.prefix + "WheelX", x)
                        root.panel.setClip(wheelColumn.modelData.prefix + "WheelY", y)
                    }
                }
            }
        }
    }
    LumetriSlider { label: "Shadow Luma"; value: root.panel.clipValue("shadowLuma", 0); onValueCommitted: value => root.panel.setClip("shadowLuma", value) }
    LumetriSlider { label: "Midtone Luma"; value: root.panel.clipValue("midtoneLuma", 0); onValueCommitted: value => root.panel.setClip("midtoneLuma", value) }
    LumetriSlider { label: "Highlight Luma"; value: root.panel.clipValue("highlightLuma", 0); onValueCommitted: value => root.panel.setClip("highlightLuma", value) }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        LumetriActionButton {
            text: "Reset"
            onClicked: root.panel.resetKeys(["shadowWheelX", "shadowWheelY", "shadowLuma", "midtoneWheelX", "midtoneWheelY", "midtoneLuma", "highlightWheelX", "highlightWheelY", "highlightLuma"])
        }
    }
}
