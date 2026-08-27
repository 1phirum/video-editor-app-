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

    RowLayout {
        spacing: 3

        LumetriTextField {
            id: numericField
            Layout.preferredWidth: 48
            horizontalAlignment: TextInput.AlignRight
            font.family: Theme.monoFont
            validator: DoubleValidator {
                bottom: root.from
                top: root.to
                decimals: root.decimals
                notation: DoubleValidator.StandardNotation
            }
            Binding {
                target: numericField
                property: "text"
                value: Number(slider.value).toFixed(root.decimals)
                when: !numericField.activeFocus
            }
            onEditingFinished: {
                var parsed = Number(text)
                if (!isNaN(parsed))
                    root.valueCommitted(Math.max(root.from,
                                                 Math.min(root.to, parsed)))
            }
        }

        Text {
            visible: root.suffix !== ""
            text: root.suffix.trim()
            color: Theme.textMuted
            font.pixelSize: Theme.fsXs
        }
    }
}
