// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"

// Lumetri Scopes: measurement instruments for the graded program frame. The
// graph itself is drawn C++-side by the "scope" image provider from the frame
// the monitor shows; this panel picks the instrument and draws the crisp axis
// labels over it. It re-fetches whenever the frame revision changes.
Item {
    id: root

    readonly property var scopeIds: [
        "waveform-luma", "waveform-rgb", "parade",
        "vectorscope", "histogram-luma", "histogram-rgb"
    ]
    readonly property int scopeIndex: typeBox.currentIndex
    readonly property bool isVectorscope: scopeIds[scopeIndex] === "vectorscope"

    Rectangle {
        anchors.fill: parent
        color: Theme.bgPanel
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("Scope")
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }
            DarkComboBox {
                id: typeBox
                Layout.fillWidth: true
                currentIndex: 0
                model: [qsTr("Waveform (Luma)"), qsTr("Waveform (RGB)"),
                        qsTr("RGB Parade"), qsTr("Vectorscope (YUV)"),
                        qsTr("Histogram (Luma)"), qsTr("Histogram (RGB)")]
            }
        }

        // The graph area: a dark frame, an IRE gutter (hidden for the
        // vectorscope), the provider-drawn scope, and a caption.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#0e0e0e"
            border.width: 1
            border.color: Theme.border
            radius: Theme.radiusSm
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                // Left IRE scale (0-100), aligned to the graph rows.
                Item {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 22
                    visible: !root.isVectorscope
                    Repeater {
                        model: [100, 80, 60, 40, 20, 0]
                        delegate: Text {
                            required property int modelData
                            required property int index
                            text: modelData
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsXs
                            anchors.right: parent.right
                            y: (parent.height - height) * (index / 5)
                        }
                    }
                }

                Image {
                    id: graph
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    fillMode: root.isVectorscope ? Image.PreserveAspectFit
                                                 : Image.Stretch
                    cache: false
                    // Asynchronous: Qt Quick calls the scope provider's
                    // requestImage() on its own image-reader thread for async
                    // Images, so the whole grade+render+blur runs OFF the GUI
                    // thread and never competes with playback. That is what keeps
                    // the editor responsive while this tab is open.
                    asynchronous: true
                    // ...but async loads normally blank the item while they fetch,
                    // which is why the graph vanished during playback before. Qt
                    // 6.8+ keeps the last frame on screen until the next one is
                    // ready, so the graph stays visible and just updates in place -
                    // no blank while playing, no blink on a splitter drag.
                    retainWhileLoading: true
                    smooth: true

                    // The graph is rendered at a fixed resolution and scaled to
                    // whatever size the panel is. A constant sourceSize means a
                    // splitter drag only rescales the pixmap already on screen, and
                    // 720x420 is finer than the panel usually is so scaling stays
                    // crisp while bounding the worker-thread render cost.
                    sourceSize: Qt.size(720, 420)

                    // Sample the frame revision at ~12 fps rather than chasing every
                    // decoded frame: 12x/s still reads as live and bounds how often
                    // the worker re-renders. When paused the revision stops changing,
                    // so this settles to no work at all.
                    property int liveRev: 0
                    property int lastRev: -1
                    Timer {
                        interval: 80
                        repeat: true
                        running: true
                        onTriggered: {
                            var r = Backend.previewFrameRevision;
                            if (r !== graph.lastRev) {
                                graph.lastRev = r;
                                graph.liveRev = r;
                            }
                        }
                    }

                    source: "image://scope/" + root.scopeIds[root.scopeIndex]
                            + "?rev=" + graph.liveRev
                }
            }
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            text: root.isVectorscope ? qsTr("BT.601 · 75% targets")
                                     : qsTr("Rec. 709 · 0-100 IRE")
            color: Theme.textMuted
            font.pixelSize: Theme.fsXs
        }
    }
}
