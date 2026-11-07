// qmllint disable
import QtQuick
import QtQuick.Controls
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"

// What a right-click on empty track space offers. The hole between two clips on
// one track is not a thing that can be selected - there is no item there to click
// on - so the commands that act on it have to come from the space itself, and they
// are addressed by the track and the time that was clicked.
//
// Opened even when there is no gap under the pointer, with the first command
// disabled. Right-clicking a stretch of nothing and getting nothing back is how
// the feature stays undiscovered; the menu is also the only place that says what
// the empty space is.
Menu {
    id: root

    property string track: ""
    property real positionMs: 0
    // Backend.gapAt(): { found, startMs, endMs, durationMs }, or empty when the
    // pointer is inside a clip, past the last clip on the track, or on a track
    // that is locked.
    property var gapData: ({})

    readonly property bool hasGap: Boolean(root.gapData && root.gapData.found)
    readonly property real gapMs: root.hasGap
                                  ? Number(root.gapData.durationMs || 0) : 0
    readonly property string gapLabel: root.gapMs >= 1000
                                       ? qsTr("%1 s").arg((root.gapMs / 1000).toFixed(2))
                                       : qsTr("%1 ms").arg(Math.round(root.gapMs))

    implicitWidth: 300
    delegate: DarkMenuItem {}
    background: Rectangle {
        color: Theme.menuBg
        border.color: Theme.menuBorder
        radius: Theme.radiusSm
    }

    // Not a command - the line that says what was clicked on. A gap is silence and
    // black in the export, which is the thing a user seeing a stripe of grey in the
    // timeline is actually asking about.
    DarkMenuItem {
        enabled: false
        text: root.hasGap
              ? qsTr("Gap on %1 - %2 of black").arg(root.track).arg(root.gapLabel)
              : (root.track === "" ? qsTr("Empty space") : qsTr("Empty space on %1").arg(root.track))
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        text: root.hasGap ? qsTr("Close Gap (%1)").arg(root.gapLabel) : qsTr("Close Gap")
        enabled: root.hasGap
        // Everything to the right on the track slides back by the length of the
        // hole, which is the same edit as a ripple delete of the empty space.
        onTriggered: Backend.closeGap(root.track,
                                      Number(root.gapData.startMs || 0),
                                      Number(root.gapData.endMs || 0))
    }
    DarkMenuItem {
        text: root.track === "" ? qsTr("Close All Gaps on This Track")
                                : qsTr("Close All Gaps on %1").arg(root.track)
        enabled: root.track !== ""
        onTriggered: Backend.closeAllGaps(root.track)
    }
    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.menuSeparator } }
    DarkMenuItem {
        text: qsTr("Move Playhead Here")
        enabled: root.positionMs >= 0
        onTriggered: Backend.playheadMs = Math.round(root.positionMs)
    }
}
