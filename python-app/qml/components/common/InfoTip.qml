//qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"

// A small info glyph that reveals a plain-language explanation on hover.
// Replaces the always-visible help paragraphs that used to sit under controls:
// the text is now one hover away instead of taking up panel space. Set `text`
// to the explanation; the glyph renders nothing while it is empty, so an unset
// `help` on a host row costs nothing.
//
// This generalizes the inline idiom already used on LumetriSlider (a `help`
// string shown through a HoverHandler + ToolTip on label hover) into one
// reusable control.
Item {
    id: root

    property string text: ""
    property int size: 13
    // How wide a wrapped tooltip gets before it wraps, so a long paragraph reads
    // as a paragraph rather than the single runaway line the default ToolTip
    // never wraps.
    property int maxWidth: 340

    implicitWidth: size
    implicitHeight: size
    visible: root.text !== ""

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: hover.hovered ? Theme.accent : Theme.textMuted

        Text {
            anchors.centerIn: parent
            text: "i"
            font.pixelSize: Math.round(root.size * 0.72)
            font.italic: true
            color: hover.hovered ? Theme.accent : Theme.textMuted
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }

    ToolTip {
        visible: hover.hovered
        delay: 300
        // Sits just to the right of the glyph, vertically centered on it.
        x: root.width + 4
        y: (root.height - height) / 2

        contentItem: Text {
            text: root.text
            color: Theme.textPrimary
            font.pixelSize: Theme.fsSm
            wrapMode: Text.WordWrap
            // implicitWidth is the unwrapped width; cap it at maxWidth so long
            // help wraps and short help stays tight.
            width: Math.min(root.maxWidth, implicitWidth)
        }
        background: Rectangle {
            color: Theme.bgCard
            border.color: Theme.border
            radius: Theme.radiusSm
        }
    }
}
