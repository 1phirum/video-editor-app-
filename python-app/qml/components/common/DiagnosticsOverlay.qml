// qmllint disable
import QtQuick
import QtQuick.Controls
import CutPro 1.0
import "../../theme"

// The readable end of the diagnostics layer. Everything below was already being
// measured - the watchdog, the item census, the model guard, the crash channel -
// and none of it was reachable without a debugger, which is how a freeze stayed
// unexplained across a dozen attempts at fixing it.
//
// Deliberately not a dialog: it must be able to appear over a window that is not
// finishing frames, so it holds no modal grab, takes no focus, and never blocks
// on anything. Refresh is a plain timer that only runs while it is visible.
Rectangle {
    id: root

    // Same pattern as the panels: the parent decides placement.
    property bool active: false

    // One place to add a field. `key` indexes the statistics map; a missing key
    // shows as "-" rather than "undefined", because a blank means "this build
    // does not report it" and that is worth seeing.
    readonly property var fields: [
        { label: "Worst GUI stall", key: "guiWorstStallMs", unit: " ms" },
        { label: "  its scope", key: "guiWorstStallScope", unit: "" },
        { label: "  its verdict", key: "guiWorstStallVerdict", unit: "" },
        { label: "GUI stalls", key: "guiStalls", unit: "" },
        { label: "  severe (>1500ms)", key: "guiSevereStalls", unit: "" },
        { label: "Scene items", key: "censusItems", unit: "" },
        { label: "  deepest chain", key: "censusMaxDepth", unit: "" },
        { label: "  worst parent", key: "censusWorstParent", unit: "" },
        { label: "  its children", key: "censusWorstParentChildren", unit: "" },
        { label: "Models clamped", key: "modelGuardClamped", unit: "" },
        { label: "  largest model", key: "modelGuardLargestKey", unit: "" },
        { label: "  its count", key: "modelGuardLargestCount", unit: "" },
        { label: "Crash reporter", key: "crashReporterRunning", unit: "" },
        { label: "Selection rebuilds", key: "selectionDetailNotify", unit: "" }
    ]

    property var stats: ({})
    property string lastSnapshot: ""

    visible: root.active
    color: "#e6101215"
    border.width: 1
    border.color: Theme.accent
    radius: 3
    z: 100000

    // Only while shown, and slowly: this reads counters the GUI thread also
    // writes, and a diagnostic that costs a frame is a diagnostic that changes
    // what it is measuring.
    Timer {
        interval: 700
        repeat: true
        running: root.active
        triggeredOnStart: true
        onTriggered: root.refresh()
    }

    function refresh() {
        var merged = Backend.previewDecodeStatistics()
        // The census lives in cutpro_scene, so it is not in the backend's map.
        var scene = Diagnostics.statistics
        for (var key in scene)
            merged[key] = scene[key]
        root.stats = merged
    }

    function shown(key) {
        var value = root.stats[key]
        if (value === undefined || value === null || value === "")
            return "-"
        if (typeof value === "object")
            return JSON.stringify(value)
        return String(value)
    }

    Column {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 0

        Text {
            text: "Diagnostics   F12 hides   Ctrl+Shift+D writes a snapshot"
            color: Theme.accent
            font.family: Theme.monoFont
            font.pixelSize: Theme.fsXs
            font.weight: Font.DemiBold
        }
        Item { width: 1; height: 6 }

        Repeater {
            model: root.fields
            delegate: Row {
                required property var modelData
                spacing: 6
                Text {
                    width: 150
                    text: modelData.label
                    color: Theme.textMuted
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fsXs
                    elide: Text.ElideRight
                }
                Text {
                    width: root.width - 176
                    text: root.shown(modelData.key)
                          + (root.shown(modelData.key) === "-"
                             ? "" : modelData.unit)
                    color: Theme.textPrimary
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fsXs
                    elide: Text.ElideRight
                }
            }
        }

        Item { width: 1; height: 8 }

        // The full per-call-site table. This is the line that names the runaway
        // Repeater, so it gets the remaining height rather than a fixed slice.
        Text {
            text: "Guarded models, largest first"
            color: Theme.accent
            font.family: Theme.monoFont
            font.pixelSize: Theme.fsXs
        }
        Text {
            width: parent.width
            text: ModelGuard.report()
            color: Theme.textSecondary
            font.family: Theme.monoFont
            font.pixelSize: Theme.fsXs
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 14
            elide: Text.ElideRight
        }

        Item { width: 1; height: 6 }
        Text {
            width: parent.width
            visible: root.lastSnapshot !== ""
            text: "wrote " + root.lastSnapshot
            color: Theme.textMuted
            font.family: Theme.monoFont
            font.pixelSize: Theme.fsXs
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 2
            elide: Text.ElideMiddle
        }
    }

    function writeSnapshot() {
        root.lastSnapshot = Diagnostics.writeSnapshot("manual, from the overlay")
        root.refresh()
    }
}
