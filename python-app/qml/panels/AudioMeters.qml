
pragma ComponentBehavior: Bound
// qmllint disable

import QtQuick
import QtQuick.Layouts
import CutPro 1.0
import "../theme"

Rectangle {
    id: root
    color: Theme.bgPrimary

    readonly property bool hasClip: Backend.clipCount > 0
    // Meters display the real post-fader PCM peak the preview sink actually played
    // (audioMeterLevels), so this only needs to know playback is running: a silent
    // or muted stretch simply reads zero and the bars ease down. The old gate
    // rescanned every media clip (re-wrapping the whole bin) on each 20 Hz tick,
    // stalling the GUI thread and starving that very sink - exactly what made
    // playback stutter and repeat audio. That per-tick scan is gone.
    readonly property bool playbackActive: Backend.playing && hasClip
    property real levelL: 0
    property real levelR: 0
    property real peakL: 0
    property real peakR: 0
    // levelL/R are the raw target the sampling timer writes at audioMeterRefreshMs
    // (~9 Hz). Bound straight to a bar height that reads as stepping, not a moving
    // level. dispLevelL/R are the smoothed values the bars actually draw: a
    // per-frame clock eases them toward the target with meter ballistics - a fast
    // attack so peaks still register at once, a slower release so the fall glides
    // instead of stepping at the sample rate.
    property real dispLevelL: 0
    property real dispLevelR: 0
    property double lastFrameMs: 0

    // Exponential ease toward target, asymmetric so a rising level snaps up while
    // a falling one drifts down. Frame-rate independent: the coefficient is built
    // from the elapsed seconds, so it behaves the same at 60 or 144 Hz.
    function easeLevel(current, target, dt) {
        var tau = target >= current ? 0.012 : 0.18
        return target + (current - target) * Math.exp(-dt / tau)
    }

    onPlaybackActiveChanged: {
        if (!playbackActive) {
            levelL = 0
            levelR = 0
            dispLevelL = 0
            dispLevelR = 0
            peakL = 0
            peakR = 0
            lastFrameMs = 0
        }
    }

    Timer {
        interval: Number(Backend.appSettings.audioMeterRefreshMs || 110)
        running: root.playbackActive
        repeat: true
        onTriggered: {
            // Real levels: the peak of the PCM the decoder actually played since
            // the last tick, read (and cleared) from the backend. No simulation.
            // This only sets the target - easeLevel in the frame clock below is
            // what the bars follow, so the ~9 Hz sampling never reaches the eye.
            var levels = Backend.audioMeterLevels()
            var l = levels && levels.length > 0 ? Number(levels[0]) : 0
            var r = levels && levels.length > 1 ? Number(levels[1]) : 0
            root.levelL = Math.max(0, Math.min(1, l))
            root.levelR = Math.max(0, Math.min(1, r))
        }
    }

    // Drives the smoothed bars and the peak-hold at display refresh rate. dt comes
    // from the wall clock rather than a FrameAnimation property so the release and
    // peak fall stay the same real speed whatever the monitor's refresh, and a
    // frame that stalled cannot turn into one giant jump.
    FrameAnimation {
        running: root.playbackActive
        onTriggered: {
            var now = Date.now()
            var dt = root.lastFrameMs > 0
                    ? Math.min(0.1, (now - root.lastFrameMs) / 1000)
                    : 0.016
            root.lastFrameMs = now
            root.dispLevelL = root.easeLevel(root.dispLevelL, root.levelL, dt)
            root.dispLevelR = root.easeLevel(root.dispLevelR, root.levelR, dt)
            // Peak-hold sits at the highest recent level and falls back slowly,
            // now a smooth per-second drift instead of a per-tick multiply.
            root.peakL = Math.max(root.dispLevelL, root.peakL - dt * 0.6)
            root.peakR = Math.max(root.dispLevelR, root.peakR - dt * 0.6)
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
                text: qsTr("Audio Meters")
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
                    Meter { Layout.fillHeight: true; level: root.dispLevelL; peak: root.peakL }
                    Text { text: "L"; color: Theme.textMuted; font.pixelSize: 8; Layout.alignment: Qt.AlignHCenter }
                }
                ColumnLayout {
                    Layout.fillHeight: true
                    spacing: 4
                    Meter { Layout.fillHeight: true; level: root.dispLevelR; peak: root.peakR }
                    Text { text: "R"; color: Theme.textMuted; font.pixelSize: 8; Layout.alignment: Qt.AlignHCenter }
                }
            }
        }
    }
}
