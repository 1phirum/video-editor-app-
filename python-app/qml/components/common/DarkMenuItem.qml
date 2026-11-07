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
    // A Menu lays its items out through contentModel, and a delegate that is
    // only invisible still reserves its row - which is why a conditional entry
    // such as Restore Clip Audio left a 34 px blank stripe in the middle of the
    // clip menu. Collapsing the height here fixes every menu built from this
    // item rather than each call site remembering to.
    height: visible ? implicitHeight : 0
    leftPadding: 12
    rightPadding: 12

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.menuText : Theme.menuTextMuted
        font.pixelSize: Theme.fsMd
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        // Inset the highlight so its square corners never poke past the menu's
        // own rounded border on the first and last rows, and give it a matching
        // radius so it reads as a pill sitting inside the menu rather than a
        // block bleeding to the edges. Shared here so every menu behaves.
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        radius: Theme.radiusSm
        color: control.highlighted && control.enabled ? Theme.menuHighlight : "transparent"
    }
}
