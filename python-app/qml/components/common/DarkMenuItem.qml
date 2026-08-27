import QtQuick
import QtQuick.Controls
import "../../theme"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

MenuItem {
    id: control

    implicitWidth: 220
    implicitHeight: 34
    leftPadding: 12
    rightPadding: 12

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Text {
        text: control.text
        color: control.enabled
               ? (control.highlighted ? Theme.textPrimary : Theme.textSecondary)
               : Theme.textMuted
        font.pixelSize: Theme.fsMd
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: control.highlighted && control.enabled ? Theme.hover : "transparent"
    }
}
