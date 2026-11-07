pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import QtQuick.Controls
import "../theme"
import "../panels"
import "../components/common"

Item {
    id: editRoot

    signal requestImport()
    signal transcriptionProgressJobsChanged(var jobs, string currentId)
    signal vocalRemovalProgressJobsChanged(var jobs, string currentId)
    signal requestCloseAi()
    signal requestPools()

    // Which editing workspace layout to render
    // ("ESSENTIALS" | "VERTICAL" | "EFFECT EDITOR").
    property string layoutPreset: "ESSENTIALS"
    property int activeTimelineTool: 0
    property var ownerWindow: null
    property var assistant: null
    property bool assistantVisible: false
    property real assistantWidth: 420
    property real assistantDrawerWidth: 360
    property bool assistantFloating: false
    property bool assistantLoaded: false
    readonly property bool assistantDocked: width >= 1380
    readonly property real assistantPanelWidth: Math.min(width, assistantDocked
        ? Math.min(assistantWidth, Math.max(360, width - 960)) : assistantDrawerWidth)
    readonly property real workspaceWidth: width - (assistantVisible && assistantDocked && !assistantFloating ? assistantPanelWidth : 0)
    onAssistantVisibleChanged: if (assistantVisible) assistantLoaded = true

    function floatAssistant(position, offset, dragging) {
        if (assistantFloating || !assistantLoader.item) return
        assistantWindow.width = Math.max(360, assistantPanelWidth)
        assistantWindow.height = Math.max(480, Math.min(height, assistantWindow.Screen.desktopAvailableHeight - 48))
        assistantWindow.x = Math.round(position.x - offset.x)
        assistantWindow.y = Math.round(position.y - offset.y)
        assistantFloating = true
        assistantWindow.raise()
        assistantWindow.requestActivate()
        if (dragging) assistantWindow.startSystemMove()
    }

    Loader {
        id: editorLoader
        objectName: "residentEditorLoader"
        anchors.fill: parent
        anchors.rightMargin: editRoot.assistantVisible && editRoot.assistantDocked && !editRoot.assistantFloating ? editRoot.assistantPanelWidth : 0
        sourceComponent: {
            switch (editRoot.layoutPreset) {
            case "VERTICAL": return verticalComp
            case "EFFECT EDITOR": return effectEditorComp
            default: return essentialsComp
            }
        }
    }

    Loader {
        id: assistantLoader
        objectName: "residentAssistantLoader"
        // Reparent the same panel so its draft, tabs and model bindings survive.
        parent: editRoot.assistantFloating ? assistantWindow.contentItem : editRoot
        anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
        width: editRoot.assistantFloating ? parent.width : editRoot.assistantPanelWidth
        active: editRoot.assistantLoaded && editRoot.assistant !== null
        visible: editRoot.assistantVisible
        z: 20
        sourceComponent: Component {
            AiAssistantPanel {
                assistant: editRoot.assistant
                detachable: true
                floating: editRoot.assistantFloating
                onDetachRequested: (position, offset, dragging) => editRoot.floatAssistant(position, offset, dragging)
                onDockRequested: editRoot.assistantFloating = false
                onWindowMoveRequested: assistantWindow.startSystemMove()
                onCloseRequested: editRoot.requestCloseAi()
                onPoolsRequested: editRoot.requestPools()
                onImportRequested: editRoot.requestImport()
            }
        }
    }

    MouseArea {
        id: assistantResize
        objectName: "aiResizeHandle"
        x: assistantLoader.x - width / 2
        width: 12; height: editRoot.height
        z: 30
        visible: editRoot.assistantVisible && !editRoot.assistantFloating
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.SplitHCursor
        property real pressGlobalX: 0
        property real pressWidth: 0
        onPressed: mouse => {
            pressGlobalX = mapToGlobal(mouse.x, mouse.y).x
            pressWidth = editRoot.assistantPanelWidth
        }
        onPositionChanged: mouse => {
            if (!pressed) return
            const maximum = editRoot.assistantDocked ? Math.min(560, editRoot.width - 960) : Math.min(560, editRoot.width)
            const requested = pressWidth + pressGlobalX - mapToGlobal(mouse.x, mouse.y).x
            const value = Math.max(360, Math.min(maximum, requested))
            if (editRoot.assistantDocked) editRoot.assistantWidth = value
            else editRoot.assistantDrawerWidth = value
        }
        onDoubleClicked: {
            if (editRoot.assistantDocked) editRoot.assistantWidth = 420
            else editRoot.assistantDrawerWidth = 360
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.containsMouse || parent.pressed ? 4 : 3
            height: parent.height
            color: parent.containsMouse || parent.pressed ? Theme.accent : Theme.border
        }
        Rectangle {
            anchors.centerIn: parent
            width: 4; height: 28; radius: 2
            color: assistantResize.containsMouse || assistantResize.pressed ? Theme.accent : Theme.textMuted
        }
        ToolTip.visible: containsMouse && !pressed
        ToolTip.delay: 700
        ToolTip.text: qsTr("Drag to resize · double-click to reset")
    }

    Window {
        id: assistantWindow
        objectName: "aiFloatingWindow"
        transientParent: editRoot.ownerWindow || editRoot.Window.window
        flags: Qt.Tool | Qt.FramelessWindowHint
        modality: Qt.NonModal
        title: qsTr("AI Assistant — Cut Pro")
        color: "transparent"
        minimumWidth: 360
        minimumHeight: 480
        visible: editRoot.assistantFloating && editRoot.assistantVisible && editRoot.visible
        onClosing: close => {
            close.accepted = false
            editRoot.requestCloseAi()
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: Theme.outline
            border.width: 1
            radius: 3
            z: 25
        }
        WindowResizeHandles { anchors.fill: parent; ownerWindow: assistantWindow; z: 30 }
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
                    SplitView.preferredWidth: editRoot.workspaceWidth * 0.20
                    onRequestImport: editRoot.requestImport()
                }
                MonitorPanel { SplitView.fillWidth: true }
                TextPanel {
                    ownerWindow: editRoot.ownerWindow
                    SplitView.preferredWidth: editRoot.workspaceWidth * 0.25
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
                        SplitView.preferredWidth: editRoot.workspaceWidth * 0.16
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
                SplitView.preferredWidth: editRoot.workspaceWidth * 0.25
                SplitView.minimumWidth: editRoot.workspaceWidth * 0.18
            }
        }
    }

    // ---- EFFECT EDITOR: full-height Properties on the right, everything else
    //      clustered on the left (Project·Monitor / ToolRail·Timeline·Meters).
    Component {
        id: effectEditorComp

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

                // Top-left: Project | Monitor
                SplitView {
                    SplitView.preferredHeight: editRoot.height * 0.55
                    orientation: Qt.Horizontal
                    handle: Rectangle {
                        implicitWidth: 3
                        color: SplitHandle.pressed ? Theme.accent : Theme.border
                    }
                    ProjectPanel {
                        SplitView.preferredWidth: editRoot.workspaceWidth * 0.18
                        onRequestImport: editRoot.requestImport()
                    }
                    MonitorPanel { SplitView.fillWidth: true }
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

            // Right: full-height Properties (Effect Controls / Lumetri / Text / Text Effects)
            TextPanel {
                ownerWindow: editRoot.ownerWindow
                SplitView.preferredWidth: editRoot.workspaceWidth * 0.28
                SplitView.minimumWidth: editRoot.workspaceWidth * 0.20
                onProgressJobsChanged: (jobs, currentId) => editRoot.transcriptionProgressJobsChanged(jobs, currentId)
            }
        }
    }
}
