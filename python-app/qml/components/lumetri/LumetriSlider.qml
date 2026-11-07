//qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

RowLayout {
    id: root
    property string label: ""
    property real from: -100
    property real to: 100
    property real value: 0
    property int decimals: 0
    property string suffix: ""
    // Plain-language explanation shown when the label is hovered, so a client
    // who has never used a colour grader knows what each slider actually does.
    property string help: ""
    signal valueCommitted(real value)

    Layout.fillWidth: true
    Layout.preferredHeight: 28
    spacing: 7

    Text {
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fsXs
        Layout.preferredWidth: 82
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        HoverHandler { id: labelHover; enabled: root.help !== "" }
        ToolTip.visible: labelHover.hovered
        ToolTip.delay: 400
        ToolTip.text: root.help
    }
    Slider {
        id: slider
        Layout.fillWidth: true
        Layout.preferredHeight: 24
        from: root.from
        to: root.to
        stepSize: root.decimals > 0 ? 0.1 : 1
        live: true
        Binding { target: slider; property: "value"; value: root.value; when: !slider.pressed }
        onPressedChanged: if (!pressed) root.valueCommitted(value)
        onMoved: if (!pressed) root.valueCommitted(value)
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 2
            color: "#4a4a4a"
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                color: Theme.accent
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 10
            height: 10
            radius: 5
            color: slider.pressed ? "white" : Theme.textPrimary
            border.color: Theme.border
        }
    }

    // Premiere shows the value as plain blue text beside the slider, not in a
    // framed field: EffectValueText is the same control the Effect Controls
    // panel uses, so the two panels read alike. A scrub only redraws the slider;
    // the value is published once, when the gesture ends.
    EffectValueText {
        id: numeric
        Layout.preferredWidth: 62
        Layout.alignment: Qt.AlignVCenter
        from: root.from
        to: root.to
        decimals: root.decimals
        unit: root.suffix.trim()
        value: slider.value
        onValueDrafted: draft => slider.value = draft
        onValueEdited: edited => root.valueCommitted(edited)
    }
}
