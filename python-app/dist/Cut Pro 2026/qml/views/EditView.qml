pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import QtQuick.Controls
import "../theme"
import "../panels"

Item {
    id: editRoot

    signal requestImport()
    signal transcriptionProgressJobsChanged(var jobs, string currentId)
    signal vocalRemovalProgressJobsChanged(var jobs, string currentId)

    // Which editing workspace layout to render ("ESSENTIALS" | "VERTICAL").
    property string layoutPreset: "ESSENTIALS"
    property int activeTimelineTool: 0
    property var ownerWindow: null

    Loader {
        anchors.fill: parent
        sourceComponent: editRoot.layoutPreset === "VERTICAL" ? verticalComp : essentialsComp
    }

    // ---- ESSENTIALS: Project · Monitor · Properties  /  ToolRail · Timeline · Meters
    Component {
        id: essentialsComp

        SplitView {
            orientation: Qt.Vertical
            handle: Rectangle {
                implicitHeight: 3
                color: SplitHandle.pressed ? Theme.accent : Theme.border
            }

            // Upper region
            SplitView {
                SplitView.preferredHeight: editRoot.height * 0.55
                orientation: Qt.Horizontal
                handle: Rectangle {
                    implicitWidth: 3
                    color: SplitHandle.pressed ? Theme.accent : Theme.border
                }

                ProjectPanel {
                    SplitView.preferredWidth: editRoot.width * 0.20
                    onRequestImport: editRoot.requestImport()
                }
                MonitorPanel { SplitView.fillWidth: true }
                TextPanel {
                    ownerWindow: editRoot.ownerWindow
                    SplitView.preferredWidth: editRoot.width * 0.25
                    onProgressJobsChanged: (jobs, currentId) => editRoot.transcriptionProgressJobsChanged(jobs, currentId)
                }
            }

            // Lower region
            SplitView {
                SplitView.fillHeight: true
                orientation: Qt.Horizontal
                handle: Rectangle {
                    implicitWidth: 3
                    color: SplitHandle.pressed ? Theme.accent : Theme.border
                }

                ToolRail {
                    SplitView.preferredWidth: Theme.toolRailWidth
                    SplitView.minimumWidth: Theme.toolRailWidth
                    SplitView.maximumWidth: Theme.toolRailWidth
                    currentTool: editRoot.activeTimelineTool
                    onToolSelected: editRoot.activeTimelineTool = tool
                }
                TimelinePanel {
                    SplitView.fillWidth: true
                    activeTool: editRoot.activeTimelineTool
                    onToolRequested: tool => editRoot.activeTimelineTool = tool
                    onVocalRemovalProgressJobsChanged: (jobs, currentId) => editRoot.vocalRemovalProgressJobsChanged(jobs, currentId)
                }
                AudioMeters {
                    SplitView.preferredWidth: Theme.audioMetersWidth
                    SplitView.minimumWidth: Theme.audioMetersWidth
                    SplitView.maximumWidth: Theme.audioMetersWidth
                }
            }
        }
    }

    // ---- VERTICAL: left cluster (Project·Properties / ToolRail·Timeline·Meters) + tall Monitor
    Component {
        id: verticalComp

        SplitView {
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 3
                color: SplitHandle.pressed ? Theme.accent : Theme.border
            }

            // Left area
            SplitView {
                SplitView.fillWidth: true
                orientation: Qt.Vertical
                handle: Rectangle {
                    implicitHeight: 3
                    color: SplitHandle.pressed ? Theme.accent : Theme.border
                }

                // Top-left: Project | Properties
                SplitView {
                    SplitView.preferredHeight: editRoot.height * 0.55
                    orientation: Qt.Horizontal
                    handle: Rectangle {
                        implicitWidth: 3
                        color: SplitHandle.pressed ? Theme.accent : Theme.border
                    }
                    ProjectPanel {
                        SplitView.preferredWidth: editRoot.width * 0.16
                        onRequestImport: editRoot.requestImport()
                    }
                    TextPanel {
                        ownerWindow: editRoot.ownerWindow
                        SplitView.fillWidth: true
                        onProgressJobsChanged: (jobs, currentId) => editRoot.transcriptionProgressJobsChanged(jobs, currentId)
                    }
                }

                // Bottom-left: ToolRail | Timeline | Meters
                SplitView {
                    SplitView.fillHeight: true
                    orientation: Qt.Horizontal
                    handle: Rectangle {
                        implicitWidth: 3
                        color: SplitHandle.pressed ? Theme.accent : Theme.border
                    }
                    ToolRail {
                        SplitView.preferredWidth: Theme.toolRailWidth
                        SplitView.minimumWidth: Theme.toolRailWidth
                        SplitView.maximumWidth: Theme.toolRailWidth
                        currentTool: editRoot.activeTimelineTool
                        onToolSelected: editRoot.activeTimelineTool = tool
                    }
                    TimelinePanel {
                        SplitView.fillWidth: true
                        activeTool: editRoot.activeTimelineTool
                        onToolRequested: tool => editRoot.activeTimelineTool = tool
                        onVocalRemovalProgressJobsChanged: (jobs, currentId) => editRoot.vocalRemovalProgressJobsChanged(jobs, currentId)
                    }
                    AudioMeters {
                        SplitView.preferredWidth: Theme.audioMetersWidth
                        SplitView.minimumWidth: Theme.audioMetersWidth
                        SplitView.maximumWidth: Theme.audioMetersWidth
                    }
                }
            }

            // Right: full-height Monitor (portrait preview)
            MonitorPanel {
                SplitView.preferredWidth: editRoot.width * 0.25
                SplitView.minimumWidth: editRoot.width * 0.18
            }
        }
    }
}
