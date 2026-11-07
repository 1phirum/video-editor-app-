pragma ComponentBehavior: Bound
// qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import CutPro 1.0
import "../theme"
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"

// Mirrors agent-app PropertiesPanel.tsx:
//   tabs = Effect Controls | Lumetri Color | Text  (default Effect Controls)
//   header has the shared "…" overflow plus a SEPARATE theme-toggle (sun)
//   Text tab -> transcript view; other tabs -> "No clip selected" empty state.
Rectangle {
    id: root
    color: Theme.bgPanel

    signal progressJobsChanged(var jobs, string currentId)

    // Restored, not defaulted: the tab this panel opens on is the last one the
    // user was working in. Panel state is deliberately not part of the edit
    // history - reopening a tab is not something undo should reverse - so it goes
    // through setUiState rather than through the project state alone.
    property int subTab: Math.max(0, Math.min(2, Number(Backend.uiStateValue("textPanel/subTab", 0))))
    property string transcriptSearch: ""
    property int editingTranscriptIndex: -1
    // Text-based editing selection. A click sets both to the same row; a
    // shift-click extends the active end, so [selectionMin, selectionMax] is the
    // run of transcript lines whose footage "Remove from video" cuts. -1 = none.
    property int selectionAnchor: -1
    property int selectionActive: -1
    readonly property int selectionMin: Math.min(selectionAnchor, selectionActive)
    readonly property int selectionMax: Math.max(selectionAnchor, selectionActive)
    function transcriptRowSelected(i) {
        return root.selectionAnchor >= 0
                && i >= root.selectionMin && i <= root.selectionMax
    }
    function clearTranscriptSelection() {
        root.selectionAnchor = -1
        root.selectionActive = -1
    }
    // Cut the footage behind the selected transcript run and ripple the gap
    // closed. Clears the selection first because the rows renumber underneath it.
    function removeSelectionFromVideo() {
        if (root.selectionAnchor < 0)
            return
        var a = root.selectionMin
        var b = root.selectionMax
        root.clearTranscriptSelection()
        if (root.editingTranscriptIndex >= a && root.editingTranscriptIndex <= b)
            root.editingTranscriptIndex = -1
        Backend.deleteTranscriptRange(a, b)
    }
    // A hand-placed text overlay is a subtitle clip with no transcript line
    // behind it (transcriptIndex -1). When one is selected the Transcript tab
    // shows a small editor for its words and its timing, so text is edited where
    // it was added rather than only by double-clicking the picture.
    readonly property var selectedTextClip: {
        var c = Backend.selectedClip
        return (c && c.kind === "subtitle"
                && c.transcriptIndex !== undefined && c.transcriptIndex < 0) ? c : null
    }
    property string _textOverlayEditingId: ""
    onSelectedTextClipChanged: {
        // Only reload the field when a different clip becomes selected. Every
        // keystroke emits clipsChanged, which rebuilds this map; reloading then
        // would fight the cursor.
        var newId = selectedTextClip ? String(selectedTextClip.id) : ""
        if (newId !== _textOverlayEditingId) {
            _textOverlayEditingId = newId
            if (selectedTextClip)
                textOverlayField.text = selectedTextClip.text || ""
        }
    }
    function formatTimecode(ms) {
        var t = ms < 0 ? 0 : ms
        var cs = Math.round(t / 10)
        var hundredths = cs % 100
        var totalSeconds = Math.floor(cs / 100)
        var seconds = totalSeconds % 60
        var minutes = Math.floor(totalSeconds / 60)
        function pad(n) { return (n < 10 ? "0" : "") + n }
        return pad(minutes) + ":" + pad(seconds) + "." + pad(hundredths)
    }
    // The engine is chosen by the model name alone. Everything up to "turbo" is a
    // local Whisper checkpoint that has to be downloaded once; "gemini" sends the
    // audio to Google instead and takes the concrete model from Settings >
    // Transcription and the API keys from Settings > Translation. "ocr" reads
    // the burned-in subtitles off the picture with local OCR - no key, no
    // download beyond the bundled models, no audio.
    property var transcriptionModelValues: ["tiny", "base", "small", "medium", "large-v3", "turbo", "gemini", "ocr"]
    property var transcriptionModelLabels: ["tiny", "base", "small", "medium", "large-v3", "turbo", "Gemini AI", "On-screen subtitles (OCR)"]
    property var languageLabels: ["Auto detect", "English", "Chinese", "Khmer", "Spanish", "French", "German", "Japanese", "Korean", "Vietnamese"]
    property var languageCodes: ["auto", "en", "zh", "km", "es", "fr", "de", "ja", "ko", "vi"]

    function setSubTab(index) {
        if (root.subTab === index)
            return
        root.subTab = index
        Backend.setUiState("textPanel/subTab", index)
    }

    // The two selectors below bound currentIndex to appSettings and never wrote
    // back, so a model or a language the user picked was theirs for the session
    // and gone on the next launch. applyAppSettings normalises and saves, and the
    // action log gets the change so the history shows what the run was set up as.
    function rememberTranscriptionSetting(key, value) {
        if (Backend.appSettings[key] === value)
            return
        var next = Object.assign({}, Backend.appSettings)
        next[key] = value
        Backend.applyAppSettings(next)
        Backend.logUiAction("transcription-setting", { "key": key, "value": value })
    }

    function indexOfValue(values, wanted) {
        var index = values.indexOf(wanted)
        return index < 0 ? 0 : index
    }

    property var selectedMediaForTranscription: []
    property var transcriptionSourceMedia: []
    property var transcriptionQueue: []
    property var transcriptionJobs: []
    property string transcriptionCurrentId: ""
    property bool transcriptionBatchMode: false
    property bool timelineTranscriptionMode: false
    onTranscriptionJobsChanged: progressJobsChanged(transcriptionJobs, transcriptionCurrentId)
    onTranscriptionCurrentIdChanged: progressJobsChanged(transcriptionJobs, transcriptionCurrentId)
    property bool clipsExpanded: true
    property string sourceClipSearch: ""
    property var ownerWindow: null

    Connections {
        target: Backend
        function onMediaChanged() {
            root.transcriptionSelectionSync.restart()
        }
        function onClipsChanged() {
            root.transcriptionSelectionSync.restart()
        }
        function onTranscriptionFinished(success, mediaId) {
            if (root.transcriptionBatchMode)
                return // Concurrent jobs already carry their own ID and status.
            if (root.transcriptionCurrentId !== "") {
                var completed = root.transcriptionJobs.slice()
                for (var i = 0; i < completed.length; ++i) {
                    if (completed[i].id === root.transcriptionCurrentId) {
                        var status = Backend.transcriptionStatus || ""
                        completed[i].status = success && status.indexOf("complete") >= 0
                                ? "Complete" : (status.indexOf("cancel") >= 0
                                ? "Cancelled" : "Failed")
                        completed[i].detail = status
                        break
                    }
                }
                root.transcriptionJobs = completed
                root.transcriptionCurrentId = ""
            }
            if ((Backend.transcriptionStatus || "").toLowerCase().indexOf("cancel") >= 0) {
                root.transcriptionQueue = []
            } else if (root.transcriptionQueue.length > 0) {
                var nextId = root.transcriptionQueue.shift()
                Qt.callLater(function() { root.startTranscriptionJob(nextId) })
            }
        }
        function onTranscriptionBatchJobsChanged() {
            if (root.transcriptionBatchMode)
                root.transcriptionJobs = Backend.transcriptionBatchJobs
        }
    }
    
    Component.onCompleted: {
        root.syncTranscriptionSelection()
        if (Backend.transcriptionBatchJobs.length > 0) {
            root.transcriptionBatchMode = true
            root.transcriptionJobs = Backend.transcriptionBatchJobs
        }
    }

    // The transcription list is rebuilt after the timeline stops changing, not
    // during. Importing a generated voice track publishes its progress once per
    // event-loop turn for thousands of turns, and this list is the same list
    // every time until the import ends.
    property alias transcriptionSelectionSync: transcriptionSelectionSyncTimer
    Timer {
        id: transcriptionSelectionSyncTimer
        interval: 120
        repeat: false
        onTriggered: root.syncTranscriptionSelection()
    }

    function timelineSourceIds() {
        var ids = []
        var seen = {}
        var clips = Backend.mediaClips
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            var kind = String(clip.kind || "")
            var mediaId = String(clip.mediaId || "")
            if ((kind === "video" || kind === "audio") && mediaId !== ""
                    && root.isHumanSpeechSource(mediaId) && !seen[mediaId]) {
                seen[mediaId] = true
                ids.push(mediaId)
            }
        }
        return ids
    }

    function isHumanSpeechSource(mediaId) {
        // Answered in C++ from a cached set. This used to scan Backend.media
        // looking for the id, which converted a QVariantMap into a fresh JS
        // object per bin entry - and syncTranscriptionSelection() called it once
        // per bin entry, so a generated voice track made it quadratic.
        return Backend.isTranscribableMedia(mediaId)
    }

    function timelineSourceDuration(mediaId) {
        var total = 0
        // Cues have no media of their own, so this only ever needs the real
        // clips - and on a transcribed timeline that is two entries, not
        // twenty thousand.
        var clips = Backend.mediaClips
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            if (String(clip.mediaId || "") === mediaId
                    && (clip.kind === "video" || clip.kind === "audio"))
                total += Math.max(0, Number(clip.durationMs || 0))
        }
        if (total > 0)
            return total
        var media = Backend.media
        for (var j = 0; j < media.length; ++j)
            if (media[j].id === mediaId)
                return Number(media[j].durationMs || 0)
        return 0
    }

    // Keep the project source entry, id, thumbnail, and backend media path,
    // but expose the duration currently used by the timeline when that media
    // has been trimmed or split.  Backend.media is the original source file;
    // Backend.clips contains the edited timeline instances.
    function timelineAwareSource(media) {
        var source = {}
        for (var key in media)
            source[key] = media[key]

        var mediaId = String(media.id || "")
        var originalDuration = Number(media.durationMs || 0)
        var editedDuration = root.timelineSourceDuration(mediaId)
        if (editedDuration > 0) {
            source.durationMs = editedDuration
            source.timelineDurationMs = editedDuration
            source.sourceDurationMs = originalDuration
            source.timelineEdited = Math.round(editedDuration)
                    !== Math.round(originalDuration)
        } else {
            source.timelineDurationMs = 0
            source.sourceDurationMs = originalDuration
            source.timelineEdited = false
        }
        return source
    }

    function syncTranscriptionSelection() {
        // Project media is the transcription source. A user can transcribe an
        // imported video/audio file without first placing it on the timeline.
        var sources = []
        root.timelineTranscriptionMode = false
        var all = []
        // Pre-filtered in C++: real video and audio only, generated speech
        // already removed. Reading Backend.media here meant walking thousands of
        // voice entries to find the two files a user can actually transcribe.
        var candidates = Backend.transcribableMedia
        for (var i = 0; i < candidates.length; ++i) {
            var media = candidates[i]
            all.push(media.id)
            sources.push(root.timelineAwareSource(media))
        }
        root.transcriptionSourceMedia = sources
        root.selectedMediaForTranscription = all
    }

    function mediaName(mediaId) {
        // The list is read once into a local. `Backend.media.length` in the loop
        // condition re-read the property - and re-converted the list - on every
        // single iteration.
        var media = Backend.media
        for (var i = 0; i < media.length; ++i)
            if (media[i].id === mediaId)
                return media[i].name
        return mediaId
    }

    function mediaDuration(mediaId) {
        var editedDuration = root.timelineSourceDuration(mediaId)
        if (editedDuration > 0)
            return editedDuration
        var media = Backend.media
        for (var i = 0; i < media.length; ++i)
            if (media[i].id === mediaId)
                return Number(media[i].durationMs || 0)
        return 0
    }

    function mediaThumbnail(mediaId) {
        for (var i = 0; i < Backend.media.length; ++i)
            if (Backend.media[i].id === mediaId)
                return Backend.media[i].thumbnailUrl || ""
        return ""
    }

    function updateJob(mediaId, status, detail) {
        var next = transcriptionJobs.slice()
        for (var i = 0; i < next.length; ++i) {
            if (next[i].id === mediaId) {
                next[i].status = status
                next[i].detail = detail || ""
                break
            }
        }
        transcriptionJobs = next
    }

    function startTranscriptionJob(mediaId) {
        transcriptionCurrentId = mediaId
        // Clicking Transcribe is an explicit refresh. Always rerun so changing
        // the model/language can replace an inaccurate cached transcript.
        updateJob(mediaId, "Transcribing", Backend.transcriptionStatus)
        var started = Backend.transcribeMedia(
                    mediaId,
                    root.transcriptionModelValues[modelSelector.currentIndex],
                    languageCodes[languageSelector.currentIndex])
        if (!started) {
            updateJob(mediaId, "Failed", Backend.lastError || "Could not start")
            transcriptionCurrentId = ""
        }
    }

    function startTranscriptionBatch() {
        var ids = selectedMediaForTranscription.slice()
        var model = root.transcriptionModelValues[modelSelector.currentIndex]
        transcriptionBatchMode = ids.length > 1 && (model === "gemini" || model.indexOf("gemini-") === 0)
        var jobs = []
        for (var i = 0; i < ids.length; ++i)
            jobs.push({ id: ids[i], name: mediaName(ids[i]), status: "Queued", detail: "",
                        durationMs: root.mediaDuration(ids[i]),
                        thumbnailUrl: root.mediaThumbnail(ids[i]) })
        transcriptionJobs = jobs
        if (transcriptionBatchMode) {
            transcriptionQueue = []
            transcriptionCurrentId = ""
            if (!Backend.transcribeMediaBatch(ids, model, languageCodes[languageSelector.currentIndex])) {
                for (var index = 0; index < ids.length; ++index)
                    updateJob(ids[index], "Failed", Backend.lastError || "Could not start")
            }
            return
        }
        var firstId = ids.shift()
        transcriptionQueue = ids
        if (firstId)
            startTranscriptionJob(firstId)
    }

    Connections {
        target: Backend
        function onEffectControlsRequested() {
            propTabs.currentIndex = 0
        }
    }

    FileDialog {
        id: importSubtitleDialog
        title: qsTr("Import subtitles")
        fileMode: FileDialog.OpenFile
        // SRT first: it is the normal case and the default the dialog opens on.
        // TTML is what a YouTube caption download actually is, .dfxp and .xml
        // are the same format under the names other tools save it as.
        nameFilters: ["Subtitles (*.srt *.ttml *.dfxp *.xml)",
                      "SubRip subtitles (*.srt)",
                      "YouTube / TTML captions (*.ttml *.dfxp *.xml)",
                      "All files (*)"]
        onAccepted: Backend.importSubtitles(selectedFile.toString())
    }

    FileDialog {
        id: exportSubtitleDialog
        title: qsTr("Export transcript as SRT")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: ["SubRip subtitles (*.srt)"]
        onAccepted: Backend.exportTranscriptSrt(selectedFile.toString())
    }

    FileDialog {
        id: exportSubtitleTtmlDialog
        title: qsTr("Export transcript as TTML")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "ttml"
        nameFilters: ["YouTube / TTML captions (*.ttml)"]
        onAccepted: Backend.exportTranscriptTtml(selectedFile.toString())
    }

    TranscriptTranslationDialog {
        id: translationDialog
        ownerWindow: root.ownerWindow
        onTranslationRequested: languageCode => Backend.translateTranscript(languageCode)
        onTranslationConfiguredRequested: (languageCode, settings) =>
            Backend.translateTranscript(languageCode, settings)
        onProviderTestRequested: settings => Backend.testTranslationProvider(settings)
    }

    RecapDialog {
        id: recapDialog
        ownerWindow: root.ownerWindow
    }

    Menu {
        id: transcriptMenu
        width: 220
        padding: 4

        background: Rectangle {
            color: Theme.menuBg
            border.color: Theme.menuBorder
            radius: Theme.radiusSm
        }

        DarkMenuItem {
            text: qsTr("Import subtitles...")
            onTriggered: importSubtitleDialog.open()
        }
        DarkMenuItem {
            text: qsTr("Export SRT...")
            enabled: Backend.transcriptCount > 0
            onTriggered: exportSubtitleDialog.open()
        }
        DarkMenuItem {
            text: qsTr("Export TTML (YouTube)...")
            enabled: Backend.transcriptCount > 0
            onTriggered: exportSubtitleTtmlDialog.open()
        }
        MenuSeparator {
            contentItem: Rectangle {
                implicitHeight: 1
                color: Theme.menuSeparator
            }
        }
        DarkMenuItem {
            text: Backend.translationInProgress
                  ? qsTr("Cancel translation")
                  : qsTr("Translate transcript...")
            enabled: Backend.transcriptCount > 0
                     && !Backend.transcriptionInProgress
            onTriggered: Backend.translationInProgress
                         ? Backend.cancelTranslation()
                         : translationDialog.open()
        }
        MenuSeparator {
            contentItem: Rectangle {
                implicitHeight: 1
                color: Theme.menuSeparator
            }
        }
        DarkMenuItem {
            text: qsTr("Video recap (Gemini)...")
            enabled: Backend.transcriptCount > 0
                     && !Backend.transcriptionInProgress
                     && !Backend.recapInProgress
            // Just open the panel; the user drives it with Load subtitle /
            // Regenerate. Don't auto-start a run on open.
            onTriggered: recapDialog.open()
        }
    }

    TranscriptContextMenu {
        id: transcriptSegmentMenu
        onEditRequested: (segmentIndex) => {
            root.editingTranscriptIndex = segmentIndex
        }
        onDeleteRequested: (segmentIndex) => {
            // The row is gone by the time the menu closes, so an open editor on it
            // would be editing whichever line moved up into its place.
            if (root.editingTranscriptIndex === segmentIndex)
                root.editingTranscriptIndex = -1
            else if (root.editingTranscriptIndex > segmentIndex)
                root.editingTranscriptIndex -= 1
            Backend.deleteTranscriptSegment(segmentIndex)
        }
        onRemoveFromVideoRequested: (segmentIndex) => {
            // Cut over the whole selected run when the clicked row is part of it;
            // otherwise just this one line. The rows renumber underneath, so clear
            // any editor and the selection before the cut lands.
            var a = segmentIndex
            var b = segmentIndex
            if (root.transcriptRowSelected(segmentIndex)) {
                a = root.selectionMin
                b = root.selectionMax
            }
            if (root.editingTranscriptIndex >= a && root.editingTranscriptIndex <= b)
                root.editingTranscriptIndex = -1
            root.clearTranscriptSelection()
            Backend.deleteTranscriptRange(a, b)
        }
    }

    function timestamp(seconds) {
        // Round to whole milliseconds before splitting the value up, rather than
        // taking the fractional part and flooring it. A cue written as 4.720 is
        // held as 4.71999999999999975, so (value % 1) * 1000 is 719.9999... and
        // flooring it printed 04.719 for a timestamp the file spelled 04.720 -
        // three of the first six cues of a YouTube import were shown a
        // millisecond early.
        var totalMs = Math.round(Math.max(0, Number(seconds) || 0) * 1000)
        var milliseconds = totalMs % 1000
        var totalSeconds = Math.floor(totalMs / 1000)
        var secs = totalSeconds % 60
        var minutes = Math.floor(totalSeconds / 60) % 60
        var hours = Math.floor(totalSeconds / 3600)
        function pad2(number) { return number < 10 ? "0" + number : String(number) }
        function pad3(number) {
            if (number < 10) return "00" + number
            if (number < 100) return "0" + number
            return String(number)
        }
        return pad2(hours) + ":" + pad2(minutes) + ":" + pad2(secs) + "." + pad3(milliseconds)
    }

    function formatDuration(milliseconds) {
        var total = Math.max(0, Math.floor((milliseconds || 0) / 1000))
        var minutes = Math.floor(total / 60)
        var seconds = total % 60
        function pad2(number) { return number < 10 ? "0" + number : String(number) }
        var hours = Math.floor(total / 3600)
        if (hours > 0) {
            minutes = minutes % 60
            return pad2(hours) + ":" + pad2(minutes) + ":" + pad2(seconds)
        }
        return pad2(minutes) + ":" + pad2(seconds)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Header: tabs + theme toggle ("… ☀") --------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.panelTabHeight
            color: Theme.bgSidebar

            RowLayout {
                anchors.fill: parent
                spacing: 0

                PanelTabs {
                    id: propTabs
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    tabs: [qsTr("Effect Controls"), qsTr("Lumetri Color"), qsTr("Text"), qsTr("Text Effects"), qsTr("Scopes")]
                    currentIndex: 0
                    onOverflowClicked: transcriptMenu.popup()
                }

                IconButton {
                    iconName: "sun"
                    boxSize: 24
                    glyphSize: 14
                    Layout.rightMargin: 6
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        // ---- Content -------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // `visible: false` is not "does not exist". A hidden panel is still
            // instantiated, its bindings still re-evaluate, and its delegates are
            // still built - so before this was a Loader, every clip selection
            // rebuilt the Effect Controls grid *and* the Lumetri controls, no
            // matter which tab was open. Backend::setSelectedClipId was measured
            // holding the GUI thread 453-458 ms inside emit selectionChanged()
            // for exactly that reason.
            //
            // active: only the current tab exists. asynchronous: it is incubated
            // over several event-loop turns instead of in one blocking batch, so
            // even the tab being switched to does not freeze the window.
            // The Loader has a size, so it sizes the panel it loads - the panels
            // do not anchor themselves any more.
            Component {
                id: effectControlsTab
                ClipEffectControlsPanel {}
            }
            Component {
                id: lumetriTab
                LumetriColorPanel {}
            }
            Component {
                id: textEffectsTab
                TextEffectsPanel {}
            }
            Component {
                id: scopesTab
                LumetriScopesPanel {}
            }

            Loader {
                anchors.fill: parent
                active: propTabs.currentIndex === 0
                asynchronous: true
                sourceComponent: effectControlsTab
            }

            Loader {
                anchors.fill: parent
                active: propTabs.currentIndex === 1
                asynchronous: true
                sourceComponent: lumetriTab
            }

            Loader {
                anchors.fill: parent
                active: propTabs.currentIndex === 3
                asynchronous: true
                // Incubation creates cards over several frames; reveal only
                // after their positions and caption sizes are complete.
                visible: status === Loader.Ready
                sourceComponent: textEffectsTab
            }

            Loader {
                anchors.fill: parent
                active: propTabs.currentIndex === 4
                asynchronous: true
                sourceComponent: scopesTab
            }

            // Text tab -> transcript view
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: propTabs.currentIndex === 2

                // Secondary tabs (white underline, per reference)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    color: Theme.bgPanel

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 12
                        spacing: 16

                        // Scrollable secondary tab strip: labels keep their full
                        // text and pan sideways when the panel is too narrow,
                        // instead of clipping. The segments count + subtitles
                        // button stay pinned to the right.
                        Flickable {
                            id: subTabsFlick
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: subTabsRow.width
                            contentHeight: height
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true
                            readonly property real maxContentX: Math.max(0, contentWidth - width)

                            Row {
                                id: subTabsRow
                                height: subTabsFlick.height
                                spacing: 16

                                Repeater {
                                    model: [qsTr("Transcript"), qsTr("Captions"), qsTr("Graphics")]
                                    delegate: Item {
                                        id: sub
                                        required property int index
                                        required property string modelData
                                        readonly property bool active: root.subTab === index
                                        height: subTabsRow.height
                                        implicitWidth: subLabel.implicitWidth

                                        Text {
                                            id: subLabel
                                            anchors.centerIn: parent
                                            text: sub.modelData
                                            font.pixelSize: Theme.fsLg
                                            font.weight: sub.active ? Font.DemiBold : Font.Normal
                                            color: sub.active ? Theme.textPrimary : Theme.textMuted
                                        }
                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: subLabel.implicitWidth
                                            height: 2
                                            color: "white"
                                            visible: sub.active
                                        }
                                        TapHandler { cursorShape: Qt.PointingHandCursor; onTapped: root.setSubTab(sub.index) }
                                    }
                                }
                            }
                            WheelHandler {
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                onWheel: (event) => {
                                    const delta = event.angleDelta.y !== 0
                                        ? event.angleDelta.y : event.angleDelta.x
                                    subTabsFlick.contentX = Math.max(
                                        0, Math.min(subTabsFlick.contentX - delta,
                                                    subTabsFlick.maxContentX))
                                }
                            }
                            // Edge fades glued to the visible edges (x tracks
                            // contentX) that show only when there is more strip
                            // hidden that way — the scroll hint. No handlers, so
                            // taps pass through to the tabs.
                            Rectangle {
                                x: subTabsFlick.contentX
                                y: 0
                                width: 16
                                height: subTabsFlick.height
                                visible: subTabsFlick.contentX > 0.5
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: Theme.bgPanel }
                                    GradientStop { position: 1.0; color: "transparent" }
                                }
                            }
                            Rectangle {
                                x: subTabsFlick.contentX + subTabsFlick.width - width
                                y: 0
                                width: 16
                                height: subTabsFlick.height
                                visible: subTabsFlick.contentX < subTabsFlick.maxContentX - 0.5
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: "transparent" }
                                    GradientStop { position: 1.0; color: Theme.bgPanel }
                                }
                            }
                        }

                        Text {
                            id: transcriptSummary
                            visible: root.subTab === 0
                            text: Backend.transcriptionInProgress
                                  ? qsTr("Transcribing")
                                  : Backend.translationInProgress
                                    ? qsTr("Translating %1/%2")
                                        .arg(Backend.translationCompletedSegments)
                                        .arg(Backend.translationTotalSegments)
                                  : Backend.transcriptCount > 0
                                    ? qsTr("%1 segments").arg(Backend.transcriptCount)
                                    : qsTr("No transcript")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsSm
                        }
                        IconButton {
                            visible: root.subTab === 0 && Backend.transcriptCount > 0
                            iconName: "subtitles"
                            boxSize: 24
                            glyphSize: 15
                            hoverEnabled: true
                            active: Backend.hasSubtitleClips
                            restColor: "white"
                            ToolTip.visible: hovered
                            ToolTip.text: Backend.hasSubtitleClips
                                          ? qsTr("Remove subtitles from timeline")
                                          : qsTr("Add subtitles to timeline")
                            onClicked: Backend.hasSubtitleClips
                                       ? Backend.removeTranscriptFromTimeline()
                                       : Backend.addTranscriptToTimeline()
                        }
                        // Opens Text to Speech as a modal popup (replaces the old
                        // inline "Text to Speech" secondary tab). Always available,
                        // regardless of which secondary tab is active, so the Text
                        // panel stays uncluttered.
                        IconButton {
                            iconName: "tts"
                            boxSize: 24
                            glyphSize: 16
                            hoverEnabled: true
                            restColor: "white"
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Text to Speech")
                            onClicked: ttsWindow.openNative()
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.border
                    }
                }

                // Search toolbar
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    visible: root.subTab === 0
                    color: Theme.bgPanel

                    SearchField {
                        id: transcriptSearchField
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: addTextButton.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        placeholder: qsTr("Search transcript...")
                        onTextChanged: root.transcriptSearch = text.toLowerCase()
                    }

                    IconButton {
                        id: addTextButton
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "type"
                        boxSize: 28
                        glyphSize: 16
                        hoverEnabled: true
                        restColor: "white"
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Add a text overlay at the playhead")
                        // A free-standing caption on S1 at the playhead. It draws
                        // in the monitor and burns into the export through the
                        // same subtitle path, and opens straight into editing so
                        // the user can type over the placeholder.
                        onClicked: {
                            var clipId = Backend.addTextClip()
                            if (clipId)
                                Backend.selectedClipId = clipId
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.border
                    }
                }

                // Selected text-overlay editor: appears when a hand-placed text
                // clip is selected, so the words and timing are edited right here.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: textOverlayEditor.implicitHeight + 20
                    visible: root.subTab === 0 && root.selectedTextClip !== null
                    color: Theme.bgPanel

                    ColumnLayout {
                        id: textOverlayEditor
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        anchors.topMargin: 10
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: qsTr("Text overlay")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsSm
                                font.weight: Font.DemiBold
                            }
                            InfoTip {
                                Layout.alignment: Qt.AlignVCenter
                                text: qsTr("Drag the clip on the timeline to move or trim its timing.")
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: root.selectedTextClip
                                      ? root.formatTimecode(root.selectedTextClip.startMs)
                                        + "  →  "
                                        + root.formatTimecode(root.selectedTextClip.startMs
                                                              + root.selectedTextClip.durationMs)
                                      : ""
                                color: Theme.accent
                                font.pixelSize: Theme.fsXs
                            }
                        }

                        // Fixed height on purpose: the field's own text must not
                        // drive the container's size, or every wrapped line would
                        // push the Model/Language rows below it up and down while
                        // typing. Width still binds to the panel (Layout.fillWidth
                        // plus the TextArea tracking availableWidth), so it reflows
                        // on resize but never grows vertically - overflow scrolls
                        // inside instead.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 78
                            color: Theme.bgPrimary
                            radius: Theme.radiusSm
                            border.color: textOverlayField.activeFocus ? Theme.accent : Theme.border

                            ScrollView {
                                id: textOverlayScroll
                                anchors.fill: parent
                                anchors.margins: 6
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                                TextArea {
                                    id: textOverlayField
                                    // Track the viewport width so text wraps to the
                                    // field instead of scrolling sideways.
                                    width: textOverlayScroll.availableWidth
                                    wrapMode: TextArea.Wrap
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fsMd
                                    selectByMouse: true
                                    placeholderText: qsTr("Type your text...")
                                    placeholderTextColor: Theme.textMuted
                                    background: null
                                    // Commit as the user types so the monitor
                                    // updates live.
                                    onTextChanged: if (root.selectedTextClip)
                                                       Backend.updateTextClip(root.selectedTextClip.id, text)
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.border
                    }
                }

                // Transcript body (empty state)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.subTab === 0

                    ColumnLayout {
                        // Referenced by everything below it: the body's top margin
                        // is this header's real height, so a row appearing or
                        // wrapping in here pushes the list down instead of being
                        // drawn over.
                        id: transcriptHeader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 6
                        visible: Backend.hasMedia

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: qsTr("Model"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            WhisperComboBox {
                                id: modelSelector
                                Layout.preferredWidth: 106
                                model: root.transcriptionModelLabels
                                itemStatusIconSource: "../../assets/icons/cloud-download.svg"
                                itemStatusIconVisible: function(modelName, index) {
                                    // The badge means "not on this disk yet", so it
                                    // belongs to the checkpoints only - there is
                                    // nothing to download for Gemini or OCR (its
                                    // models ship bundled).
                                    var value = root.transcriptionModelValues[index]
                                    if (!value || value === "gemini" || value === "ocr")
                                        return false
                                    return Backend.downloadedWhisperModels.indexOf(
                                                String(value)) < 0
                                }
                                currentIndex: root.indexOfValue(
                                                  root.transcriptionModelValues,
                                                  Backend.appSettings.transcriptionModel)
                                onActivated: root.rememberTranscriptionSetting(
                                                 "transcriptionModel",
                                                 root.transcriptionModelValues[currentIndex])
                                popupWidth: 140
                            }
                            Text { text: qsTr("Language"); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            WhisperComboBox {
                                id: languageSelector
                                Layout.fillWidth: true
                                model: root.languageLabels
                                currentIndex: root.indexOfValue(
                                                  root.languageCodes,
                                                  Backend.appSettings.transcriptionLanguage)
                                onActivated: root.rememberTranscriptionSetting(
                                                 "transcriptionLanguage",
                                                 root.languageCodes[currentIndex])
                                popupWidth: 190
                            }
                            Button {
                                HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                implicitHeight: 32
                                implicitWidth: 104
                                padding: 0
                                text: Backend.transcriptionInProgress ? qsTr("Stop") : qsTr("Transcribe")
                                enabled: !Backend.translationInProgress
                                contentItem: Text {
                                    text: parent.text
                                    color: Theme.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: Theme.fsSm
                                }
                                background: Rectangle {
                                    color: parent.down ? Theme.hover : Theme.bgPrimary
                                    border.color: Theme.border
                                    radius: Theme.radiusSm
                                }
                                onClicked: {
                                    if (Backend.transcriptionInProgress) {
                                        root.transcriptionQueue = []
                                        if (root.transcriptionCurrentId !== "")
                                            root.updateJob(root.transcriptionCurrentId,
                                                           "Cancelled", "Cancelled by user")
                                        Backend.cancelTranscription()
                                    } else {
                                        if (root.selectedMediaForTranscription.length > 0)
                                            root.startTranscriptionBatch()
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: Backend.transcriptionInProgress
                                      ? Backend.transcriptionStatus
                                      : Backend.translationStatus !== ""
                                        ? Backend.translationStatus
                                        : Backend.transcriptionStatus
                                color: Theme.textMuted
                                elide: Text.ElideRight
                            }
                            // Cuts the footage behind the selected lines out of the
                            // timeline and closes the gap - text-based editing. Only
                            // shown once a run is selected.
                            Button {
                                visible: root.selectionAnchor >= 0
                                enabled: !Backend.translationInProgress
                                HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                implicitHeight: 28
                                padding: 0
                                leftPadding: 12
                                rightPadding: 12
                                text: {
                                    var n = root.selectionMax - root.selectionMin + 1
                                    return n > 1 ? qsTr("Remove %1 lines from video").arg(n)
                                                 : qsTr("Remove from video")
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: Theme.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: Theme.fsXs
                                }
                                background: Rectangle {
                                    color: parent.down ? Theme.hover : Theme.bgPrimary
                                    border.color: Theme.danger
                                    radius: Theme.radiusSm
                                }
                                onClicked: root.removeSelectionFromVideo()
                            }
                        }
                    }

                    ListView {
                        id: transcriptList
                        anchors.fill: parent
                        // The header's real height rather than a number that has to
                        // be edited every time a control is added to it.
                        anchors.topMargin: transcriptHeader.height + 16
                        clip: true
                        visible: !Backend.transcriptionInProgress
                        model: Backend.translationInProgress ? Backend.translationPreview : Backend.transcript
                        onCountChanged: ModelGuard.note("text.transcript", count)
                        spacing: 2
                        delegate: Rectangle {
                            id: transcriptRow
                            required property int index
                            required property var modelData
                            width: transcriptList.width - 16
                            x: 16
                            readonly property bool matches: root.transcriptSearch === ""
                                                            || String(modelData.text).toLowerCase().indexOf(root.transcriptSearch) >= 0
                            height: matches ? segmentColumn.implicitHeight + 12 : 0
                            visible: matches
                            readonly property bool selected: root.transcriptRowSelected(transcriptRow.index)
                            color: selected
                                   ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                                   : rowSelect.containsMouse ? Theme.hover : "transparent"

                            Column {
                                id: segmentColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3
                                Text {
                                    text: root.timestamp(transcriptRow.modelData.start)
                                          + " - " + root.timestamp(transcriptRow.modelData.end)
                                    color: Theme.accent
                                    font.pixelSize: Theme.fsXs
                                    font.family: Theme.monoFont
                                    leftPadding: 8
                                    rightPadding: 8
                                }
                                Text {
                                    visible: Backend.translationInProgress
                                             || root.editingTranscriptIndex !== transcriptRow.index
                                    width: parent.width - 16
                                    leftPadding: 8
                                    rightPadding: 8
                                    text: transcriptRow.modelData.text
                                    color: Theme.textSecondary
                                    font.family: "Khmer OS System"
                                    font.pixelSize: Theme.fsSm
                                    wrapMode: Text.WordWrap
                                }
                                TextField {
                                    id: transcriptEditor
                                    property bool discardOnFocusLoss: false
                                    visible: !Backend.translationInProgress
                                             && root.editingTranscriptIndex === transcriptRow.index
                                    width: parent.width
                                    text: transcriptRow.modelData.text
                                    selectByMouse: true
                                    color: Theme.textPrimary
                                    background: Rectangle {
                                        color: Theme.bgPrimary
                                        border.color: Theme.accent
                                        radius: Theme.radiusSm
                                    }
                                    onAccepted: {
                                        if (Backend.updateTranscriptSegment(transcriptRow.index, text))
                                            root.editingTranscriptIndex = -1
                                    }
                                    Keys.onEscapePressed: {
                                        discardOnFocusLoss = true
                                        root.editingTranscriptIndex = -1
                                        Qt.callLater(function() { discardOnFocusLoss = false })
                                    }
                                    onActiveFocusChanged: {
                                        if (!activeFocus && visible && !discardOnFocusLoss
                                                && Backend.updateTranscriptSegment(transcriptRow.index, text))
                                            root.editingTranscriptIndex = -1
                                    }
                                    onVisibleChanged: {
                                        if (visible) {
                                            forceActiveFocus()
                                            selectAll()
                                        }
                                    }
                                }
                            }
                            MouseArea {
                                id: rowSelect
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                // While a row is being edited the TextField owns the
                                // pointer, so this stays out of its way.
                                enabled: !Backend.translationInProgress
                                         && root.editingTranscriptIndex !== transcriptRow.index
                                onClicked: function(mouse) {
                                    if (mouse.button === Qt.RightButton) {
                                        // The menu is shared, so the row it acts on is
                                        // handed over before it opens. A right-click
                                        // outside the current run reselects just this
                                        // row, so the menu never cuts a hidden span.
                                        if (!root.transcriptRowSelected(transcriptRow.index)) {
                                            root.selectionAnchor = transcriptRow.index
                                            root.selectionActive = transcriptRow.index
                                        }
                                        transcriptSegmentMenu.segmentIndex = transcriptRow.index
                                        transcriptSegmentMenu.popup()
                                        return
                                    }
                                    Backend.playheadMs = transcriptRow.modelData.start * 1000
                                    if ((mouse.modifiers & Qt.ShiftModifier)
                                            && root.selectionAnchor >= 0) {
                                        root.selectionActive = transcriptRow.index
                                    } else {
                                        root.selectionAnchor = transcriptRow.index
                                        root.selectionActive = transcriptRow.index
                                    }
                                }
                                onDoubleClicked: function(mouse) {
                                    if (mouse.button !== Qt.LeftButton)
                                        return
                                    root.editingTranscriptIndex = transcriptRow.index
                                    Qt.callLater(function() {
                                        transcriptEditor.forceActiveFocus()
                                        transcriptEditor.selectAll()
                                    })
                                }
                            }
                        }
                    }

                    TranscriptSkeleton {
                        id: transcriptSkeleton
                        anchors.fill: parent
                        anchors.topMargin: transcriptHeader.height + 16
                        visible: Backend.transcriptionInProgress
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.topMargin: transcriptHeader.height + 16
                        visible: transcriptList.count === 0
                                 && root.transcriptionSourceMedia.length > 0
                                 && !Backend.transcriptionInProgress
                                 && !Backend.translationInProgress
                        spacing: 0
                        
                        // Header
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            Layout.alignment: Qt.AlignTop
                            color: "transparent"
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 16
                                spacing: 8
                                IconButton {
                                    iconName: root.clipsExpanded ? "chevron-down" : "chevron-right"
                                    boxSize: 24
                                    glyphSize: 14
                                    onClicked: root.clipsExpanded = !root.clipsExpanded
                                }
                                Text {  
                                    text: qsTr("%1 project source clips").arg(root.transcriptionSourceMedia.length)
                                    color: Theme.textPrimary 
                                    font.pixelSize: Theme.fsSm  
                                    Layout.fillWidth: true  
                                }
                                Text {
                                    text: qsTr("%1 selected").arg(root.selectedMediaForTranscription.length)
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fsSm
                                }
                            }
                        }
                        
                        // Expanded Content
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: root.clipsExpanded
                            
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0
                                
                                SearchField {
                                    Layout.fillWidth: true
                                    Layout.margins: 16
                                    Layout.topMargin: 4
                                    Layout.bottomMargin: 16
                                    placeholder: qsTr("Filter source clips")
                                    onTextChanged: root.sourceClipSearch = text.toLowerCase()
                                }
                                
                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; Layout.leftMargin: 16; Layout.rightMargin: 16 }
                                
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 32
                                    color: "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 16
                                        anchors.rightMargin: 16
                                        spacing: 16
                                        LumetriCheckBox {
                                            checked: root.selectedMediaForTranscription.length > 0
                                                      && root.selectedMediaForTranscription.length === root.transcriptionSourceMedia.length
                                            onClicked: {
                                                if (checked) {
                                                    var all = []
                                                    for (var i = 0; i < root.transcriptionSourceMedia.length; i++) all.push(root.transcriptionSourceMedia[i].id)
                                                    root.selectedMediaForTranscription = all
                                                } else {
                                                    root.selectedMediaForTranscription = []
                                                }
                                            }
                                        }
                                        Text { text: qsTr("Name"); color: Theme.textPrimary; font.pixelSize: Theme.fsSm; Layout.fillWidth: true }
                                        Text { text: qsTr("Duration"); color: Theme.textPrimary; font.pixelSize: Theme.fsSm; Layout.rightMargin: 16 }
                                    }
                                }
                                
                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                                
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    model: root.transcriptionSourceMedia
                                    clip: true
                                    delegate: Rectangle {
                                        id: sourceMediaRow
                                        width: ListView.view.width
                                        height: 54
                                        color: "transparent"
                                        required property var modelData
                                        property bool isSelected: root.selectedMediaForTranscription.indexOf(modelData.id) !== -1
                                        visible: root.sourceClipSearch === "" || String(modelData.name).toLowerCase().indexOf(root.sourceClipSearch) !== -1
                                        
                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            width: parent.width
                                            height: 1
                                            color: Theme.border
                                        }
                                        
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 16
                                            anchors.rightMargin: 16
                                            spacing: 16
                                            LumetriCheckBox {
                                                checked: sourceMediaRow.isSelected
                                                onClicked: {
                                                    var arr = root.selectedMediaForTranscription.slice()
                                                    var idx = arr.indexOf(modelData.id)
                                                    if (checked && idx === -1) arr.push(modelData.id)
                                                    else if (!checked && idx !== -1) arr.splice(idx, 1)
                                                    root.selectedMediaForTranscription = arr
                                                }
                                            }
                                            Rectangle {
                                                Layout.preferredWidth: 60
                                                Layout.preferredHeight: 38
                                                radius: Theme.radiusSm
                                                color: Theme.bgPrimary
                                                clip: true
                                                Image {
                                                    anchors.fill: parent
                                                    source: modelData.thumbnailUrl || ""
                                                    fillMode: Image.PreserveAspectCrop
                                                    asynchronous: true
                                                    visible: source.toString() !== ""
                                                }
                                                Image {
                                                    anchors.centerIn: parent
                                                    width: 18
                                                    height: 18
                                                    source: modelData.kind === "audio"
                                                            ? "../../assets/icons/music.svg"
                                                            : "../../assets/icons/film.svg"
                                                    opacity: 0.65
                                                    visible: !modelData.thumbnailUrl
                                                }
                                            }
                                            Text {
                                                text: modelData.name || qsTr("Media")
                                                color: Theme.textPrimary
                                                font.pixelSize: Theme.fsSm
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                text: root.formatDuration(modelData.durationMs)
                                                color: Theme.textPrimary
                                                font.pixelSize: Theme.fsSm
                                                Layout.rightMargin: 16
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillHeight: true
                            visible: !root.clipsExpanded
                        }
                    }
                    
                    Item {
                        anchors.fill: parent
                        visible: transcriptList.count === 0
                                 && root.transcriptionSourceMedia.length === 0
                                 && !Backend.transcriptionInProgress
                                 && !Backend.translationInProgress
                        
                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 16
                            Text {
                                text: qsTr("No dialogue found")
                                color: Theme.textPrimary
                                font.pixelSize: 18
                                Layout.alignment: Qt.AlignHCenter
                            }
                            Text {
                                text: qsTr("To enable transcription services, your audio must contain\nunmuted verbal dialogue.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fsSm
                                horizontalAlignment: Text.AlignHCenter
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }

                CaptionStylePanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.subTab === 1
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.subTab === 2

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("No graphics selected")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsSm
                    }
                }

                // Text to Speech opens as a native modal window - the app's standard
                // dialog shell (NativeModalWindow), matching Subtitle settings and the
                // Speech report - from the icon button in the secondary-tab row,
                // instead of a permanent inline tab. The panel itself is unchanged;
                // only its host moved, which keeps the Text panel uncluttered. The
                // native title bar carries the title and close button, and the window
                // is sized tight so the panel's fill spacer collapses and only the
                // controls show (no large empty area). A Window is a top-level, so
                // declaring it here does not affect the surrounding ColumnLayout.
                NativeModalWindow {
                    id: ttsWindow
                    ownerWindow: root.ownerWindow
                    dialogTitle: qsTr("Text to Speech")
                    dialogWidth: 840
                    dialogHeight: 430

                    TextToSpeechTimedPanel {
                        anchors.fill: parent
                        ownerWindow: ttsWindow
                    }
                }
            }
        }
    }
}
