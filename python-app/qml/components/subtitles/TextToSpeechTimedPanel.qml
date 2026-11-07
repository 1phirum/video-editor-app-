pragma ComponentBehavior: Bound
// qmllint disable
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

Rectangle {
    id: root
    color: Theme.bgPanel

    // The window a modal opened from here is transient for. Passed down from
    // TextPanel, which is the only thing that knows it.
    property var ownerWindow: null

    // Restored from the database rather than defaulted, because a dub is not a
    // one-off: someone voicing a Khmer track picks Khmer and male once and then
    // generates all afternoon, and re-picking both on every launch is the whole
    // of the annoyance. Written through on change, not on generate, so the
    // choice survives even if the run is never started.
    property string language: String(Backend.uiStateValue("tts/language", "en"))
    property string gender: String(Backend.uiStateValue("tts/gender", "female"))

    // Languages a neural voice is configured for in text_to_speech.py (VOICES).
    // Codes are the base language the worker keys on; it strips any region suffix.
    readonly property var languageLabels: ["English", "Chinese", "Khmer", "Spanish",
                                            "French", "German", "Japanese", "Korean",
                                            "Vietnamese", "Thai", "Indonesian",
                                            "Portuguese", "Russian", "Italian",
                                            "Hindi", "Arabic"]
    readonly property var languageCodes: ["en", "zh", "km", "es", "fr", "de", "ja",
                                           "ko", "vi", "th", "id", "pt", "ru", "it",
                                           "hi", "ar"]
    // How much a line may be sped up so it ends before the next one starts. 1.0 - the
    // default - never touches the voice: a line that runs long keeps its full length,
    // and the line after it waits instead of starting on top of it. Above 1.0 each line
    // is sped up by only what it needs, up to this, so fewer of them have to wait.
    property real maxSpeed: Number(Backend.uiStateValue("tts/maxSpeed", 1.0))

    // A uniform pace for every generated voice. 1.0 (Natural) means off - the
    // Speed limit above decides each line instead. Above 1.0, every line is
    // spoken at exactly this rate: set 1.15 and the whole dub is 1.15x. Range
    // 1.00 - 2.00.
    property real fixedSpeed: Number(Backend.uiStateValue("tts/fixedSpeed", 1.0))

    // The silence left between one voice and the next. The voices are laid out the way
    // a person would by hand: each one waits at its subtitle until the line before it
    // has finished, so two are never heard at once, and they are spread across audio
    // rows (A1, A2, ...) as their subtitles crowd. Zero butts them end to end; a larger
    // value leaves a breath between every line and pushes the whole chain, and its end,
    // later.
    //
    // A long line makes every later line wait, so the dub can end after the picture.
    // That is on purpose: it is pulled back with the Speed limit above, or Fit to video
    // in the voice report - placement here never re-encodes.
    property int gapMs: Number(Backend.uiStateValue("tts/gapMs", 0))

    readonly property var speedValues: [1.0, 1.15, 1.3, 1.5, 2.0, 3.0]
    readonly property var speedLabels: ["Natural", "Up to 1.15×", "Up to 1.30×",
                                        "Up to 1.50×", "Up to 2.00×", "Up to 3.00×"]

    readonly property var gapValues: [0, 100, 200, 400]
    readonly property var gapLabels: ["Back to back", "0.1s apart", "0.2s apart",
                                      "0.4s apart"]

    function gapIndex() {
        var best = 0
        for (var i = 1; i < root.gapValues.length; ++i) {
            if (Math.abs(root.gapValues[i] - root.gapMs)
                    < Math.abs(root.gapValues[best] - root.gapMs))
                best = i
        }
        return best
    }

    function speedIndex() {
        var best = 0
        for (var i = 1; i < root.speedValues.length; ++i) {
            if (Math.abs(root.speedValues[i] - root.maxSpeed)
                    < Math.abs(root.speedValues[best] - root.maxSpeed))
                best = i
        }
        return best
    }

    function setLanguage(code) {
        if (root.language === code)
            return
        root.language = code
        Backend.setUiState("tts/language", code)
    }

    function setGender(value) {
        if (root.gender === value)
            return
        root.gender = value
        Backend.setUiState("tts/gender", value)
    }

    function setMaxSpeed(value) {
        var clamped = Math.min(3.0, Math.max(1.0, Number(value) || 1.0))
        if (Math.abs(root.maxSpeed - clamped) < 0.0005)
            return
        root.maxSpeed = clamped
        Backend.setUiState("tts/maxSpeed", clamped)
    }

    function setFixedSpeed(value) {
        var clamped = Math.min(2.0, Math.max(1.0, Number(value) || 1.0))
        if (Math.abs(root.fixedSpeed - clamped) < 0.0005)
            return
        root.fixedSpeed = clamped
        Backend.setUiState("tts/fixedSpeed", clamped)
    }

    function setGapMs(value) {
        var clamped = Math.min(5000, Math.max(0, Math.round(Number(value) || 0)))
        if (root.gapMs === clamped)
            return
        root.gapMs = clamped
        Backend.setUiState("tts/gapMs", clamped)
    }

    // Set while a generate is running so the chain report can be held until the last
    // voice has actually landed on a track.
    property bool fitAfterGeneration: false
    property string fitStatus: ""
    // How far the finished dub runs past the last frame of video, and the speed limit
    // that would pull it back inside. Filled in by applyFit(); an empty limit means
    // there is nothing to fix.
    property real pictureLimit: 0
    property string overrunStatus: ""

    function clockText(ms) {
        var total = Math.max(0, Math.round(Number(ms || 0) / 1000))
        var minutes = Math.floor(total / 60)
        var seconds = total % 60
        return minutes + "m " + (seconds < 10 ? "0" : "") + seconds + "s"
    }

    // Re-lays the generated voices in cue order into the sequential chain: each one
    // waits for the line before it to finish, so none is heard over another, and they
    // are dealt across rows as their subtitles crowd. Live placement drops them near
    // their cues as they arrive and the backend lays the real chain the moment the batch
    // lands - the cues come out of the worker in the order it finishes them, not the
    // order they are heard - so this normally finds nothing left to move. It is here for
    // the voices already on a timeline: a manual trim, an undo, or a project made before
    // this existed.
    function applyFit() {
        var result = Backend.chainGeneratedSpeech(root.gapMs)
        if (result.busy)
            return false
        var moved = Number(result.moved || 0)
        var tracks = result.tracks || []
        var where = tracks.length > 0 ? tracks.join(", ") : "A1"
        var rows = Number(result.rows || 1)
        var spread = rows > 1 ? " · across " + rows + " rows" : ""
        if (Number(result.voiceClips || 0) === 0) {
            // A cancelled run also ends the import, and it takes its voices with it.
            root.fitStatus = ""
        } else if (moved <= 0) {
            root.fitStatus = "Voices already in order on " + where + spread
        } else {
            root.fitStatus = "Placed " + moved + " voices in order onto " + where + spread
        }
        root.refreshOverrun(Number(result.overrunMs || 0))
        return true
    }

    // Why the dub can end after the picture does, and what to do about it. A line that
    // takes longer to say than the room its subtitle has pushes every later line back,
    // and that debt only comes off at a pause wide enough to absorb it. Over a couple of
    // thousand cues it accumulates, and the last voice can land well past the last
    // frame. Speeding the lines up is the only fix - the words are the words - so the
    // question worth answering is by how much, and that is arithmetic over the lengths
    // already on the clips rather than something to guess at with the combo box.
    function refreshOverrun(overrunMs) {
        if (Number(overrunMs || 0) <= 0) {
            root.pictureLimit = 0
            root.overrunStatus = ""
            return
        }
        var fit = Backend.speechSpeedForPicture()
        var over = root.clockText(overrunMs)
        if (fit.fits) {
            root.pictureLimit = Number(fit.limit || 0)
            root.overrunStatus = "The dub runs " + over + " past the end of the video. "
                    + "There is more speech than picture at the current limit - "
                    + "regenerate at up to " + root.pictureLimit.toFixed(2)
                    + "× and it ends inside the film."
        } else {
            root.pictureLimit = 0
            root.overrunStatus = "The dub runs " + over + " past the end of the video, "
                    + "and even 3.00× is not enough to pull it back. The transcript has "
                    + "more speech in it than this film has room for: shorten the lines, "
                    + "or accept that the voiceover outlasts the picture."
        }
    }

    // Regenerates at the limit that makes the dub end inside the picture. Every line is
    // re-synthesized and re-timed, because there is no way to make audio that already
    // exists shorter without re-encoding it - which is exactly what the worker's atempo
    // pass does.
    function fitToPicture() {
        if (root.pictureLimit <= 1.0)
            return
        root.setMaxSpeed(root.pictureLimit)
        root.startGeneration(true)
    }

    // One entry point for both buttons, so a rebuild is exactly a regenerate with a
    // different limit - which is all it is. The previous batch is removed before the
    // new one is placed, so there is nothing to clean up first and no way to end up
    // with two voiceovers on the timeline. `fitAfter` is what makes a rebuild land
    // tidy: the fit runs itself once placement reports it has finished.
    function startGeneration(fitAfter) {
        root.fitStatus = ""
        root.overrunStatus = ""
        root.pictureLimit = 0
        root.fitAfterGeneration = fitAfter === true
        Backend.generateTimedTextToSpeech(root.speechSegments(), root.language,
                                          root.gender, root.maxSpeed,
                                          root.fixedSpeed)
    }

    // Where the lines come from. The transcript is the source of truth and it
    // exists the moment something has been transcribed or an SRT imported, while
    // subtitle clips only exist once someone has dragged them onto a track -
    // there is no reason to make a dub wait for that. When both exist they hold
    // the same text anyway: the sequence transcript is a view over those clips.
    readonly property bool fromTranscript: Backend.transcriptCount > 0
    readonly property int segmentCount: root.fromTranscript ? Backend.transcriptCount
                                                            : root.segments.length

    // The two sources spell a cue differently - the transcript keeps seconds in
    // start/end, a timeline clip keeps milliseconds in startMs/durationMs - so the
    // list reads them through these rather than through two delegates.
    function rowStartMs(item) {
        if (!item)
            return 0
        return item.startMs !== undefined ? Number(item.startMs)
                                          : Math.round(Number(item.start || 0) * 1000)
    }

    function rowText(item) {
        return item ? String(item.text || "") : ""
    }

    function subtitleSegments() {
        var values = []
        var source = Backend.clips || []
        for (var i = 0; i < source.length; ++i) {
            var clip = source[i]
            if (!clip || String(clip.kind || "") !== "subtitle")
                continue
            var text = String(clip.text || "").trim()
            var start = Number(clip.startMs || 0)
            var end = start + Number(clip.durationMs || 0)
            if (text.length > 0 && end > start)
                values.push({ text: text, startMs: start, endMs: end, index: i })
        }
        values.sort(function(a, b) { return a.startMs - b.startMs })
        return values
    }

    // Transcript cues in the shape the worker reads. Built on Generate and not
    // held: turning 19 831 entries into a QVariantList is the freeze the note
    // below is about, and an edit should not pay for it - the list on screen is
    // bound straight to Backend.transcript instead.
    function transcriptSegments() {
        var values = []
        var source = Backend.transcript || []
        for (var i = 0; i < source.length; ++i) {
            var cue = source[i]
            var text = String((cue && cue.text) || "").trim()
            var startMs = Math.round(Number((cue && cue.start) || 0) * 1000)
            var endMs = Math.round(Number((cue && cue.end) || 0) * 1000)
            if (text.length > 0 && endMs > startMs)
                values.push({ text: text, startMs: startMs, endMs: endMs, index: i })
        }
        values.sort(function(a, b) { return a.startMs - b.startMs })
        return values
    }

    function speechSegments() {
        return root.fromTranscript ? root.transcriptSegments() : root.segments
    }

    function timecode(ms) {
        var total = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        var h = Math.floor(total / 3600)
        var m = Math.floor(total / 60) % 60
        var s = total % 60
        function pad(v) { return v < 10 ? "0" + v : String(v) }
        return pad(h) + ":" + pad(m) + ":" + pad(s)
    }

    // Only ever recomputed while this tab is the one on screen. It used to be
    // rebuilt from every clipsChanged, visible or not, and a twenty-thousand-cue
    // subtitle track turned that into a ~1 s freeze on every timeline edit: the
    // JS array has to be converted back into a QVariantList to become a
    // ListView model, one QVariantMap per entry.
    property var segments: []
    property bool segmentsStale: true

    function refreshSegments() {
        // Nothing to build while the transcript is the source: the list is bound
        // to Backend.transcript directly, so a transcript edit reaches the UI
        // without a twenty-thousand-entry conversion behind it.
        if (root.fromTranscript) {
            root.segmentsStale = false
            if (root.segments.length > 0)
                root.segments = []
            return
        }
        if (!root.visible) {
            root.segmentsStale = true
            return
        }
        root.segmentsStale = false
        root.segments = root.subtitleSegments()
    }

    onVisibleChanged: if (root.visible && root.segmentsStale) root.refreshSegments()
    // The transcript being emptied - a failed transcription, a cleared project -
    // hands the panel back to the timeline clips, which have to be gathered.
    onFromTranscriptChanged: root.refreshSegments()
    Component.onCompleted: root.refreshSegments()

    // Coalesced: a single edit can emit clipsChanged more than once, and an
    // import emits it once for the whole batch either way.
    Timer {
        id: segmentsTimer
        interval: 60
        repeat: false
        onTriggered: root.refreshSegments()
    }

    Connections {
        target: Backend
        function onClipsChanged() {
            // A transcript-sourced list has nothing to rebuild, so a timeline edit
            // while one exists costs this panel nothing at all.
            if (root.fromTranscript)
                return
            if (root.visible)
                segmentsTimer.restart()
            else
                root.segmentsStale = true
        }

        // Placement drains the generated cues over many event-loop turns, so the engine
        // finishing is not the same as the voices being on the timeline. This is the
        // flag that goes false when the last one has landed. The backend has chained
        // them in cue order by the time it clears, so what runs below is the report -
        // and the safety net if a clip was touched in between.
        function onTimelineImportActiveChanged() {
            if (Backend.timelineImportActive || !root.fitAfterGeneration)
                return
            root.fitAfterGeneration = false
            // One turn later: clipsChanged from the final batch is still settling, and
            // the fit is an undo step of its own - it should not land inside that.
            fitTimer.restart()
        }
    }

    Timer {
        id: fitTimer
        interval: 120
        repeat: false
        onTriggered: root.applyFit()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // Two pairs per row. Four stacked full-width combo boxes pushed the cue list
        // and the buttons off a short panel; the labels are one word each, so the row
        // has the room.
        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 10
            rowSpacing: 8
            Text { text: qsTr("Language"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            WhisperComboBox {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                model: root.languageLabels
                currentIndex: Math.max(0, root.languageCodes.indexOf(root.language))
                onActivated: root.setLanguage(root.languageCodes[currentIndex])
            }
            Text { text: qsTr("Voice"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            WhisperComboBox {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                model: ["Female", "Male"]
                currentIndex: root.gender === "male" ? 1 : 0
                onActivated: root.setGender(currentIndex === 1 ? "male" : "female")
            }
            Text {
                text: qsTr("Speed limit")
                color: root.fixedSpeed > 1.001 ? Theme.textMuted : Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }
            WhisperComboBox {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                // The fixed Voice speed below overrides the auto-fit limit, so it is
                // greyed out while a fixed speed is set.
                enabled: root.fixedSpeed <= 1.001
                opacity: enabled ? 1.0 : 0.5
                model: root.speedLabels
                currentIndex: root.speedIndex()
                onActivated: root.setMaxSpeed(root.speedValues[currentIndex])
            }
            Text { text: qsTr("Voice gap"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            WhisperComboBox {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                model: root.gapLabels
                currentIndex: root.gapIndex()
                onActivated: root.setGapMs(root.gapValues[currentIndex])
            }
            Text { text: qsTr("Voice speed"); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            RowLayout {
                Layout.fillWidth: true
                Layout.columnSpan: 3
                spacing: 10
                Slider {
                    id: fixedSpeedSlider
                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    from: 1.0
                    to: 2.0
                    stepSize: 0.05
                    value: root.fixedSpeed
                    onMoved: root.setFixedSpeed(value)
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    background: Rectangle {
                        x: fixedSpeedSlider.leftPadding
                        y: fixedSpeedSlider.topPadding
                           + fixedSpeedSlider.availableHeight / 2 - height / 2
                        width: fixedSpeedSlider.availableWidth
                        height: 2
                        radius: 1
                        color: "#4a4a4a"
                        Rectangle {
                            width: fixedSpeedSlider.visualPosition * parent.width
                            height: parent.height
                            radius: 1
                            color: Theme.accent
                        }
                    }
                    handle: Rectangle {
                        x: fixedSpeedSlider.leftPadding
                           + fixedSpeedSlider.visualPosition
                             * (fixedSpeedSlider.availableWidth - width)
                        y: fixedSpeedSlider.topPadding
                           + fixedSpeedSlider.availableHeight / 2 - height / 2
                        width: 12
                        height: 12
                        radius: 6
                        color: fixedSpeedSlider.pressed ? "white" : Theme.textPrimary
                        border.color: Theme.border
                    }
                }
                Text {
                    Layout.preferredWidth: 96
                    text: root.fixedSpeed <= 1.001
                          ? qsTr("Natural")
                          : root.fixedSpeed.toFixed(2) + qsTr("× all lines")
                    color: root.fixedSpeed > 1.001 ? Theme.accent : Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    horizontalAlignment: Text.AlignRight
                }
            }
            Item { Layout.columnSpan: 4; implicitHeight: 1 }
        }

        Text {
            text: root.segmentCount > 0
                  ? root.segmentCount + (root.fromTranscript
                                         ? qsTr(" transcript segments connected")
                                         : qsTr(" subtitle clips connected"))
                  : qsTr("Transcribe or import subtitles first")
            color: root.segmentCount > 0 ? Theme.textMuted : "#e88787"
            font.pixelSize: Theme.fsSm
        }

        ListView {
            id: subtitleList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // Bound to the backend list rather than to a copy, so an edit or a
            // delete in the Transcript tab shows up here as soon as it lands.
            model: root.fromTranscript ? Backend.transcript : root.segments
            spacing: 4
            delegate: Rectangle {
                id: segmentRow
                required property var modelData
                width: subtitleList.width
                height: 42
                color: rowHover.hovered ? Theme.hover : Theme.bgPrimary
                border.color: rowHover.hovered ? Theme.accent : Theme.border
                radius: Theme.radiusSm
                HoverHandler { id: rowHover }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    Text {
                        text: root.timecode(root.rowStartMs(segmentRow.modelData))
                        color: Theme.accent
                        font.pixelSize: Theme.fsXs
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.rowText(segmentRow.modelData)
                        color: Theme.textPrimary
                        font.family: "Khmer OS System"
                        font.pixelSize: Theme.fsSm
                        elide: Text.ElideRight
                    }
                }
            }
            visible: count > 0
        }

        // The one thing the chain cannot fix by moving clips: there is more speech than
        // film. Shown with the limit that would fix it, and a button that applies it,
        // because working the number out by trying combo entries is a regenerate each.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.overrunStatus !== "" && !Backend.textToSpeechEngine.inProgress
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: root.overrunStatus
                color: "#e0b070"
                font.pixelSize: Theme.fsXs
            }
            LumetriActionButton {
                visible: root.pictureLimit > 1.0
                text: qsTr("Fit to video at ") + root.pictureLimit.toFixed(2) + "×"
                onClicked: root.fitToPicture()
            }
        }

        // What the fit pass did, kept out of the engine's own status line so a rebuild
        // does not overwrite the reason its layout looks the way it does.
        Text {
            Layout.fillWidth: true
            visible: root.fitStatus !== "" && !Backend.textToSpeechEngine.inProgress
            wrapMode: Text.WordWrap
            text: root.fitStatus
            color: Theme.textMuted
            font.pixelSize: Theme.fsXs
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: Backend.textToSpeechEngine.inProgress
                      ? Backend.textToSpeechEngine.status
                      : Backend.textToSpeechEngine.status
                color: Backend.textToSpeechEngine.status.indexOf("failed") >= 0
                       || Backend.textToSpeechEngine.status.indexOf("missing") >= 0
                       ? "#e88787" : Theme.textMuted
                font.pixelSize: Theme.fsXs
                elide: Text.ElideRight
            }
            BusyIndicator {
                running: Backend.textToSpeechEngine.inProgress
                visible: running
                implicitWidth: 22
                implicitHeight: 22
            }
            LumetriActionButton {
                text: qsTr("Voice report")
                // Not while the import is still running: the report is a snapshot,
                // and a snapshot of a half-placed timeline is misleading rather
                // than early.
                enabled: !Backend.textToSpeechEngine.inProgress
                onClicked: speechReportDialog.open()
            }
            LumetriActionButton {
                text: Backend.textToSpeechEngine.inProgress ? qsTr("Cancel") : qsTr("Generate")
                enabled: Backend.textToSpeechEngine.inProgress || root.segmentCount > 0
                // Fits itself when the voices have landed. Generating and then having to
                // ask for the positions to be fixed is the same job in two presses, and
                // the layout it leaves behind in between is the one the user complained
                // about.
                onClicked: Backend.textToSpeechEngine.inProgress
                           ? Backend.textToSpeechEngine.cancel()
                           : root.startGeneration(true)
            }
        }
    }

    SpeechReportDialog {
        id: speechReportDialog
        ownerWindow: root.ownerWindow
        maxSpeed: root.maxSpeed
        gapMs: root.gapMs
        // The dialog picked its limits and closed itself; the rebuild runs here, where
        // the status line and the Cancel button already are - and chains itself once the
        // new voices are on the timeline.
        onRebuildRequested: function(speed, gap) {
            root.setMaxSpeed(speed)
            root.setGapMs(gap)
            if (!Backend.textToSpeechEngine.inProgress && root.segmentCount > 0)
                root.startGeneration(true)
        }
        // Chaining from the dialog is immediate and the dialog stays open to show the
        // counts drop; all the panel takes from it is the gap, so the next generate and
        // the Voice gap box agree with what was just done.
        onFitRequested: function(gap) {
            root.setGapMs(gap)
            root.fitStatus = ""
        }
    }
}
