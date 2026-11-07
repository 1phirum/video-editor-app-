// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"

// A colour picker in the app's own colours.
//
// Replaces QtQuick.Dialogs.ColorDialog, which draws itself from the platform
// style: a white panel with light-grey controls in the middle of a dark editor,
// and no alpha channel unless it is asked for - so picking a caption background
// silently threw away its transparency.
//
// Saturation and value on the square, hue underneath, alpha under that when the
// caller wants it. Hex accepts both #rrggbb and #aarrggbb, so a value copied out
// of the panel can be pasted straight back in.
NativeModalWindow {
    id: dialog

    // Whether the alpha row is offered. A caption background is #b3000000 and
    // needs it; text colour is opaque by nature and the row would only invite a
    // half-visible caption.
    property bool showAlpha: false
    // Suggestions, not history: the colours a caption is actually set to.
    property var presets: ["#ffffff", "#000000", "#ffd60a", "#ff8a00", "#f2453d",
                           "#4cd964", "#34c8ff", "#4b8ff5", "#b45cff", "#8a8a8a"]

    signal picked(string value)

    dialogWidth: 380
    dialogHeight: showAlpha ? 486 : 452
    color: Theme.translationDialogSurface

    // Hue, saturation, value and alpha are the state; colorValue is derived from
    // them. Kept apart because a fully black or fully white colour has no hue of
    // its own, and round-tripping through it would send the hue slider home every
    // time the value reached an end of its range.
    property real hue: 0
    property real sat: 0
    property real val: 1
    property real alpha: 1
    // Set while the fields below are being written from the state, so their own
    // change handlers do not write back and fight the drag.
    property bool syncing: false

    readonly property color current: Qt.hsva(hue, sat, val, alpha)

    function openWith(value) {
        dialog.adopt(value)
        dialog.openNative()
    }

    // Pull a colour apart into the four numbers the controls drive.
    function adopt(value) {
        const c = Qt.color(String(value).length > 0 ? value : "#ffffff")
        dialog.syncing = true
        // hsvHue is -1 for a grey, which would otherwise reset the hue slider to
        // red every time the saturation was dragged to zero.
        if (c.hsvSaturation > 0 && c.hsvHue >= 0)
            dialog.hue = c.hsvHue
        dialog.sat = c.hsvSaturation
        dialog.val = c.hsvValue
        dialog.alpha = dialog.showAlpha ? c.a : 1
        dialog.syncing = false
        dialog.pushHex()
    }

    // "#rrggbb", or "#aarrggbb" when the alpha row is in play and the colour is
    // not opaque - the shortest form that survives a round trip.
    function currentString() {
        const c = dialog.current
        const byte = function (v) {
            const s = Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16)
            return s.length < 2 ? "0" + s : s
        }
        const rgb = byte(c.r) + byte(c.g) + byte(c.b)
        return dialog.showAlpha && dialog.alpha < 0.999
               ? "#" + byte(dialog.alpha) + rgb : "#" + rgb
    }

    function pushHex() {
        dialog.syncing = true
        hexField.text = dialog.currentString()
        dialog.syncing = false
    }

    // Six digits or eight, with or without the hash. Anything else and the field
    // goes back to what the controls are actually showing rather than leaving a
    // half-typed colour looking like it was accepted.
    function takeHex(entered) {
        const raw = String(entered).replace("#", "").trim()
        if (raw.length === 6 || raw.length === 8)
            dialog.adopt("#" + raw)
        else
            dialog.pushHex()
    }

    onHueChanged: if (!syncing) pushHex()
    onSatChanged: if (!syncing) pushHex()
    onValChanged: if (!syncing) pushHex()
    onAlphaChanged: if (!syncing) pushHex()

    // Transparency, drawn the way every editor draws it. Cheap: one 16 px tile
    // repeated, not a grid of rectangles.
    component Checkerboard: Canvas {
        onPaint: {
            const ctx = getContext("2d")
            const step = 6
            ctx.fillStyle = "#4a4a4a"
            ctx.fillRect(0, 0, width, height)
            ctx.fillStyle = "#5f5f5f"
            for (let y = 0; y < height; y += step)
                for (let x = 0; x < width; x += step)
                    if (((x / step) + (y / step)) % 2 === 0)
                        ctx.fillRect(x, y, step, step)
        }
    }

    // A flat button in the app's palette. QtQuick.Controls' own Button is drawn
    // by the platform style, which is where the old dialog's light grey came
    // from.
    component FlatButton: Rectangle {
        id: button
        property string label: ""
        property bool primary: false
        signal clicked

        implicitWidth: Math.max(96, caption.implicitWidth + 28)
        implicitHeight: 30
        radius: Theme.radiusMd
        color: button.primary
               ? (area.pressed ? Qt.darker(Theme.accent, 1.2)
                               : area.containsMouse ? Qt.lighter(Theme.accent, 1.1)
                                                    : Theme.accent)
               : (area.pressed ? Theme.bgCardHeader
                               : area.containsMouse ? Theme.translationRowHover
                                                    : Theme.bgCard)
        border.width: button.primary ? 0 : 1
        border.color: Theme.translationRowBorder

        Text {
            id: caption
            anchors.centerIn: parent
            text: button.label
            color: button.primary ? "#ffffff" : Theme.textPrimary
            font.pixelSize: Theme.fsMd
            font.weight: button.primary ? Font.DemiBold : Font.Normal
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        // Saturation across, value down, over the hue picked below. Three layers
        // rather than a shader: the same thing every colour picker draws, and it
        // costs two gradients.
        Item {
            id: square
            Layout.fillWidth: true
            Layout.preferredHeight: 186

            function apply(px, py) {
                dialog.sat = Math.max(0, Math.min(1, px / square.width))
                dialog.val = Math.max(0, Math.min(1, 1 - py / square.height))
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                color: Qt.hsva(dialog.hue, 1, 1, 1)
                clip: true

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#ffffffff" }
                        GradientStop { position: 1.0; color: "#00ffffff" }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#00000000" }
                        GradientStop { position: 1.0; color: "#ff000000" }
                    }
                }
            }

            Rectangle {
                width: 16
                height: 16
                radius: 8
                color: "transparent"
                border.width: 2
                border.color: dialog.val > 0.6 && dialog.sat < 0.5 ? "#20000000"
                                                                   : "#ffffffff"
                x: dialog.sat * square.width - 8
                y: (1 - dialog.val) * square.height - 8

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 6
                    color: "transparent"
                    border.width: 1
                    border.color: "#60000000"
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                onPressed: (m) => square.apply(m.x, m.y)
                onPositionChanged: (m) => { if (pressed) square.apply(m.x, m.y) }
            }
        }

        // Hue, full saturation and value across its whole length, so the strip
        // reads as the ring it stands for.
        Item {
            id: hueBar
            Layout.fillWidth: true
            Layout.preferredHeight: 16

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.000; color: "#ff0000" }
                    GradientStop { position: 0.167; color: "#ffff00" }
                    GradientStop { position: 0.333; color: "#00ff00" }
                    GradientStop { position: 0.500; color: "#00ffff" }
                    GradientStop { position: 0.667; color: "#0000ff" }
                    GradientStop { position: 0.833; color: "#ff00ff" }
                    GradientStop { position: 1.000; color: "#ff0000" }
                }
            }

            Rectangle {
                width: 12
                height: parent.height + 6
                radius: 6
                y: -3
                x: dialog.hue * hueBar.width - 6
                color: Qt.hsva(dialog.hue, 1, 1, 1)
                border.width: 2
                border.color: "#ffffff"
            }

            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.PointingHandCursor
                function apply(px) {
                    dialog.hue = Math.max(0, Math.min(0.9999,
                                                      (px - 4) / hueBar.width))
                }
                onPressed: (m) => apply(m.x)
                onPositionChanged: (m) => { if (pressed) apply(m.x) }
            }
        }

        // Opacity, over a checkerboard so the left end reads as see-through
        // rather than as white.
        Item {
            id: alphaBar
            visible: dialog.showAlpha
            Layout.fillWidth: true
            Layout.preferredHeight: dialog.showAlpha ? 16 : 0

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                clip: true
                color: "transparent"

                Checkerboard {
                    anchors.fill: parent
                }

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0.0
                            color: Qt.hsva(dialog.hue, dialog.sat, dialog.val, 0)
                        }
                        GradientStop {
                            position: 1.0
                            color: Qt.hsva(dialog.hue, dialog.sat, dialog.val, 1)
                        }
                    }
                }
            }

            Rectangle {
                width: 12
                height: parent.height + 6
                radius: 6
                y: -3
                x: dialog.alpha * alphaBar.width - 6
                color: "transparent"
                border.width: 2
                border.color: "#ffffff"
            }

            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.PointingHandCursor
                function apply(px) {
                    dialog.alpha = Math.max(0, Math.min(1, (px - 4) / alphaBar.width))
                }
                onPressed: (m) => apply(m.x)
                onPositionChanged: (m) => { if (pressed) apply(m.x) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: qsTr("Hex")
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 30
                radius: Theme.radiusSm
                color: Theme.bgCard
                border.width: 1
                border.color: hexField.activeFocus ? Theme.accent
                                                   : Theme.translationRowBorder

                TextInput {
                    id: hexField
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsMd
                    font.family: Theme.monoFont
                    selectByMouse: true
                    selectionColor: Theme.accent
                    text: "#ffffff"
                    // Only the characters a hex colour is made of, so there is
                    // nothing to reject on the way out.
                    validator: RegularExpressionValidator {
                        regularExpression: /#?[0-9a-fA-F]{0,8}/
                    }
                    onEditingFinished: dialog.takeHex(text)
                    Keys.onReturnPressed: dialog.takeHex(text)
                }
            }
        }

        Text {
            text: qsTr("Presets")
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: dialog.presets
                delegate: Rectangle {
                    required property var modelData
                    width: 26
                    height: 26
                    radius: Theme.radiusSm
                    color: modelData
                    border.width: swatch.containsMouse ? 2 : 1
                    border.color: swatch.containsMouse ? Theme.accent
                                                       : Theme.translationRowBorder

                    MouseArea {
                        id: swatch
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // Keeps the alpha the user already chose: a preset picks
                        // the colour, not how transparent the box behind it is.
                        onClicked: {
                            const keep = dialog.alpha
                            dialog.adopt(modelData)
                            if (dialog.showAlpha) {
                                dialog.alpha = keep
                                dialog.pushHex()
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true; Layout.minimumHeight: 2 }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // What is about to be applied, over the checkerboard so a partly
            // transparent choice is visible as one.
            Rectangle {
                implicitWidth: 58
                implicitHeight: 30
                radius: Theme.radiusSm
                clip: true
                color: "transparent"
                border.width: 1
                border.color: Theme.translationRowBorder

                Checkerboard { anchors.fill: parent }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    color: dialog.current
                }
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                label: qsTr("Cancel")
                onClicked: dialog.close()
            }

            FlatButton {
                label: qsTr("OK")
                primary: true
                onClicked: {
                    dialog.picked(dialog.currentString())
                    dialog.close()
                }
            }
        }





    }
}
