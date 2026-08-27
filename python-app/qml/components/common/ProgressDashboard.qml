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

Dialog {
    id: root
    implicitWidth: 760
    implicitHeight: 520
    padding: 0
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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

    function cancelAll() {
        if (Backend.exportInProgress) Backend.cancelExport()
        if (Backend.translationInProgress) Backend.cancelTranslation()
        if (Backend.transcriptionInProgress) Backend.cancelTranscription()
        if (Backend.demucsInProgress) Backend.cancelDemucs()
        if (Backend.textToSpeechEngine.inProgress) Backend.textToSpeechEngine.cancel()
    }
    function transcriptionCompletedCount() {
        var count = 0
        for (var i = 0; i < transcriptionJobs.length; ++i)
            if (transcriptionJobs[i].status === "Complete") ++count
        return count
    }
    function transcriptionBatchProgress() {
        if (transcriptionJobs.length === 0)
            return Backend.transcriptionInProgress ? Backend.transcriptionProgress : 0
        var totalDuration = 0
        var completedDuration = 0
        for (var i = 0; i < transcriptionJobs.length; ++i) {
            var job = transcriptionJobs[i]
            var duration = Math.max(1, Number(job.durationMs || 0))
            totalDuration += duration
            completedDuration += job.status === "Complete" ? duration : (job.id === transcriptionCurrentId ? duration * Backend.transcriptionProgress : 0)
        }
        return totalDuration > 0 ? Math.max(0, Math.min(1, completedDuration / totalDuration)) : 0
    }
    function statusText(job) {
        if (job.status === "Complete") return "100% | Complete"
        if (job.status === "Failed") return "Failed"
        if (job.status === "Cancelled") return "Cancelled"
        if (job.id === transcriptionCurrentId && Backend.transcriptionInProgress)
            return Math.round(Backend.transcriptionProgress * 100) + "% | Transcribing"
        return "Queued"
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
                    + (vocalEtaText() !== "" ? " | " + vocalEtaText() : "")
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
        if (seconds < 60) return "About " + seconds + " sec remaining"
        var minutes = Math.max(1, Math.round(seconds / 60))
        if (minutes < 60) return "About " + minutes + " min remaining"
        var hours = Math.floor(minutes / 60)
        var rest = minutes % 60
        return "About " + hours + " hr" + (rest > 0 ? " " + rest + " min" : "") + " remaining"
    }
    function vocalEtaText() {
        return formatRemaining(vocalRemainingMilliseconds())
    }

    Timer {
        interval: 1000
        repeat: true
        running: Backend.demucsInProgress
        onTriggered: root.etaClock = Date.now()
    }

    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle { color: Theme.bgPanel; border.color: "#505050"; border.width: 1; radius: 6 }

    contentItem: ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.bgSidebar
            topLeftRadius: 6; topRightRadius: 6
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 18
                Text { text: "Progress Dashboard"; color: Theme.textPrimary; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Text { text: root.hasActiveJobs ? "In progress" : "Recent activity"; color: root.hasActiveJobs ? Theme.accent : Theme.textMuted; font.pixelSize: Theme.fsSm }
                ToolButton {
                    text: "×"; implicitWidth: 28; implicitHeight: 28; hoverEnabled: true; onClicked: root.close()
                    background: Rectangle { radius: 4; color: parent.hovered ? Theme.hover : "transparent" }
                    contentItem: Text { text: parent.text; color: Theme.textSecondary; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }

        Flickable {
            Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: width; contentHeight: jobsColumn.implicitHeight; clip: true
            ColumnLayout {
                id: jobsColumn
                width: parent.width; spacing: 10; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                anchors.topMargin: 18; anchors.leftMargin: 20; anchors.rightMargin: 20

                GroupRow {
                    Layout.fillWidth: true
                    visible: root.transcriptionJobs.length > 0 || Backend.transcriptionInProgress || Backend.transcriptionStatus !== ""
                    title: "Speech to Text Transcriptions"
                    iconName: "subtitles"
                    expanded: root.transcriptionExpanded
                    percent: root.transcriptionBatchProgress()
                    percentText: Backend.transcriptionInProgress ? Math.round(root.transcriptionBatchProgress() * 100) + "%" : (root.transcriptionJobs.length > 0 ? root.transcriptionCompletedCount() + "/" + root.transcriptionJobs.length : "Complete")
                    onToggle: root.transcriptionExpanded = !root.transcriptionExpanded
                }

                Repeater {
                    model: root.transcriptionExpanded ? root.transcriptionJobs : []
                    delegate: ClipRow {
                        required property var modelData
                        Layout.fillWidth: true
                        clipName: modelData.name
                        thumbnailUrl: modelData.thumbnailUrl || ""
                        clipStatus: root.statusText(modelData)
                        progress: modelData.status === "Complete" ? 1 : (modelData.id === root.transcriptionCurrentId ? Number(Backend.transcriptionProgress || 0) : 0)
                        active: modelData.id === root.transcriptionCurrentId && Backend.transcriptionInProgress
                    }
                }

                GroupRow {
                    Layout.fillWidth: true; visible: Backend.mediaImportInProgress
                    title: "Media imports"; iconName: "import"; expanded: false; percent: Backend.mediaImportProgress / 100; percentText: Backend.mediaImportProgress + "%"; expandable: false
                }
                GroupRow {
                    Layout.fillWidth: true; visible: Backend.translationInProgress
                    title: "Transcript translation"; iconName: "type"; expanded: false; percent: -1; percentText: "Running"; expandable: false
                }
                GroupRow {
                    Layout.fillWidth: true; visible: Backend.textToSpeechEngine.inProgress
                    title: "Text to Speech"; iconName: "volume-2"; expanded: false
                    percent: Backend.textToSpeechEngine.progress
                    percentText: Math.round(Backend.textToSpeechEngine.progress * 100) + "%"
                    detailText: Backend.textToSpeechEngine.status
                    busy: true; expandable: false
                }
                GroupRow {
                    Layout.fillWidth: true; visible: Backend.exportInProgress
                    title: "Export"; iconName: "file-up"; expanded: false; percent: Backend.exportProgress; percentText: Math.round(Backend.exportProgress * 100) + "%"; expandable: false
                }
                GroupRow {
                    Layout.fillWidth: true; visible: Backend.demucsInProgress || Backend.demucsStatus !== ""
                    title: "Remove Vocals"; iconName: "volume-x"; expanded: root.vocalRemovalExpanded
                    percent: root.vocalRemovalJobs.length > 0 ? root.vocalBatchProgress() : (Backend.demucsInProgress ? Backend.demucsProgress : -1)
                    percentText: root.vocalRemovalJobs.length > 0
                                 ? (Backend.demucsInProgress
                                    ? Math.min(99, Math.round(root.vocalBatchProgress() * 100)) + "%"
                                    : (root.vocalCompletedCount() === root.vocalRemovalJobs.length
                                       ? "100% | Complete"
                                       : root.vocalCompletedCount() + "/" + root.vocalRemovalJobs.length))
                                 : (Backend.demucsInProgress
                                    ? Math.min(99, Math.round(Backend.demucsProgress * 100)) + "%"
                                    : Backend.demucsStatus)
                    detailText: Backend.demucsInProgress
                                ? (root.vocalEtaText() !== "" ? root.vocalEtaText() : "Estimating time remaining...")
                                : Backend.demucsStatus
                    busy: Backend.demucsInProgress
                    onToggle: root.vocalRemovalExpanded = !root.vocalRemovalExpanded
                }
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
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.preferredHeight: 240
                    visible: !root.hasActiveJobs && root.transcriptionJobs.length === 0 && root.vocalRemovalJobs.length === 0 && Backend.transcriptionStatus === "" && Backend.translationStatus === "" && Backend.exportStatus === ""
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8
                        Text {
                            text: "No background processes"
                            color: Theme.textPrimary
                            font.pixelSize: 13
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: "Completed and active work will appear here."
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsSm
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 70; color: Theme.bgPanel; bottomLeftRadius: 6; bottomRightRadius: 6
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 22
                Button {
                    text: "Clear all completed"; flat: true; enabled: !root.hasActiveJobs && (root.transcriptionJobs.length > 0 || Backend.transcriptionStatus !== "" || Backend.translationStatus !== "" || Backend.exportStatus !== "")
                    contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textSecondary : Theme.textMuted; font.pixelSize: Theme.fsSm; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: "transparent" }
                    onClicked: { root.clearCompletedRequested = true; root.transcriptionJobs = [] }
                    Layout.fillWidth: true
                }
                Button {
                    text: "Cancel all"; enabled: root.hasCancellableJobs; implicitWidth: 110; implicitHeight: 40; hoverEnabled: true
                    background: Rectangle { radius: 20; color: parent.hovered ? "#505050" : "transparent"; border.color: parent.enabled ? Theme.textSecondary : Theme.border; border.width: 2 }
                    contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textMuted; font.pixelSize: Theme.fsSm; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: root.cancelAll()
                }
            }
        }
    }

    component GroupRow: Rectangle {
        id: group
        implicitHeight: group.detailText !== "" ? 114 : 94
        color: "#414141"; radius: 10; border.color: "#464646"; border.width: 1
        property string title: ""; property string iconName: "subtitles"; property real percent: -1; property string percentText: ""; property string detailText: ""; property bool expanded: false; property bool expandable: true; property bool busy: false
        signal toggle()
        MouseArea {
            anchors.fill: parent
            z: 3
            cursorShape: group.expandable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (group.expandable) group.toggle()
        }
        ColumnLayout {
            anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18; anchors.topMargin: 14; anchors.bottomMargin: 14; spacing: 7
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                                Image {
                                        source: group.expandable
                                                        ? "../../assets/icons/"
                                                            + (group.expanded ? "chevron-down" : "chevron-right")
                                                            + ".svg"
                                                        : ""
                                        sourceSize.width: 13
                                        sourceSize.height: 13
                                        Layout.preferredWidth: 15
                                        Layout.preferredHeight: 15
                                        fillMode: Image.PreserveAspectFit
                                        opacity: 0.78
                                }
                IconButton { iconName: group.iconName; boxSize: 30; glyphSize: 18; restColor: Theme.textPrimary; hoverEnabled: false }
                Text { text: group.title; color: Theme.textPrimary; font.pixelSize: 16; Layout.fillWidth: true; elide: Text.ElideRight }
                Item {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    visible: group.busy
                    Repeater {
                        model: 8
                        delegate: Rectangle {
                            required property int index
                            width: 3; height: 7; radius: 2
                            color: Theme.textPrimary
                            opacity: 0.22 + index * 0.09
                            x: 9.5
                            y: 1
                            transform: [
                                Rotation { origin.x: 1.5; origin.y: 10; angle: index * 45 },
                                Rotation {
                                    origin.x: 1.5; origin.y: 10
                                    angle: spinnerRotation.angle
                                }
                            ]
                        }
                    }
                    NumberAnimation {
                        id: spinnerRotation
                        target: spinnerRotation
                        property: "angle"
                        from: 0; to: 360; duration: 850
                        loops: Animation.Infinite
                        running: group.busy
                        property real angle: 0
                    }
                }
                Text { text: group.percentText; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            }
            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 57; height: 7; radius: 3; color: "#505050"; visible: group.percent >= 0; Rectangle { width: parent.width * Math.max(0, Math.min(1, group.percent)); height: parent.height; radius: 3; color: Theme.accent } }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 57
                text: group.detailText
                visible: text !== ""
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
                elide: Text.ElideRight
            }
        }
    }

    component ClipRow: Rectangle {
        id: clip
        implicitHeight: 72; color: "transparent"
        property string clipName: ""; property string clipStatus: ""; property url thumbnailUrl: ""; property real progress: 0; property bool active: false
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 30; anchors.rightMargin: 18; spacing: 12
            Rectangle {
                Layout.preferredWidth: 78
                Layout.preferredHeight: 46
                radius: 4
                color: Theme.bgPrimary
                border.color: clip.active ? Theme.accent : Theme.border
                clip: true
                Image {
                    anchors.fill: parent
                    source: clip.thumbnailUrl
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    visible: status === Image.Ready
                }
                IconButton {
                    anchors.centerIn: parent
                    iconName: "film"
                    boxSize: 28
                    glyphSize: 17
                    restColor: Theme.textMuted
                    hoverEnabled: false
                    visible: clip.thumbnailUrl.toString() === ""
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 5
                Text { text: clip.clipName; color: Theme.textPrimary; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideMiddle }
                Text { text: clip.clipStatus; color: Theme.textMuted; font.pixelSize: Theme.fsXs; Layout.fillWidth: true; elide: Text.ElideRight }
            }
            BusyIndicator { running: clip.active; visible: clip.active; implicitWidth: 22; implicitHeight: 22 }
            IconButton {
                iconName: clip.active ? "more-horizontal" : (clip.clipStatus.indexOf("Complete") >= 0 ? "check" : "alert-circle")
                boxSize: 24
                glyphSize: 15
                restColor: clip.active ? Theme.accent : (clip.clipStatus.indexOf("Complete") >= 0 ? "#78c98a" : Theme.textMuted)
                hoverEnabled: false
                visible: !clip.active
            }
        }
    }
}
