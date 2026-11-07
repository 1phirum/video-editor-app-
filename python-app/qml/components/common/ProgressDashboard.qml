// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import CutPro 1.0
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

// The progress sheet is built out of the Export panel's section language (see
// components/export/ExportSection.qml): every background job is one 40px strip
// on a card - chevron, glyph, name, then its own slim track and percent on the
// right - and the clips it is working through live inside that strip's body
// instead of floating loose underneath it.
Dialog {
    id: root
    padding: 0
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    implicitWidth: 640
    // Hugs its content: one running job is a short card instead of a slab of
    // empty trough. Idle it holds 150px for the empty message, and a long batch
    // stops growing at 340 and scrolls instead of running past the window.
    implicitHeight: sheetHeader.implicitHeight + sheetFooter.implicitHeight
                    + (isIdle ? 150
                              : Math.max(56, Math.min(340, jobsColumn.implicitHeight)))

    property bool transcriptionExpanded: true
    property var transcriptionJobs: []
    property string transcriptionCurrentId: ""
    property var vocalRemovalJobs: []
    property string vocalRemovalCurrentId: ""
    property bool vocalRemovalExpanded: true
    property double vocalRemovalStartedAt: 0
    property double etaClock: Date.now()
    onVocalRemovalCurrentIdChanged: {
        vocalRemovalStartedAt = vocalRemovalCurrentId !== "" ? Date.now() : 0
        etaClock = Date.now()
    }
    property bool hasActiveJobs: Backend.mediaImportInProgress || Backend.transcriptionInProgress || Backend.translationInProgress || Backend.exportInProgress || Backend.demucsInProgress || Backend.textToSpeechEngine.inProgress || root.vocalRemovalJobs.some(function(job) { return job.status === "Queued" || job.status === "Running" })
    property bool hasCancellableJobs: Backend.transcriptionInProgress || Backend.translationInProgress || Backend.exportInProgress || Backend.demucsInProgress || Backend.textToSpeechEngine.inProgress
    property bool clearCompletedRequested: false

    // A finished job leaves its status line behind, so "nothing is running" and
    // "there is nothing to show at all" are different states: only the second
    // one gets the empty message.
    readonly property bool hasFinishedWork:
        transcriptionJobs.length > 0 || vocalRemovalJobs.length > 0
        || Backend.transcriptionStatus !== "" || Backend.translationStatus !== ""
        || Backend.exportStatus !== "" || Backend.demucsStatus !== ""
    readonly property bool isIdle: !hasActiveJobs && !hasFinishedWork

    function cancelAll() {
        if (Backend.exportInProgress) Backend.cancelExport()
        if (Backend.translationInProgress) Backend.cancelTranslation()
        if (Backend.transcriptionInProgress) Backend.cancelTranscription()
        if (Backend.demucsInProgress) Backend.cancelDemucs()
        if (Backend.textToSpeechEngine.inProgress) Backend.textToSpeechEngine.cancel()
    }
    // "Nothing is running" and "the run finished" are different facts, and this
    // section used to print Complete for both: percentText fell back to that word
    // whenever transcriptionInProgress was false and there was no per-clip job
    // list - which is exactly the state a single-media run is in between its
    // status line appearing and the worker process actually starting. A ten hour
    // source therefore read Complete on the first frame and never moved off it.
    //
    // Only the status line knows which phase a run is in, so it decides. Backend
    // writes five shapes: "Transcribing..." and "Transcribing - <window>" while a
    // run walks, "Transcription complete[ - lang]" or "TTML exported" when it
    // worked, "Transcription cancelled" when the user stopped it, and raw worker
    // text for anything else - which means it failed.
    readonly property string transcriptionPhase: {
        var status = String(Backend.transcriptionStatus || "")
        if (Backend.transcriptionInProgress
                || status.indexOf("Transcribing") === 0)
            return "running"
        if (status === "")
            return "idle"
        if (status.indexOf("Transcription complete") === 0
                || status === "TTML exported")
            return "complete"
        if (status.indexOf("Transcription cancelled") === 0)
            return "cancelled"
        // Importing an SRT or TTML writes its own count into this same status line
        // ("Imported 19831 subtitle segments"), and it is not a transcription run:
        // reading a file is finished the moment it returns, so there is nothing to
        // wait for and no worker to report a percentage. Enumerated here because
        // the catch-all below reads anything it does not recognise as worker error
        // text, which turned a successful import into the word Failed.
        if (status.indexOf("Imported ") === 0)
            return "imported"
        return "failed"
    }
    function transcriptionPhaseLabel() {
        if (transcriptionPhase === "complete") return qsTr("Complete")
        if (transcriptionPhase === "imported") return qsTr("Imported")
        if (transcriptionPhase === "cancelled") return qsTr("Cancelled")
        if (transcriptionPhase === "failed") return qsTr("Failed")
        return ""
    }
    function transcriptionCompletedCount() {
        var count = 0
        for (var i = 0; i < transcriptionJobs.length; ++i)
            if (transcriptionJobs[i].status === "Complete") ++count
        return count
    }
    function transcriptionBatchProgress() {
        if (transcriptionJobs.length === 0)
            return transcriptionPhase === "running"
                    ? Number(Backend.transcriptionProgress || 0) : 0
        var totalDuration = 0
        var completedDuration = 0
        for (var i = 0; i < transcriptionJobs.length; ++i) {
            var job = transcriptionJobs[i]
            var duration = Math.max(1, Number(job.durationMs || 0))
            totalDuration += duration
            completedDuration += job.status === "Complete" ? duration : duration * jobProgress(job)
        }
        return totalDuration > 0 ? Math.max(0, Math.min(1, completedDuration / totalDuration)) : 0
    }
    function statusText(job) {
        if (job.status === "Complete") return "100% | Complete"
        if (job.status === "Failed") return "Failed"
        if (job.status === "Cancelled") return "Cancelled"
        if (jobActive(job))
            return Math.round(jobProgress(job) * 100) + "% | " + (job.detail || "Transcribing")
        return "Queued"
    }
    function jobActive(job) {
        return job.status === "Transcribing"
                || (job.id === transcriptionCurrentId && Backend.transcriptionInProgress)
    }
    function jobProgress(job) {
        // Concurrent clips own their progress. Legacy single-worker jobs still
        // use the current-ID fallback, so local transcription keeps its display.
        if (typeof job.progress === "number")
            return Math.max(0, Math.min(1, job.progress))
        return job.id === transcriptionCurrentId ? Number(Backend.transcriptionProgress || 0) : 0
    }
    function vocalCompletedCount() {
        var count = 0
        for (var i = 0; i < vocalRemovalJobs.length; ++i)
            if (vocalRemovalJobs[i].status === "Complete") ++count
        return count
    }
    function vocalBatchProgress() {
        if (vocalRemovalJobs.length === 0)
            return Backend.demucsInProgress ? Backend.demucsProgress : 0
        var total = 0
        var done = 0
        for (var i = 0; i < vocalRemovalJobs.length; ++i) {
            var job = vocalRemovalJobs[i]
            var duration = Math.max(1, Number(job.durationMs || 0))
            total += duration
            done += job.status === "Complete" ? duration : (job.id === vocalRemovalCurrentId ? duration * Backend.demucsProgress : 0)
        }
        return total > 0 ? Math.max(0, Math.min(1, done / total)) : 0
    }
    function vocalStatusText(job) {
        if (job.status === "Complete") return "100% | Complete"
        if (job.status === "Failed") return "Failed"
        if (job.id === vocalRemovalCurrentId && Backend.demucsInProgress) {
            var percent = Math.min(99, Math.round(Number(Backend.demucsProgress || 0) * 100))
            var phase = percent >= 99 ? "Finalizing audio" : "Removing vocals"
            return percent + "% | " + phase
        }
        return "Queued"
    }
    function vocalCurrentJob() {
        for (var i = 0; i < vocalRemovalJobs.length; ++i)
            if (vocalRemovalJobs[i].id === vocalRemovalCurrentId)
                return vocalRemovalJobs[i]
        return null
    }
    function vocalRemainingMilliseconds() {
        var progress = Math.max(0, Math.min(1, Number(Backend.demucsProgress || 0)))
        var current = vocalCurrentJob()
        if (!Backend.demucsInProgress || !current || vocalRemovalStartedAt <= 0
                || progress < 0.02)
            return -1
        var elapsed = Math.max(1000, etaClock - vocalRemovalStartedAt)
        var currentDuration = Math.max(1, Number(current.durationMs || 0))
        var processingMsPerMediaMs = elapsed / (currentDuration * progress)
        var remainingMediaMs = currentDuration * (1 - progress)
        for (var i = 0; i < vocalRemovalJobs.length; ++i) {
            var job = vocalRemovalJobs[i]
            if (job.id !== vocalRemovalCurrentId && job.status === "Queued")
                remainingMediaMs += Math.max(1, Number(job.durationMs || 0))
        }
        return Math.max(0, processingMsPerMediaMs * remainingMediaMs)
    }
    function formatRemaining(milliseconds) {
        if (milliseconds < 0 || !isFinite(milliseconds)) return ""
        var seconds = Math.max(1, Math.round(milliseconds / 1000))
        if (seconds < 60) return qsTr("About %1 sec left").arg(seconds)
        var minutes = Math.max(1, Math.round(seconds / 60))
        if (minutes < 60) return qsTr("About %1 min left").arg(minutes)
        var hours = Math.floor(minutes / 60)
        var rest = minutes % 60
        return "About " + hours + " hr" + (rest > 0 ? " " + rest + " min" : "") + " left"
    }
    function vocalEtaText() {
        return formatRemaining(vocalRemainingMilliseconds())
    }

    // A failed strip has room for exactly one word - "Failed" - and the worker's
    // own explanation is the one thing the user actually needs, so it opens here
    // on its own small sheet over the dashboard instead of being dropped.
    function showReason(title, text) {
        reasonPopup.reasonTitle = title
        reasonPopup.reasonText = (text && String(text).trim() !== "")
                                 ? String(text)
                                 : qsTr("No further detail was reported.")
        reasonPopup.open()
    }

    Timer {
        interval: 1000
        repeat: true
        running: Backend.demucsInProgress
        onTriggered: root.etaClock = Date.now()
    }

    // The failure-reason sheet. Parented to the same overlay as the dashboard so
    // it centres on the window and sits above it, with a soft dim of its own
    // (the dashboard opens without one) to read as the top layer.
    Popup {
        id: reasonPopup
        parent: root.parent
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0

        property string reasonTitle: ""
        property string reasonText: ""

        width: Math.min(460, (parent ? parent.width : 480) - 32)
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        implicitHeight: reasonLayout.implicitHeight

        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }
        enter: Transition {}
        exit: Transition {}

        background: Rectangle {
            color: Theme.bgCard
            border.color: Theme.outline
            border.width: 1
            radius: 6
        }

        contentItem: ColumnLayout {
            id: reasonLayout
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 42
                color: Theme.bgCardHeader
                topLeftRadius: 5
                topRightRadius: 5

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.divider
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    spacing: 8

                    Text {
                        text: reasonPopup.reasonTitle
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsMd
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    IconButton {
                        iconName: "x"
                        boxSize: 26
                        glyphSize: 13
                        restColor: Theme.textSecondary
                        onClicked: reasonPopup.close()
                    }
                }
            }

            // Worker output can run long, so the reason scrolls rather than
            // stretching the sheet down off the bottom of the window.
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, reasonBody.implicitHeight)
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                Text {
                    id: reasonBody
                    width: reasonPopup.width
                    leftPadding: 14
                    rightPadding: 14
                    topPadding: 12
                    bottomPadding: 12
                    text: reasonPopup.reasonText
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 48
                color: Theme.bgCardHeader
                bottomLeftRadius: 5
                bottomRightRadius: 5

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: Theme.divider
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12

                    Item { Layout.fillWidth: true }

                    SheetButton {
                        text: qsTr("Close")
                        onClicked: reasonPopup.close()
                    }
                }
            }
        }
    }

    // No dimming behind the panel: it opens over the workspace as an ordinary
    // window, not as a lightbox. The overlay item stays (transparent) so the
    // dialog keeps swallowing clicks that land outside it.
    Overlay.modal: Rectangle { color: "transparent" }
    // Shown and hidden outright. A fade leaves the panel half-transparent while
    // it runs, so the settings underneath read straight through it.
    enter: Transition {}
    exit: Transition {}

    // Reads as a sheet dropped over the workspace rather than as one more docked
    // panel: one step of lift on the surface, a near-black outline, and three
    // tightening translucent frames for an edge shadow. Rings instead of a
    // blurred MultiEffect layer, which would need the popup padded out to stop
    // the blur being clipped flat against the panel edge.
    background: Item {
        Repeater {
            model: 3
            delegate: Rectangle {
                required property int index
                anchors.fill: sheet
                anchors.margins: -(index + 1)
                radius: sheet.radius + index + 1
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(0, 0, 0, 0.18 - index * 0.05)
            }
        }
        Rectangle {
            id: sheet
            anchors.fill: parent
            color: Theme.bgCard
            border.color: Theme.outline
            border.width: 1
            radius: 6
        }
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        // Inside the outline, not over it: a child filling the whole rect paints
        // across the border and the frame then shows only alongside the body.
        anchors.margins: 1
        spacing: 0

        Rectangle {
            id: sheetHeader
            Layout.fillWidth: true
            implicitHeight: 46
            color: Theme.bgCardHeader
            topLeftRadius: 5
            topRightRadius: 5

            // Hairline, not a border: the tone step above already does the
            // separating, so the line only has to settle the edge.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 10
                spacing: 7

                Text {
                    text: qsTr("Progress Dashboard")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsLg
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.preferredWidth: 6
                    Layout.preferredHeight: 6
                    radius: 3
                    color: root.hasActiveJobs ? Theme.accent : Theme.textMuted
                    opacity: root.hasActiveJobs ? 1 : 0.7
                }

                Text {
                    text: root.hasActiveJobs ? qsTr("In progress")
                                             : root.hasFinishedWork ? qsTr("Recent activity") : qsTr("Idle")
                    color: root.hasActiveJobs ? Theme.accent : Theme.textMuted
                    font.pixelSize: Theme.fsSm
                }

                IconButton {
                    iconName: "x"
                    boxSize: 26
                    glyphSize: 13
                    restColor: Theme.textSecondary
                    onClicked: root.close()
                }
            }
        }

        // A trough, one step darker than the sheet, because it holds the job
        // cards rather than content of its own - the same three-tone stack the
        // Export panel uses: trough, card, strip.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgPrimary

            ScrollView {
                id: jobsScroll
                anchors.fill: parent
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    id: jobsColumn
                    width: jobsScroll.availableWidth
                    spacing: 8

                    // Keeps the first card clear of the header hairline.
                    Item { Layout.fillWidth: true; Layout.preferredHeight: 4 }

                    JobSection {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        visible: root.transcriptionJobs.length > 0
                                 || root.transcriptionPhase !== "idle"
                        title: qsTr("Speech to Text Transcriptions")
                        iconName: "subtitles"
                        busy: root.transcriptionPhase === "running"
                        // No chevron until there is a clip list behind it, so an
                        // open section never resolves to an empty body.
                        expandable: root.transcriptionJobs.length > 0
                        expanded: root.transcriptionExpanded
                                  && root.transcriptionJobs.length > 0
                        // -1 when a finished run has left only its status behind:
                        // the batch helper answers 0 for "nothing to measure",
                        // and an empty track beside the word "Complete" argues
                        // with itself. -1 drops the track instead.
                        // A single-clip run with no measurable progress (one-pass
                        // Gemini streams no percentage) shows the busy sweep - -1
                        // here, and busy above - rather than a frozen 0% track.
                        percent: (root.transcriptionPhase === "running"
                                  && Backend.transcriptionIndeterminate
                                  && root.transcriptionJobs.length <= 1)
                                 ? -1
                                 : (root.transcriptionPhase === "running"
                                    || root.transcriptionJobs.length > 0
                                    ? root.transcriptionBatchProgress() : -1)
                        // Never the word Complete unless the run said so. A
                        // batch shows how many of its clips are done; a single
                        // run shows its own percentage while it walks.
                        percentText: root.transcriptionPhase === "running"
                                     ? (Backend.transcriptionIndeterminate
                                        && root.transcriptionJobs.length <= 1
                                        ? qsTr("Working")
                                        : Math.round(root.transcriptionBatchProgress() * 100) + "%")
                                     : (root.transcriptionJobs.length > 0
                                        ? root.transcriptionCompletedCount() + "/" + root.transcriptionJobs.length
                                        : root.transcriptionPhaseLabel())
                        // A windowed run puts its window counter in the status
                        // line, which is the only progress a single long source
                        // has to show; a failure puts the worker's own words
                        // there, which is the only place the user can read them.
                        // An import has no progress at all - its line is the
                        // segment count, and that is worth showing.
                        detailText: root.transcriptionPhase === "running"
                                    ? (root.transcriptionJobs.length > 1
                                       ? qsTr("%1 of %2 done").arg(root.transcriptionCompletedCount()).arg(root.transcriptionJobs.length)
                                       : String(Backend.transcriptionStatus || ""))
                                    : (root.transcriptionPhase === "failed"
                                       || root.transcriptionPhase === "imported"
                                       ? String(Backend.transcriptionStatus || "")
                                       : "")
                        onToggle: root.transcriptionExpanded = !root.transcriptionExpanded

                        Repeater {
                            model: root.transcriptionExpanded ? root.transcriptionJobs : []
                            delegate: ClipRow {
                                required property var modelData
                                Layout.fillWidth: true
                                clipName: modelData.name
                                thumbnailUrl: modelData.thumbnailUrl || ""
                                clipStatus: root.statusText(modelData)
                                progress: modelData.status === "Complete" ? 1 : root.jobProgress(modelData)
                                active: root.jobActive(modelData)
                                failReason: modelData.status === "Failed" ? String(modelData.detail || "") : ""
                                onDetailRequested: root.showReason(qsTr("Transcription failed"), clipName + "\n\n" + failReason)
                            }
                        }
                    }

                    JobSection {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        visible: Backend.mediaImportInProgress
                        title: qsTr("Media imports")
                        iconName: "import"
                        expandable: false
                        busy: true
                        percent: Backend.mediaImportProgress / 100
                        percentText: Backend.mediaImportProgress + "%"
                    }

                    JobSection {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        visible: Backend.translationInProgress
                        title: qsTr("Transcript translation")
                        iconName: "type"
                        expandable: false
                        busy: true
                        percent: -1
                        percentText: qsTr("Running")
                    }

                    JobSection {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        visible: Backend.textToSpeechEngine.inProgress
                        title: qsTr("Text to Speech")
                        iconName: "volume-2"
                        expandable: false
                        busy: true
                        percent: Backend.textToSpeechEngine.progress
                        percentText: Math.round(Backend.textToSpeechEngine.progress * 100) + "%"
                        detailText: Backend.textToSpeechEngine.status
                    }

                    JobSection {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        visible: Backend.exportInProgress
                        title: qsTr("Export")
                        iconName: "file-up"
                        expandable: false
                        busy: true
                        percent: Backend.exportProgress
                        percentText: Math.round(Backend.exportProgress * 100) + "%"
                    }

                    JobSection {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        visible: Backend.demucsInProgress || Backend.demucsStatus !== ""
                        title: qsTr("Remove Vocals")
                        iconName: "volume-x"
                        busy: Backend.demucsInProgress
                        expandable: root.vocalRemovalJobs.length > 0
                        expanded: root.vocalRemovalExpanded
                                  && root.vocalRemovalJobs.length > 0
                        percent: root.vocalRemovalJobs.length > 0
                                 ? root.vocalBatchProgress()
                                 : (Backend.demucsInProgress ? Backend.demucsProgress : -1)
                        percentText: root.vocalRemovalJobs.length > 0
                                     ? (Backend.demucsInProgress
                                        ? Math.min(99, Math.round(root.vocalBatchProgress() * 100)) + "%"
                                        : (root.vocalCompletedCount() === root.vocalRemovalJobs.length
                                           ? qsTr("Complete")
                                           : root.vocalCompletedCount() + "/" + root.vocalRemovalJobs.length))
                                     : (Backend.demucsInProgress
                                        ? Math.min(99, Math.round(Backend.demucsProgress * 100)) + "%"
                                        : "")
                        // The ETA belongs to the batch, so it is written once
                        // here instead of being repeated on every clip row. A
                        // finished run with no clip list left has only its
                        // status line to show, and that reads better here than
                        // squeezed into the percent slot.
                        detailText: Backend.demucsInProgress
                                    ? (root.vocalEtaText() !== "" ? root.vocalEtaText()
                                                                  : qsTr("Estimating time left..."))
                                    : (root.vocalRemovalJobs.length > 0 ? "" : Backend.demucsStatus)
                        onToggle: root.vocalRemovalExpanded = !root.vocalRemovalExpanded
                        // The direct effect-panel toggle runs demucs without a
                        // clip-row list, so a failure there has nowhere to be
                        // clicked: surface the reason on the strip itself.
                        infoClickable: !Backend.demucsInProgress
                                       && root.vocalRemovalJobs.length === 0
                                       && Backend.demucsStatus.toLowerCase().indexOf("fail") >= 0
                        onInfoRequested: root.showReason(qsTr("Vocal removal failed"),
                                                         Backend.lastError !== ""
                                                         ? Backend.lastError
                                                         : Backend.demucsStatus)

                        Repeater {
                            model: root.vocalRemovalExpanded ? root.vocalRemovalJobs : []
                            delegate: ClipRow {
                                required property var modelData
                                Layout.fillWidth: true
                                clipName: modelData.name
                                thumbnailUrl: modelData.thumbnailUrl || ""
                                clipStatus: root.vocalStatusText(modelData)
                                progress: modelData.status === "Complete" ? 1 : (modelData.id === root.vocalRemovalCurrentId ? Number(Backend.demucsProgress || 0) : 0)
                                active: modelData.id === root.vocalRemovalCurrentId && Backend.demucsInProgress
                                // The reason the batch recorded when this clip
                                // failed - the worker's own words, not the bare
                                // "Vocal separation failed" placeholder - with the
                                // last backend error and then a plain-language hint
                                // as fallbacks so a failed row always explains itself.
                                failReason: (modelData.detail && modelData.detail !== ""
                                             && modelData.detail !== "Vocal separation failed")
                                            ? modelData.detail
                                            : (Backend.lastError !== ""
                                               ? Backend.lastError
                                               : qsTr("The vocal separation process ended without reporting a reason. Check that the audio engine is installed, and that a long clip did not run out of memory."))
                                onDetailRequested: root.showReason(qsTr("Vocal removal failed"), failReason)
                            }
                        }
                    }

                    Item { Layout.fillWidth: true; Layout.preferredHeight: 4 }
                }
            }

            // Centred in the trough rather than parked at the top of the job
            // list, so the sheet does not read as a list with one stray label.
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 300)
                visible: root.isIdle
                spacing: 7

                Image {
                    source: "../../assets/icons/clock.svg"
                    sourceSize.width: 22
                    sourceSize.height: 22
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    Layout.alignment: Qt.AlignHCenter
                    opacity: 0.28
                }
                Text {
                    text: qsTr("No background processes")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsMd
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: qsTr("Transcriptions, exports and vocal removal report in here while they run.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsXs
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        Rectangle {
            id: sheetFooter
            Layout.fillWidth: true
            implicitHeight: 50
            color: Theme.bgCardHeader
            bottomLeftRadius: 5
            bottomRightRadius: 5

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                GhostButton {
                    text: qsTr("Clear all completed")
                    enabled: !root.hasActiveJobs && root.hasFinishedWork
                    onClicked: {
                        root.clearCompletedRequested = true
                        root.transcriptionJobs = []
                        root.vocalRemovalJobs = []
                    }
                }

                Item { Layout.fillWidth: true }

                SheetButton {
                    text: qsTr("Cancel all")
                    enabled: root.hasCancellableJobs
                    onClicked: root.cancelAll()
                }
            }
        }
    }

    // One job, laid out like an ExportSection: the strip is the whole card while
    // it is closed and grows a body of clip rows when it is opened.
    component JobSection: Rectangle {
        id: section

        property string title: ""
        property string iconName: "subtitles"
        property real percent: -1
        property string percentText: ""
        property string detailText: ""
        property bool expanded: false
        property bool expandable: true
        property bool busy: false
        // When true, a red alert glyph on the strip opens the failure reason -
        // for a job that failed with no clip-row list to click.
        property bool infoClickable: false
        default property alias sectionData: sectionBody.data
        signal toggle()
        signal infoRequested()

        implicitHeight: sectionLayout.implicitHeight
        color: Theme.bgCard
        radius: Theme.radiusSm

        ColumnLayout {
            id: sectionLayout
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0

            Rectangle {
                id: strip
                Layout.fillWidth: true
                implicitHeight: 40
                radius: section.radius
                color: section.expandable && stripHover.hovered
                       ? Qt.lighter(Theme.bgCardHeader, 1.14)
                       : Theme.bgCardHeader

                // Squares off the two bottom corners while the body is showing,
                // so the strip's own radius does not cut a notch into the top of
                // the clip list. Closed, the strip *is* the card and keeps all
                // four corners.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.radius
                    color: parent.color
                    visible: section.expanded
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.divider
                    visible: section.expanded
                }

                HoverHandler {
                    id: stripHover
                    cursorShape: section.expandable ? Qt.PointingHandCursor
                                                    : Qt.ArrowCursor
                }

                TapHandler {
                    enabled: section.expandable
                    onTapped: section.toggle()
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 12
                    spacing: 9

                    Image {
                        source: section.expandable
                                ? "../../assets/icons/"
                                  + (section.expanded ? "chevron-down" : "chevron-right")
                                  + ".svg"
                                : ""
                        sourceSize.width: 13
                        sourceSize.height: 13
                        Layout.preferredWidth: 13
                        Layout.preferredHeight: 13
                        fillMode: Image.PreserveAspectFit
                        opacity: 0.7
                    }

                    Image {
                        source: "../../assets/icons/" + section.iconName + ".svg"
                        sourceSize.width: 14
                        sourceSize.height: 14
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        fillMode: Image.PreserveAspectFit
                        opacity: 0.85
                    }

                    Text {
                        text: section.title
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsMd
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 60
                    }

                    // First thing dropped when the sheet is narrow: the track and
                    // the percent are the row's job, the ETA is a nicety.
                    Text {
                        visible: section.detailText !== "" && strip.width > 380
                        text: section.detailText
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsXs
                        elide: Text.ElideRight
                        Layout.maximumWidth: 180
                    }

                    Rectangle {
                        id: track
                        visible: section.percent >= 0 || section.busy
                        Layout.preferredWidth: 84
                        Layout.preferredHeight: 4
                        radius: 2
                        color: Theme.bgPrimary
                        clip: true

                        Rectangle {
                            visible: section.percent >= 0
                            width: parent.width * Math.max(0, Math.min(1, section.percent))
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                        }

                        // Nothing to count against, so the track reports "moving"
                        // instead of "this far along" - which is the one thing the
                        // spinner that used to sit further along this row was
                        // saying, in a second place.
                        Rectangle {
                            id: sweep
                            visible: section.percent < 0 && section.busy
                            width: parent.width * 0.36
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent

                            NumberAnimation on x {
                                running: sweep.visible
                                loops: Animation.Infinite
                                from: -sweep.width
                                to: track.width
                                duration: 1150
                            }
                        }
                    }

                    Text {
                        text: section.percentText
                        color: section.busy ? Theme.textSecondary : Theme.textMuted
                        font.pixelSize: Theme.fsXs
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        Layout.minimumWidth: 28
                        Layout.maximumWidth: 130
                    }

                    // Opens the failure reason for a job that has no clip rows to
                    // carry an alert icon of their own.
                    IconButton {
                        visible: section.infoClickable
                        iconName: "alert-circle"
                        boxSize: 22
                        glyphSize: 14
                        restColor: Theme.danger
                        hoverColor: Theme.danger
                        onClicked: section.infoRequested()
                    }
                }
            }

            ColumnLayout {
                id: sectionBody
                visible: section.expanded
                Layout.fillWidth: true
                Layout.leftMargin: 6
                Layout.rightMargin: 6
                Layout.topMargin: 5
                Layout.bottomMargin: 6
                spacing: 1
            }
        }
    }

    // One clip inside an open section. Flat and unhovered on purpose: there is
    // nothing to click here, so a highlight would only promise one.
    //
    // The id is `jobClip` and not `clip` because every child Item here has a
    // `clip` property of its own. The id still wins an unqualified lookup
    // (checked on 6.11), so the old name worked - but it made every
    // `clip.something` inside a child a question worth stopping over, and
    // qmllint reads it as shadowing.
    component ClipRow: Rectangle {
        id: jobClip

        property string clipName: ""
        property string clipStatus: ""
        property url thumbnailUrl: ""
        property real progress: 0
        property bool active: false
        // The failure explanation for this clip, shown when its alert icon is
        // clicked. Empty leaves the icon inert - a complete or queued row has
        // nothing to open.
        property string failReason: ""
        signal detailRequested()
        readonly property bool complete: clipStatus.indexOf("Complete") >= 0
        readonly property bool failed: clipStatus.indexOf("Failed") >= 0
        readonly property bool queued: clipStatus === "Queued"

        implicitHeight: 44
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 50
                Layout.preferredHeight: 30
                radius: 3
                color: Theme.bgPrimary
                border.width: 1
                border.color: jobClip.active ? Theme.accent : Theme.outline
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: jobClip.thumbnailUrl
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    visible: status === Image.Ready
                }

                Image {
                    anchors.centerIn: parent
                    source: "../../assets/icons/film.svg"
                    sourceSize.width: 14
                    sourceSize.height: 14
                    opacity: 0.3
                    visible: jobClip.thumbnailUrl.toString() === ""
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: jobClip.clipName
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsMd
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Text {
                    text: jobClip.clipStatus
                    color: jobClip.active ? Theme.textSecondary : Theme.textMuted
                    font.pixelSize: Theme.fsXs
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            // Only the clip being worked on gets a track: a queued or finished
            // row says so in its label, and an empty or full bar beside it is
            // one more line to scan past.
            Rectangle {
                visible: jobClip.active
                Layout.preferredWidth: 64
                Layout.preferredHeight: 4
                radius: 2
                color: Theme.bgPrimary

                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, jobClip.progress))
                    height: parent.height
                    radius: parent.radius
                    color: Theme.accent
                }
            }

            IconButton {
                visible: !jobClip.active
                iconName: jobClip.complete ? "check"
                                           : jobClip.failed ? "alert-circle"
                                           : jobClip.queued ? "clock" : "x"
                boxSize: 20
                glyphSize: 13
                restColor: jobClip.complete ? "#78c98a"
                                            : jobClip.failed ? Theme.danger : Theme.textMuted
                hoverColor: jobClip.failed ? Theme.danger : restColor
                // A failed row is the only one worth clicking: its icon opens the
                // reason the strip had no room to print. Everything else here is
                // status, not a control, so it stays inert (arrow cursor, no-op).
                enabled: jobClip.failed && jobClip.failReason !== ""
                onClicked: if (jobClip.failed) jobClip.detailRequested()
            }
        }
    }
    // Footer buttons in the Export panel's language rather than the old 2px
    // stadium outline, so the sheet's controls and the export bar's read as the
    // same set of buttons.
    component GhostButton: Button {
        implicitHeight: 28
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: true
        HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? Theme.textSecondary : Theme.textMuted
            font.pixelSize: Theme.fsSm
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: parent.hovered && parent.enabled ? Theme.hover : "transparent"
        }
    }

    component SheetButton: Button {
        implicitHeight: 30
        leftPadding: 16
        rightPadding: 16
        hoverEnabled: true
        HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Theme.fsSm
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: parent.down ? Qt.darker(Theme.hover, 1.2)
                               : parent.hovered ? Theme.hover : Theme.bgSidebar
            border.width: 1
            border.color: Theme.outline
        }
    }
}
