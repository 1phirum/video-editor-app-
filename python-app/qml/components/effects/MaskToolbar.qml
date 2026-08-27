//qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

RowLayout {
    id: root
    property string clipId: ""
    signal maskRequested(string type)
    Layout.fillWidth: true
    Layout.preferredHeight: 30
    spacing: 4
    Text { text: "Masks"; color: Theme.textSecondary; font.pixelSize: Theme.fsXs; Layout.preferredWidth: 82 }
    Repeater {
        model: [{id: "ellipse", icon: "circle", tip: "Ellipse mask"}, {id: "rectangle", icon: "square", tip: "Four-point mask"}, {id: "pen", icon: "pen-tool", tip: "Free draw mask"}]
        delegate: IconButton {
            required property var modelData
            boxSize: 24; glyphSize: 13; iconName: modelData.icon
            enabled: root.clipId !== ""
            onClicked: root.maskRequested(modelData.id)
            ToolTip.visible: hovered; ToolTip.text: modelData.tip
        }
    }
}
