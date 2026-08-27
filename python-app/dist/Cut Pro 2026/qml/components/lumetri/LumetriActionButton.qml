import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

Button {
    id: control

    implicitWidth: Math.max(64, content.implicitWidth + 22)
    implicitHeight: 28
    padding: 0
    hoverEnabled: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Text {
        id: content
        text: control.text
        color: control.enabled ? Theme.textPrimary : Theme.textMuted
        font.pixelSize: Theme.fsXs
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: control.down ? Qt.darker(Theme.hover, 1.18)
                            : control.hovered ? Theme.hover : Theme.bgPrimary
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : "#3a3a3a"
        radius: Theme.radiusSm
    }
}
