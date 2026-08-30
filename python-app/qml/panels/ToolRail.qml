pragma ComponentBehavior: Bound
// qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"

Rectangle {
    id: root
    color: "#3a3a3a"

    property int currentTool: 0
    signal toolSelected(int tool)

    // Right seam
    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: "#575757"
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 6
        spacing: 1

        Repeater {
            // Premiere's own tool glyphs. Their canvas is 44 units wide, so a
            // 22px glyph draws them at exactly the size Premiere does in a 32px
            // cell; the old lucide icons needed 18 because they were 24-unit.
            model: ["pr-select", "pr-track-select", "pr-ripple",
                    "pr-razor", "pr-hand", "pr-zoom"]
            delegate: IconButton {
                required property int index
                required property string modelData
                boxSize: 32
                glyphSize: 22
                iconName: modelData
                active: root.currentTool === index
                adobeStyle: true
                restColor: "#c2c2c2"
                hoverColor: "#ffffff"
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: ["Selection tool", "Track select forward",
                               "Ripple edit tool", "Razor tool", "Hand tool",
                               "Zoom tool"][index]
                onClicked: root.toolSelected(index)

                Canvas {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 3
                    anchors.bottomMargin: 3
                    width: 4
                    height: 4
                    opacity: 0.55
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = "#d2d2d2"
                        ctx.beginPath()
                        ctx.moveTo(width, 0)
                        ctx.lineTo(width, height)
                        ctx.lineTo(0, height)
                        ctx.closePath()
                        ctx.fill()
                    }
                }
            }
        }
    }
}
