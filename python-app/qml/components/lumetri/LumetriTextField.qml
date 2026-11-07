//qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

TextField {
    id: control

    implicitHeight: 28
    leftPadding: 8
    rightPadding: 8
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: "white"
    font.pixelSize: Theme.fsXs

    HoverHandler { cursorShape: Qt.IBeamCursor }

    background: Rectangle {
        color: Theme.bgPrimary
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : "#3a3a3a"
        radius: Theme.radiusSm
    }
}
