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

    property int subTab: 0
    property string transcriptSearch: ""
    property int editingTranscriptIndex: -1
    property var whisperModels: ["tiny", "base", "small", "medium", "large-v3", "turbo"]
    property var languageLabels: ["Auto detect", "English", "Chinese", "Khmer", "Spanish", "French", "German", "Japanese", "Korean", "Vietnamese"]
    property var languageCodes: ["auto", "en", "zh", "km", "es", "fr", "de", "ja", "ko", "vi"]

    function indexOfValue(values, wanted) {
        var index = values.indexOf(wanted)
        return index < 0 ? 0 : index
    }

    property var selectedMediaForTranscription: []
    property var transcriptionSourceMedia: []
    property var transcriptionQueue: []
    property var transcriptionJobs: []
    property string transcriptionCurrentId: ""
    property bool timelineTranscriptionMode: false
    onTranscriptionJobsChanged: progressJobsChanged(transcriptionJobs, transcriptionCurrentId)
    onTranscriptionCurrentIdChanged: progressJobsChanged(transcriptionJobs, transcriptionCurrentId)
    property bool clipsExpanded: true
    property string sourceClipSearch: ""
    property var ownerWindow: null

    Connections {
        target: Backend
        function onMediaChanged() {
            root.syncTranscriptionSelection()
        }
        function onClipsChanged() {
            root.syncTranscriptionSelection()
        }
        function onTranscriptionFinished(success, mediaId) {
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
    }
    
    Component.onCompleted: {
        root.syncTranscriptionSelection()
    }

    function timelineSourceIds() {
        var ids = []
        var seen = {}
        for (var i = 0; i < Backend.clips.length; ++i) {
            var clip = Backend.clips[i]
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
        for (var i = 0; i < Backend.media.length; ++i) {
            var media = Backend.media[i]
            if (String(media.id || "") !== mediaId)
                continue
            var path = String(media.path || "").replace(/\\/g, "/").toLowerCase()
            return media.excludeFromTranscript !== true
                    && String(media.generatedBy || "") !== "text_to_speech"
                    && path.indexOf("/generated-speech/") < 0
        }
        return false
    }

    function timelineSourceDuration(mediaId) {
        var total = 0
        for (var i = 0; i < Backend.clips.length; ++i) {
            var clip = Backend.clips[i]
            if (String(clip.mediaId || "") === mediaId
                    && (clip.kind === "video" || clip.kind === "audio"))
                total += Math.max(0, Number(clip.durationMs || 0))
        }
        if (total > 0)
            return total
        for (var j = 0; j < Backend.media.length; ++j)
            if (Backend.media[j].id === mediaId)
                return Number(Backend.media[j].durationMs || 0)
        return 0
    }

    function syncTranscriptionSelection() {
        var timelineIds = root.timelineSourceIds()
        var sources = []
        if (timelineIds.length > 0) {
            root.timelineTranscriptionMode = true
            for (var i = 0; i < Backend.media.length; ++i)
                if (timelineIds.indexOf(Backend.media[i].id) >= 0)
                    sources.push(Backend.media[i])
            root.transcriptionSourceMedia = sources
            root.selectedMediaForTranscription = timelineIds
            return
        }
        root.timelineTranscriptionMode = false
        var all = []
        for (var j = 0; j < Backend.media.length; ++j) {
            if (root.isHumanSpeechSource(String(Backend.media[j].id || ""))) {
                all.push(Backend.media[j].id)
                sources.push(Backend.media[j])
            }
        }
        root.transcriptionSourceMedia = sources
        root.selectedMediaForTranscription = all
    }

    function mediaName(mediaId) {
        for (var i = 0; i < Backend.media.length; ++i)
            if (Backend.media[i].id === mediaId)
                return Backend.media[i].name
        return mediaId
    }

    function mediaDuration(mediaId) {
        for (var i = 0; i < Backend.media.length; ++i)
            if (Backend.media[i].id === mediaId)
                return Number(Backend.media[i].durationMs || 0)
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
                    mediaId, modelSelector.currentText,
                    languageCodes[languageSelector.currentIndex])
        if (!started) {
            updateJob(mediaId, "Failed", Backend.lastError || "Could not start")
            transcriptionCurrentId = ""
        }
    }

    function startTranscriptionBatch() {
        var ids = selectedMediaForTranscription.slice()
        var jobs = []
        for (var i = 0; i < ids.length; ++i)
            jobs.push({ id: ids[i], name: mediaName(ids[i]), status: "Queued", detail: "",
                        durationMs: root.timelineTranscriptionMode
                                     ? root.timelineSourceDuration(ids[i])
                                     : root.mediaDuration(ids[i]),
                        thumbnailUrl: root.mediaThumbnail(ids[i]) })
        transcriptionJobs = jobs
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
        title: "Import subtitles"
        fileMode: FileDialog.OpenFile
        nameFilters: ["SubRip subtitles (*.srt)", "All files (*)"]
        onAccepted: Backend.importSubtitles(selectedFile.toString())
    }

    FileDialog {
        id: exportSubtitleDialog
        title: "Export transcript as SRT"
        fileMode: FileDialog.SaveFile
        nameFilters: ["SubRip subtitles (*.srt)"]
        onAccepted: Backend.exportTranscriptSrt(selectedFile.toString())
    }

    TranscriptTranslationDialog {
        id: translationDialog
        ownerWindow: root.ownerWindow
        onTranslationRequested: languageCode => Backend.translateTranscript(languageCode)
        onTranslationConfiguredRequested: (languageCode, settings) =>
            Backend.translateTranscript(languageCode, settings)
        onProviderTestRequested: settings => Backend.testTranslationProvider(settings)
    }

    Menu {
        id: transcriptMenu
        width: 220
        padding: 4

        background: Rectangle {
            color: Theme.bgPanel
            border.color: "#444444"
            radius: Theme.radiusSm
        }

        DarkMenuItem {
            text: "Import subtitles..."
            onTriggered: importSubtitleDialog.open()
        }
        DarkMenuItem {
            text: "Export SRT..."
            enabled: Backend.transcript.length > 0
            onTriggered: exportSubtitleDialog.open()
        }
        MenuSeparator {
            contentItem: Rectangle {
                implicitHeight: 1
                color: "#444444"
            }
        }
        DarkMenuItem {
            text: Backend.translationInProgress
                  ? "Cancel translation"
                  : "Translate transcript..."
            enabled: Backend.transcript.length > 0
                     && !Backend.transcriptionInProgress
            onTriggered: Backend.translationInProgress
                         ? Backend.cancelTranslation()
                         : translationDialog.open()
        }
    }

    function timestamp(seconds) {
        var value = Math.max(0, Number(seconds) || 0)
        var milliseconds = Math.floor((value % 1) * 1000)
        var totalSeconds = Math.floor(value)
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
                    tabs: ["Effect Controls", "Lumetri Color", "Text"]
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

            Item {
                anchors.fill: parent
                visible: propTabs.currentIndex === 0

                ClipEffectControlsPanel {
                    anchors.fill: parent
                }
            }

            Item {
                anchors.fill: parent
                visible: propTabs.currentIndex === 1

                LumetriColorPanel { anchors.fill: parent }
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

                        Repeater {
                            model: ["Transcript", "Captions", "Graphics", "Text to Speech"]
                            delegate: Item {
                                id: sub
                                required property int index
                                required property string modelData
                                readonly property bool active: root.subTab === index
                                Layout.fillHeight: true
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
                                TapHandler { cursorShape: Qt.PointingHandCursor; onTapped: root.subTab = sub.index }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            visible: root.subTab === 0
                            text: Backend.transcriptionInProgress
                                  ? "Transcribing"
                                  : Backend.translationInProgress
                                    ? "Translating"
                                  : Backend.transcript.length > 0
                                    ? Backend.transcript.length + " segments"
                                    : "No transcript"
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsSm
                        }
                        IconButton {
                            visible: root.subTab === 0 && Backend.transcript.length > 0
                            iconName: "subtitles"
                            boxSize: 24
                            glyphSize: 15
                            hoverEnabled: true
                            active: Backend.hasSubtitleClips
                            restColor: "white"
                            ToolTip.visible: hovered
                            ToolTip.text: Backend.hasSubtitleClips
                                          ? "Remove subtitles from timeline"
                                          : "Add subtitles to timeline"
                            onClicked: Backend.hasSubtitleClips
                                       ? Backend.removeTranscriptFromTimeline()
                                       : Backend.addTranscriptToTimeline()
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
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        placeholder: "Search transcript..."
                        onTextChanged: root.transcriptSearch = text.toLowerCase()
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
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 6
                        visible: Backend.hasMedia

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Model"; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            WhisperComboBox {
                                id: modelSelector
                                Layout.preferredWidth: 96
                                model: root.whisperModels
                                itemStatusIconSource: "../../assets/icons/cloud-download.svg"
                                itemStatusIconVisible: function(modelName, index) {
                                    return Backend.downloadedWhisperModels.indexOf(
                                                String(modelName)) < 0
                                }
                                currentIndex: root.indexOfValue(
                                                  root.whisperModels,
                                                  Backend.appSettings.transcriptionModel)
                                popupWidth: 130
                            }
                            Text { text: "Language"; color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                            WhisperComboBox {
                                id: languageSelector
                                Layout.fillWidth: true
                                model: root.languageLabels
                                currentIndex: root.indexOfValue(
                                                  root.languageCodes,
                                                  Backend.appSettings.transcriptionLanguage)
                                popupWidth: 190
                            }
                            Button {
                                HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                implicitHeight: 32
                                implicitWidth: 104
                                padding: 0
                                text: Backend.transcriptionInProgress ? "Stop" : "Transcribe"
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
                    }

                    ListView {
                        id: transcriptList
                        anchors.fill: parent
                        anchors.topMargin: 76
                        clip: true
                        visible: !Backend.transcriptionInProgress
                                 && !Backend.translationInProgress
                        model: Backend.transcript
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
                            color: rowTap.hovered ? Theme.hover : "transparent"

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
                                    visible: root.editingTranscriptIndex !== transcriptRow.index
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
                                    visible: root.editingTranscriptIndex === transcriptRow.index
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
                            HoverHandler { id: rowTap }
                            TapHandler {
                                cursorShape: Qt.PointingHandCursor
                                enabled: root.editingTranscriptIndex !== transcriptRow.index
                                onTapped: Backend.playheadMs = transcriptRow.modelData.start * 1000
                                onDoubleTapped: {
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
                        anchors.fill: parent
                        anchors.topMargin: 76
                        visible: Backend.transcriptionInProgress
                                 || Backend.translationInProgress
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.topMargin: 76
                        visible: transcriptList.count === 0
                                 && Backend.durationMs > 0
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
                                    text: root.timelineTranscriptionMode
                                          ? root.selectedMediaForTranscription.length + " timeline source clips"
                                          : Backend.mediaCount + " source clips have not been transcribed"
                                    color: Theme.textPrimary 
                                    font.pixelSize: Theme.fsSm  
                                    Layout.fillWidth: true  
                                }
                                Text {
                                    text: root.selectedMediaForTranscription.length + " selected"
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
                                    placeholder: "Filter source clips"
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
                                        Text { text: "Name"; color: Theme.textPrimary; font.pixelSize: Theme.fsSm; Layout.fillWidth: true }
                                        Text { text: "Duration"; color: Theme.textPrimary; font.pixelSize: Theme.fsSm; Layout.rightMargin: 16 }
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
                                        height: 40
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
                                            Text {
                                                text: modelData.name || "Media"
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
                                 && Backend.durationMs === 0
                                 && !Backend.transcriptionInProgress
                                 && !Backend.translationInProgress
                        
                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 16
                            Text {
                                text: "No dialogue found"
                                color: Theme.textPrimary
                                font.pixelSize: 18
                                Layout.alignment: Qt.AlignHCenter
                            }
                            Text {
                                text: "To enable transcription services, your audio must contain\nunmuted verbal dialogue."
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
                        text: "No graphics selected"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsSm
                    }
                }

                TextToSpeechTimedPanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.subTab === 3
                }
            }
        }
    }
}
