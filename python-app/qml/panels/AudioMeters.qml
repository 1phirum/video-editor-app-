
pragma ComponentBehavior: Bound
// qmllint disable

import QtQuick
import QtQuick.Layouts
import CutPro 1.0
import "../theme"

Rectangle {
    id: root
    color: Theme.bgPrimary

    readonly property bool hasClip: Backend.clips.length > 0
    readonly property bool playbackActive:
        Backend.playing && hasAudibleClipAt(Backend.playheadMs)
    property real levelL: 0
    property real levelR: 0
    property real peakL: 0
    property real peakR: 0

    function hasAudibleClipAt(position) {
        // Re-evaluated on every playhead tick, so it walks the media clips only:
        // reading Backend.clips per iteration re-wrapped the whole list each
        // time, and a subtitle track made that twenty thousand wrappers per tick.
        var clips = Backend.mediaClips
        var muted = Backend.mutedTracks
        for (var i = 0; i < clips.length; ++i) {
            var clip = clips[i]
            if (clip.enabled === false
                    || muted.indexOf(String(clip.track)) >= 0
                    || position < clip.startMs
                    || position >= clip.startMs + clip.durationMs)
                continue
            // O(1) in C++. This was a nested walk of the whole bin per candidate
            // clip per tick, and a generated voice track fills the bin with one
            // entry per spoken cue.
            var media = Backend.mediaById(String(clip.mediaId || ""))
            if (media && Number(media.channels) > 0)
                return true
        }
        return false
    }

    onPlaybackActiveChanged: {
        if (!playbackActive) {
            levelL = 0
            levelR = 0
            peakL = 0
            peakR = 0
        }
    }

    Behavior on levelL { NumberAnimation { duration: 90 } }
    Behavior on levelR { NumberAnimation { duration: 90 } }

    Timer {
        interval: Number(Backend.appSettings.audioMeterRefreshMs || 110)
        running: root.playbackActive
        repeat: true
        onTriggered: {
            root.levelL = Math.max(0.02, Math.min(0.98, root.levelL + (Math.random() - 0.48) * 0.35))
            root.levelR = Math.max(0.02, Math.min(0.98, root.levelR + (Math.random() - 0.48) * 0.35))
            root.peakL = Math.max(root.levelL, root.peakL * 0.96)
            root.peakR = Math.max(root.levelR, root.peakR * 0.96)
        }
    }



    // A single meter bar with the green→yellow→red level gradient.
    component Meter: Rectangle {
        id: meter
        property real level: 0
        property real peak: 0
        width: 8
        radius: 2
        color: "#101010"
        border.width: 1
        border.color: Theme.border
        clip: true

        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * meter.level
            clip: true

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: meter.height
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.meterRed }
                    GradientStop { position: 0.2; color: Theme.meterYellow }
                    GradientStop { position: 0.4; color: Theme.meterGreen }
                    GradientStop { position: 1.0; color: Theme.meterGreen }
                }
            }
        }

        // Peak-hold marker
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.rgba(1, 1, 1, 0.8)
            y: meter.height * (1 - meter.peak) - 0.5
            visible: meter.peak > 0.001
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: Theme.bgSidebar
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "Audio Meters"
                color: Theme.textPrimary
                font.pixelSize: Theme.fsSm
                font.weight: Font.DemiBold
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        // Body
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 10
            Layout.bottomMargin: 8
            spacing: 6

            // dB scale
            Item {
                id: scale
                Layout.preferredWidth: 18
                Layout.fillHeight: true
                Repeater {
                    model: [0, -6, -12, -18, -24, -30, -42, -54]
                    delegate: Text {
                        required property int index
                        required property int modelData
                        text: modelData
                        color: Theme.textMuted
                        font.pixelSize: 8
                        x: scale.width - implicitWidth
                        y: (scale.height - implicitHeight) * (index / 7)
                    }
                }
            }

            // Meters
            RowLayout {
                id: meterColumns
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignHCenter
                spacing: 5
                ColumnLayout {
                    Layout.fillHeight: true
                    spacing: 4
                    Meter { Layout.fillHeight: true; level: root.levelL; peak: root.peakL }
                    Text { text: "L"; color: Theme.textMuted; font.pixelSize: 8; Layout.alignment: Qt.AlignHCenter }
                }
                ColumnLayout {
                    Layout.fillHeight: true
                    spacing: 4
                    Meter { Layout.fillHeight: true; level: root.levelR; peak: root.peakR }
                    Text { text: "R"; color: Theme.textMuted; font.pixelSize: 8; Layout.alignment: Qt.AlignHCenter }
                }
            }
        }
    }
}
