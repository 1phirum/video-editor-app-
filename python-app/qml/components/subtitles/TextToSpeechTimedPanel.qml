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

    property string language: "en"
    property string gender: "female"

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

    function timecode(ms) {
        var total = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        var h = Math.floor(total / 3600)
        var m = Math.floor(total / 60) % 60
        var s = total % 60
        function pad(v) { return v < 10 ? "0" + v : String(v) }
        return pad(h) + ":" + pad(m) + ":" + pad(s)
    }

    property var segments: subtitleSegments()
    Connections {
        target: Backend
        function onClipsChanged() { root.segments = root.subtitleSegments() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Text {
            text: "Text to Speech"
            color: Theme.textPrimary
            font.pixelSize: Theme.fsLg
            font.weight: Font.DemiBold
        }
        Text {
            text: root.segments.length > 0
                  ? root.segments.length + " subtitle clips connected"
                  : "Add subtitle clips to the timeline first"
            color: root.segments.length > 0 ? Theme.textMuted : "#e88787"
            font.pixelSize: Theme.fsSm
        }

        ListView {
            id: subtitleList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.segments
            spacing: 4
            delegate: Rectangle {
                required property var modelData
                width: subtitleList.width
                height: 42
                color: Theme.bgPrimary
                border.color: Theme.border
                radius: Theme.radiusSm
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    Text {
                        text: root.timecode(modelData.startMs)
                        color: Theme.accent
                        font.pixelSize: Theme.fsXs
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsSm
                        elide: Text.ElideRight
                    }
                }
            }
            visible: count > 0
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            Text { text: "Language"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            WhisperComboBox {
                Layout.fillWidth: true
                model: ["English", "Chinese", "Khmer"]
                currentIndex: root.language === "en" ? 0 : (root.language === "zh" ? 1 : 2)
                onActivated: root.language = ["en", "zh", "km"][currentIndex]
            }
            Text { text: "Voice"; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            WhisperComboBox {
                Layout.fillWidth: true
                model: ["Female", "Male"]
                currentIndex: root.gender === "male" ? 1 : 0
                onActivated: root.gender = currentIndex === 1 ? "male" : "female"
            }
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
                text: Backend.textToSpeechEngine.inProgress ? "Cancel" : "Generate"
                enabled: Backend.textToSpeechEngine.inProgress || root.segments.length > 0
                onClicked: Backend.textToSpeechEngine.inProgress
                           ? Backend.textToSpeechEngine.cancel()
                           : Backend.generateTimedTextToSpeech(root.segments,
                                                               root.language,
                                                               root.gender)
            }
        }
    }
}
