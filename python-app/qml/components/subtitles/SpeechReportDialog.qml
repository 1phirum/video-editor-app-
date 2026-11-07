// qmllint disable
// What the last voiceover did to the timeline, in two lists.
//
// Both answer the same question - which lines came out longer than the room they
// had - from the two ends. "Overlapping" is what it did to the timeline: the voice
// kept talking past the next cue, so it was moved down a track and now plays over
// its neighbour. "Speaks too fast to fit" is what it would have cost to avoid
// that: the factor the line would have to be sped up by, which is the thing the
// user does not want done to it. A row jumps the playhead and selects the clip,
// because the fix - shorten the text, widen the gap, mute the line - is made on
// the timeline, not here.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

NativeModalWindow {
    id: dialog

    // One snapshot, taken on open. The clip list is free to change while this is
    // up - an undo, a trim - and a row that has gone stale simply selects nothing.
    property var report: ({})
    property int section: 0   // 0 overlapping, 1 too fast

    readonly property var rows: dialog.section === 0
                                ? (dialog.report.overlaps || [])
                                : (dialog.report.fast || [])
    readonly property int totalRows: dialog.section === 0
                                     ? Number(dialog.report.overlapCount || 0)
                                     : Number(dialog.report.fastCount || 0)

    // The cap in the panel, so the bar opens on whatever the last run used. The panel
    // owns the persisted value; this only shows it and asks for a new one.
    property real maxSpeed: 1.0
    property real cap: 1.0

    // The silence left between one voice and the next. Same arrangement: the panel
    // stores it, this asks. The voices are laid out in order - each waits for the line
    // before it to finish, so none is heard over another - and this gap is the pause
    // left between them; a larger value pushes the whole chain later.
    property int gapMs: 0
    property int gap: 0

    // What the user picked here, for the panel to store and act on. Rebuild closes the
    // dialog - the progress belongs in the panel, where the status line and the Cancel
    // button already are. Fixing positions does not: it takes no time and the counts
    // above are worth watching drop.
    signal rebuildRequested(real speed, int gapMs)
    signal fitRequested(int gapMs)

    dialogTitle: qsTr("Generated voice report")
    dialogWidth: 760
    dialogHeight: 700

    function refresh() {
        dialog.report = Backend.generatedSpeechReport()
    }

    function open() {
        dialog.refresh()
        dialog.cap = dialog.maxSpeed
        dialog.gap = dialog.gapMs
        openNative()
    }

    // How many lines need more room than this limit gives them. A tail sum over the
    // histogram, which is the reason the buckets are laid out the way they are - see
    // speech_report.h. Exact for a project of any size, unlike the row lists, which
    // stop at 400.
    function unfittedAt(speed) {
        var buckets = dialog.report.speedBuckets || []
        var base = Number(dialog.report.speedBucketBase || 1.0)
        var step = Number(dialog.report.speedBucketStep || 0.05)
        if (buckets.length === 0 || step <= 0)
            return 0
        var first = Math.max(0, Math.round((Number(speed) - base) / step))
        var total = 0
        for (var i = first; i < buckets.length; ++i)
            total += Number(buckets[i] || 0)
        return total
    }

    // A line ends up above the comfort threshold only if it needed to and the limit
    // let it, so this is the same tail sum taken at the threshold itself.
    function rushedAt(speed) {
        var limit = Number(dialog.report.rushedSpeed || 1.3)
        return Number(speed) > limit ? dialog.unfittedAt(limit) : 0
    }

    // The lowest limit that fits every line, rounded up to a whole bucket so the
    // number shown is the number the tail sum answers for. Clamped to 3.0 because
    // that is as fast as the worker will speak; when the sequence needs more than
    // that, the readout says how many lines are left over rather than pretending.
    readonly property real fitAllCap: {
        var step = Number(dialog.report.speedBucketStep || 0.05)
        var worst = Number(dialog.report.maxRequiredSpeed || 1)
        if (!(worst > 1.0) || step <= 0)
            return 1.0
        return Math.min(3.0, Math.ceil(worst / step - 1e-9) * step)
    }

    readonly property var capPresets: {
        var values = [1.0, 1.15, 1.3, 1.5, 2.0]
        var result = []
        for (var i = 0; i < values.length; ++i) {
            if (values[i] > dialog.fitAllCap + 0.001)
                continue
            result.push({ value: values[i],
                          label: values[i] <= 1.001 ? qsTr("Natural")
                                                    : values[i].toFixed(2) + "×" })
        }
        if (dialog.fitAllCap > 1.001
                && Math.abs(dialog.fitAllCap - values[values.length - 1]) > 0.001)
            result.push({ value: dialog.fitAllCap,
                          label: (dialog.unfittedAt(dialog.fitAllCap) === 0
                                  ? "Fit all " : "Fastest ")
                                 + dialog.fitAllCap.toFixed(2) + "×" })
        return result
    }

    // The silence left between one voice and the next in the chain. Zero butts them end
    // to end; a little more leaves a breath between every line and pushes the chain's end
    // later.
    readonly property var gapPresets: [
        { value: 0, label: qsTr("Back to back") },
        { value: 100, label: "0.1s" },
        { value: 200, label: "0.2s" },
        { value: 400, label: "0.4s" }
    ]

    // What the last fit pass did, so the button reports without stealing the dialog.
    property string fitStatus: ""

    function runFit() {
        var result = Backend.chainGeneratedSpeech(dialog.gap)
        if (result.busy) {
            dialog.fitStatus = qsTr("Still adding voices to the timeline - try again in a moment.")
            return
        }
        var moved = Number(result.moved || 0)
        var tracks = result.tracks || []
        var where = tracks.length > 0 ? tracks.join(", ") : "A1"
        var rows = Number(result.rows || 1)
        var spread = rows > 1 ? " · across " + rows + " rows" : ""
        if (moved <= 0) {
            dialog.fitStatus = "Voices already in order (" + where + ")" + spread
        } else {
            dialog.fitStatus = "Placed " + moved + " voices in order onto " + where + spread
        }
        dialog.fitRequested(dialog.gap)
        dialog.refresh()
    }

    function timecode(ms) {
        var total = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        var h = Math.floor(total / 3600)
        var m = Math.floor(total / 60) % 60
        var s = total % 60
        function pad(v) { return v < 10 ? "0" + v : String(v) }
        return pad(h) + ":" + pad(m) + ":" + pad(s)
    }

    function seconds(ms) {
        return (Math.max(0, Number(ms || 0)) / 1000).toFixed(2) + "s"
    }

    function jumpTo(row) {
        if (!row)
            return
        Backend.playheadMs = Number(row.startMs || 0)
        Backend.selectedClipId = String(row.clipId || "")
    }

    // A slot of 0 means nothing follows this line, so it had the rest of the
    // sequence to talk in and only landed on the list by being on top of a clip
    // that was already there.
    function gapText(row) {
        return Number(row.slotMs || 0) > 0 ? dialog.seconds(row.slotMs)
                                           : qsTr("the end of the sequence")
    }

    function rowDetail(row) {
        if (!row)
            return ""
        if (dialog.section === 0) {
            var parts = []
            if (row.otherTrack !== undefined)
                parts.push("over " + row.otherTrack + " #" + row.otherIndex)
            var voices = Number(row.concurrent || 0)
            if (voices > 2)
                parts.push(voices + " voices at once")
            parts.push("line " + dialog.seconds(row.audioMs) + ", gap "
                       + dialog.gapText(row))
            return parts.join("  ·  ")
        }
        var over = Number(row.overrunMs || 0)
        return "line " + dialog.seconds(row.audioMs) + ", gap " + dialog.gapText(row)
                + (over > 0 ? "  ·  " + dialog.seconds(over) + " too long" : "")
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.translationDialogSurface
        border.color: Theme.translationRowBorder
        border.width: 1
        radius: Theme.translationDialogRadius
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 14
            Layout.bottomMargin: 12
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: {
                        var voices = Number(dialog.report.voiceClips || 0)
                        if (voices <= 0)
                            return qsTr("No generated voice clips on the timeline.")
                        var tracks = (dialog.report.tracks || []).join(", ")
                        var worst = Number(dialog.report.maxRequiredSpeed || 1)
                        var applied = Number(dialog.report.appliedSpeed || 1)
                        return voices + " voice clips on " + tracks
                                + "  ·  worst line needs " + worst.toFixed(2)
                                + "× speed to fit its gap"
                                + (applied > 1.001
                                   ? "  ·  built at up to " + applied.toFixed(2) + "×"
                                   : "")
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                }
                InfoTip {
                    Layout.alignment: Qt.AlignTop
                    text: qsTr("Click a row to move the playhead there and select the clip.")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: [
                        { key: 0, title: qsTr("Overlapping voices"),
                          count: Number(dialog.report.overlapCount || 0) },
                        { key: 1, title: qsTr("Speaks too fast to fit"),
                          count: Number(dialog.report.fastCount || 0) }
                    ]
                    delegate: Rectangle {
                        id: tab
                        required property var modelData
                        readonly property bool active: dialog.section === tab.modelData.key
                        Layout.preferredHeight: 30
                        implicitWidth: tabLabel.implicitWidth + 26
                        color: tab.active ? Theme.translationRowHover
                                          : (tabHover.hovered ? Theme.hover : Theme.translationRow)
                        border.width: 1
                        border.color: tab.active ? Theme.accent : Theme.translationRowBorder
                        radius: Theme.translationRowRadius
                        HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: dialog.section = tab.modelData.key }
                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: tab.modelData.title + "  " + tab.modelData.count
                            color: tab.active ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fsSm
                            font.weight: tab.active ? Font.DemiBold : Font.Normal
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: Number(dialog.report.retimedCount || 0) > 0
                    text: {
                        var rushed = Number(dialog.report.rushedCount || 0)
                        var limit = Number(dialog.report.rushedSpeed || 1.3)
                        return dialog.report.retimedCount + " lines sped up to fit"
                                + (rushed > 0 ? ", " + rushed + " past "
                                                + limit.toFixed(2) + "×" : "")
                    }
                    color: "#e8b787"
                    font.pixelSize: Theme.fsXs
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bgPrimary
                border.color: Theme.translationRowBorder
                border.width: 1
                radius: Theme.translationRowRadius

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: dialog.rows.length === 0
                    text: Number(dialog.report.voiceClips || 0) <= 0
                          ? qsTr("Generate a timed voiceover first.")
                          : (dialog.section === 0
                             ? qsTr("No voice plays over another voice.")
                             : qsTr("Every line fits the gap before the next one at its natural pace."))
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                }

                ListView {
                    id: rowList
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 4
                    visible: dialog.rows.length > 0
                    model: dialog.rows
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        id: reportRow
                        required property var modelData
                        width: rowList.width - 12
                        height: 54
                        color: rowHover.hovered ? Theme.translationRowHover : Theme.translationRow
                        border.width: 1
                        border.color: rowHover.hovered ? Theme.accent : Theme.translationRowBorder
                        radius: Theme.translationRowRadius
                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: dialog.jumpTo(reportRow.modelData) }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            ColumnLayout {
                                spacing: 2
                                Text {
                                    text: dialog.timecode(reportRow.modelData.startMs)
                                    color: Theme.accent
                                    font.pixelSize: Theme.fsSm
                                }
                                Text {
                                    text: reportRow.modelData.track + "  #"
                                          + reportRow.modelData.index
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fsXs
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: String(reportRow.modelData.text || qsTr("(no text)"))
                                    color: Theme.textPrimary
                                    font.family: "Khmer OS System"
                                    font.pixelSize: Theme.fsSm
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: dialog.rowDetail(reportRow.modelData)
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fsXs
                                    elide: Text.ElideRight
                                }
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                horizontalAlignment: Text.AlignRight
                                text: dialog.section === 0
                                      ? dialog.seconds(reportRow.modelData.overlapMs)
                                      : Number(reportRow.modelData.requiredSpeed || 1).toFixed(2) + "×"
                                color: "#e88787"
                                font.pixelSize: Theme.fsMd
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: dialog.totalRows > dialog.rows.length
                wrapMode: Text.WordWrap
                text: qsTr("Showing the worst %1 of %2.").arg(dialog.rows.length).arg(dialog.totalRows)
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
            }

            // The two lists above say what is wrong; this bar is where it gets fixed,
            // by the two independent levers that exist.
            //
            // "Fix positions" re-lays the voices in order: each one waits for the line
            // before it to finish, so none is heard over another, and they are dealt
            // across rows as their subtitles crowd. It is what live placement builds as
            // the cues arrive, so it normally reports nothing left to move - it is here
            // for voices that were trimmed, dragged, or made by an older build. Nothing
            // is re-encoded, so it is instant, undoable, and needs no regenerate.
            //
            // "Rebuild voices" changes how fast they are spoken. It re-runs the same
            // generate with a speed limit, and every line is then sped up by exactly
            // what it needs and no more - a line that only just overran is barely
            // touched, the worst one takes the whole limit. Nothing is re-synthesised
            // either: the natural voice files are keyed by their text, so this is an
            // ffmpeg pass.
            //
            // Together they are the whole answer: placement puts the voices in order so
            // none is heard over another, and the speed limit shortens them so the chain
            // ends before the picture does.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: rebuildBody.implicitHeight + 20
                color: Theme.translationRow
                border.width: 1
                border.color: Theme.translationRowBorder
                radius: Theme.translationRowRadius
                visible: Number(dialog.report.voiceClips || 0) > 0

                ColumnLayout {
                    id: rebuildBody
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Leave between voices")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fsSm
                        }
                        Repeater {
                            model: dialog.gapPresets
                            delegate: Rectangle {
                                id: gapChip
                                required property var modelData
                                readonly property bool active:
                                    dialog.gap === gapChip.modelData.value
                                Layout.preferredHeight: 28
                                implicitWidth: gapLabel.implicitWidth + 22
                                color: gapChip.active ? Theme.translationRowHover
                                                      : (gapHover.hovered ? Theme.hover
                                                                          : Theme.bgPrimary)
                                border.width: 1
                                border.color: gapChip.active ? Theme.accent
                                                             : Theme.translationRowBorder
                                radius: Theme.translationRowRadius
                                HoverHandler { id: gapHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: dialog.gap = gapChip.modelData.value }
                                Text {
                                    id: gapLabel
                                    anchors.centerIn: parent
                                    text: gapChip.modelData.label
                                    color: gapChip.active ? Theme.textPrimary
                                                          : Theme.textSecondary
                                    font.pixelSize: Theme.fsSm
                                    font.weight: gapChip.active ? Font.DemiBold : Font.Normal
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        // Stays open on purpose: the counts above are recounted straight
                        // after, so the tabs reflect the new layout.
                        Button {
                            text: qsTr("Fix positions")
                            implicitWidth: 124
                            implicitHeight: 30
                            enabled: !Backend.timelineImportActive
                            onClicked: dialog.runFit()
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? "white" : Theme.textMuted
                                font.pixelSize: Theme.fsSm
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: !parent.enabled ? Theme.bgPrimary
                                                       : (parent.down ? Theme.hover : Theme.accent)
                                border.color: parent.enabled ? Theme.accent : Theme.textMuted
                                radius: height / 2
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: dialog.fitStatus !== ""
                              ? dialog.fitStatus
                              : "Each voice waits for the line before it to finish, so no "
                                + "two are ever heard at once, and they are spread across "
                                + "audio rows"
                                + (dialog.gap > 0
                                   ? " with " + (dialog.gap / 1000).toFixed(1)
                                     + "s of silence between them"
                                   : "")
                                + ". Instant and undoable - no audio is re-encoded."
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsXs
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: Theme.translationRowBorder
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Speak up to")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fsSm
                        }
                        Repeater {
                            model: dialog.capPresets
                            delegate: Rectangle {
                                id: capChip
                                required property var modelData
                                readonly property bool active:
                                    Math.abs(dialog.cap - capChip.modelData.value) < 0.001
                                Layout.preferredHeight: 28
                                implicitWidth: capLabel.implicitWidth + 22
                                color: capChip.active ? Theme.translationRowHover
                                                      : (capHover.hovered ? Theme.hover
                                                                          : Theme.bgPrimary)
                                border.width: 1
                                border.color: capChip.active ? Theme.accent
                                                             : Theme.translationRowBorder
                                radius: Theme.translationRowRadius
                                HoverHandler { id: capHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: dialog.cap = capChip.modelData.value }
                                Text {
                                    id: capLabel
                                    anchors.centerIn: parent
                                    text: capChip.modelData.label
                                    color: capChip.active ? Theme.textPrimary
                                                          : Theme.textSecondary
                                    font.pixelSize: Theme.fsSm
                                    font.weight: capChip.active ? Font.DemiBold : Font.Normal
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: qsTr("Rebuild voices")
                            implicitWidth: 124
                            implicitHeight: 30
                            enabled: !Backend.textToSpeechEngine.inProgress
                            onClicked: {
                                // The gap goes with it: the panel re-lays the track once
                                // the new voices have all landed, so a rebuild is both
                                // levers in one press.
                                dialog.rebuildRequested(dialog.cap, dialog.gap)
                                dialog.close()
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? "white" : Theme.textMuted
                                font.pixelSize: Theme.fsSm
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: !parent.enabled ? Theme.bgPrimary
                                                       : (parent.down ? Theme.hover : Theme.accent)
                                border.color: parent.enabled ? Theme.accent : Theme.textMuted
                                radius: height / 2
                            }
                        }
                    }

                    // Counted over every clip, not over the 400 rows above, so the
                    // number is right for a 20 000-cue film.
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: {
                            var left = dialog.unfittedAt(dialog.cap)
                            var rushed = dialog.rushedAt(dialog.cap)
                            var limit = Number(dialog.report.rushedSpeed || 1.3)
                            var tail = "  Those make the line after them wait instead of "
                                       + "playing over it."
                            if (dialog.cap <= 1.001)
                                return "Natural pace: nothing is sped up, and "
                                        + left + " lines run past the next one." + tail
                            var head = left === 0
                                       ? "Every line fits: the whole voiceover rebuilds onto one track."
                                       : left + " lines still need more room than the limit gives them."
                                         + tail
                            return head
                                    + (rushed > 0
                                       ? "  " + rushed + " lines end up above "
                                         + limit.toFixed(2) + "×, which sounds hurried."
                                       : "  Nothing ends up above " + limit.toFixed(2) + "×.")
                        }
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsXs
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: Theme.bgSidebar
            radius: Theme.translationDialogRadius
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Refresh")
                    implicitWidth: 82
                    implicitHeight: 32
                    onClicked: dialog.refresh()
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.down ? Theme.hover : Theme.bgPrimary
                        border.color: Theme.textMuted
                        radius: height / 2
                    }
                }
                Button {
                    text: qsTr("Close")
                    implicitWidth: 82
                    implicitHeight: 32
                    onClicked: dialog.close()
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: Theme.fsSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.down ? Theme.hover : Theme.accent
                        border.color: Theme.accent
                        radius: height / 2
                    }
                }
            }
        }
    }
}
