//qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

// Right-click menu for one line of the transcript. The index is set by whoever
// opens it, because a Menu outlives the row that spawned it - reading the index
// off the delegate at trigger time would act on whatever row happens to be under
// the pointer by then.
Menu {
    id: root

    property int segmentIndex: -1
    signal editRequested(int segmentIndex)
    signal deleteRequested(int segmentIndex)
    // Cut the footage behind this line (or the whole selected run) out of the
    // timeline, not just the transcript text - text-based editing.
    signal removeFromVideoRequested(int segmentIndex)

    width: 220
    padding: 4

    background: Rectangle {
        color: Theme.menuBg
        border.color: Theme.menuBorder
        radius: Theme.radiusSm
    }

    // DarkMenuItem rather than MenuItem, and not through Menu's `delegate`: that
    // property only builds rows for actions the menu is handed, so items declared
    // here as children kept the default style. A Menu makes its first row the
    // current one as it opens, and the default style's highlight is a near-white
    // box - so "Edit text" came up looking both light and disabled every time the
    // menu was opened, while the row below it looked right.
    DarkMenuItem {
        text: qsTr("Edit text")
        enabled: root.segmentIndex >= 0
        onTriggered: root.editRequested(root.segmentIndex)
    }

    DarkMenuItem {
        text: qsTr("Delete segment")
        enabled: root.segmentIndex >= 0
        onTriggered: root.deleteRequested(root.segmentIndex)
    }

    DarkMenuItem {
        text: qsTr("Remove from video")
        enabled: root.segmentIndex >= 0
        onTriggered: root.removeFromVideoRequested(root.segmentIndex)
    }
}
