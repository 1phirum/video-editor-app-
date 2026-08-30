// qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CutPro 1.0
import "theme"
import "components/common"
import "components/effects"
import "components/export"
import "components/lumetri"
import "components/project"
import "components/subtitles"
import "components/timeline"
import "panels"
import "settings"
import "views"

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 720
    title: "Cut pro"
    property int visibilityBeforeFullScreen: Window.Windowed

    color: Theme.bgPanel

    // App-wide default pointer. Sits under the whole UI, so any Item that sets
    // its own AppCursor.name (or a cursorShape) still wins; this only decides
    // what the arrow looks like everywhere else.
    Item {
        anchors.fill: parent
        z: -1000
        AppCursor.name: "Select"
    }

    SettingsDialog {
        id: settingsDialog
        ownerWindow: window
    }

    ProgressDashboard {
        id: progressDashboard
        parent: Overlay.overlay
        width: Math.min(920, Math.max(560, window.width - 72))
        height: Math.min(620, Math.max(420, window.height - 72))
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
    }


    // Parented to the overlay layer rather than to the window's ColumnLayout so
    // that it draws over every panel without taking part in the layout, and so a
    // panel that is mid-rebuild cannot push it around. Top-right because the
    // freeze it exists to explain is usually reported from the timeline, which is
    // along the bottom.
    DiagnosticsOverlay {
        id: diagnosticsOverlay
        parent: Overlay.overlay
        width: Math.min(460, window.width - 24)
        height: Math.min(520, window.height - 24)
        x: window.width - width - 12
        y: 12
    }

    function toggleFullScreen() {
        // Full-screen playback owns the window state while it is on, so F11 gives
        // that up first instead of leaving a black picture layer over a windowed
        // UI. Clearing the flag is what restores the window.
        if (Backend.videoFullScreen) {
            Backend.videoFullScreen = false
            return
        }
        if (visibility === Window.FullScreen) {
            if (visibilityBeforeFullScreen === Window.Maximized)
                showMaximized()
            else
                showNormal()
            return
        }

        visibilityBeforeFullScreen = visibility
        showFullScreen()
    }

    // Set only when full-screen playback is what put the window in that state, so
    // leaving playback does not also undo an F11 the user asked for separately.
    property bool fullScreenForVideo: false

    // Windows can leave full screen without going through this window - the taskbar
    // and Win+Down both do it. The picture layer has to follow, or it covers a
    // windowed UI with nothing to close it but a key the user cannot see.
    //
    // Qualified with the window id on purpose: the bare name would resolve to the
    // signal's injected parameter, which Qt deprecates and drops in the next major
    // version, and then this handler would stop reading the state it acts on.
    onVisibilityChanged: {
        if (window.visibility !== Window.FullScreen && Backend.videoFullScreen)
            Backend.videoFullScreen = false
    }

    // The monitor re-hosts its own picture; the window is what has to grow around
    // it. Driven off the flag rather than from the button so the two cannot
    // disagree - whatever sets it, the window follows.
    Connections {
        target: Backend
        function onVideoFullScreenChanged() {
            if (Backend.videoFullScreen) {
                if (window.visibility !== Window.FullScreen) {
                    window.visibilityBeforeFullScreen = window.visibility
                    window.fullScreenForVideo = true
                    window.showFullScreen()
                }
            } else if (window.fullScreenForVideo) {
                window.fullScreenForVideo = false
                if (window.visibility === Window.FullScreen) {
                    if (window.visibilityBeforeFullScreen === Window.Maximized)
                        window.showMaximized()
                    else
                        window.showNormal()
                }
            }
        }
    }

    Shortcut {
        sequence: "F11"
        onActivated: window.toggleFullScreen()
    }

    Shortcut {
        sequence: "Ctrl+,"
        onActivated: settingsDialog.open()
    }

    // One Escape for the window: a second Shortcut on the same key is an
    // ambiguous overload and then neither of them fires. Full-screen playback
    // takes it first, because that is the state the key was asked to leave.
    Shortcut {
        sequence: "Escape"
        enabled: Backend.videoFullScreen
                 || window.visibility === Window.FullScreen
        onActivated: {
            if (Backend.videoFullScreen)
                Backend.videoFullScreen = false
            else
                window.toggleFullScreen()
        }
    }

    // The two keys that make the instrumentation reachable. Everything the
    // watchdog, the item census and the model guard measure was already being
    // recorded and none of it could be read without a debugger attached, which is
    // how a freeze stayed unexplained across a dozen attempts at fixing it.
    //
    // F12 rather than a menu item on purpose: it has to work while the window is
    // not finishing frames, and a menu needs a paint to open.
    Shortcut {
        sequence: "F12"
        onActivated: diagnosticsOverlay.active = !diagnosticsOverlay.active
    }

    // Writes the same numbers to a file next to the crash reports, for the case
    // where the window is too wedged to read them off the screen. Shows the
    // overlay first so the path it wrote to is visible.
    Shortcut {
        sequence: "Ctrl+Shift+D"
        onActivated: {
            diagnosticsOverlay.active = true
            diagnosticsOverlay.writeSnapshot()
        }
    }

    FileDialog {
        id: mediaDialog
        title: "Import media"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Media files (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mp3 *.wav *.aac *.m4a *.png *.jpg *.jpeg *.webp)", "All files (*)"]
        onAccepted: {
            Backend.importMediaAsync(selectedFiles.map(function(url) { return url.toString() }))
            Backend.activeWorkspace = "Edit"
        }
    }

    palette.window: Theme.bgPanel
    palette.windowText: Theme.textPrimary
    palette.base: Theme.bgPrimary
    palette.text: Theme.textPrimary
    palette.button: Theme.bgSidebar
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.accent
    palette.highlightedText: "#ffffff"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 1. Menu bar --------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.menuBarHeight
            color: Theme.bgSidebar

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                // Left: home + workspace tabs
                IconButton { iconName: "home"; glyphSize: 16; restColor: Theme.textSecondary }

                Repeater {
                    model: ["Import", "Edit", "Export"]
                    delegate: Item {
                        id: ws
                        required property int index
                        required property string modelData
                        readonly property bool active: Backend.activeWorkspace === modelData
                        Layout.fillHeight: true
                        implicitWidth: wsLabel.implicitWidth + 16

                        Text {
                            id: wsLabel
                            anchors.centerIn: parent
                            text: ws.modelData
                            font.pixelSize: Theme.fsLg
                            font.weight: ws.active ? Font.DemiBold : Font.Normal
                            color: ws.active ? Theme.textPrimary : Theme.textSecondary
                        }
                        Rectangle {
                            anchors.top: wsLabel.bottom
                            anchors.topMargin: 5
                            anchors.horizontalCenter: wsLabel.horizontalCenter
                            width: wsLabel.implicitWidth
                            height: 2
                            color: Theme.accent
                            visible: ws.active
                        }
                        TapHandler { cursorShape: Qt.PointingHandCursor; onTapped: Backend.activeWorkspace = ws.modelData }
                    }
                }

                Item { Layout.fillWidth: true }

                // Center: project name
                Text {
                    text: Backend.projectName
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsLg
                }

                Item { Layout.fillWidth: true }

                // Right: workspace preset + tool icons
                Button {
                    id: presetBtn
                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    implicitHeight: 26
                    implicitWidth: presetLabel.implicitWidth + 16
                    flat: true
                    hoverEnabled: false
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: "transparent"
                    }
                    contentItem: Text {
                        id: presetLabel
                        text: Backend.layoutPreset
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fsSm
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.5
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: presetPopup.opened ? presetPopup.close() : presetPopup.open()

                    Popup {
                        id: presetPopup
                        y: presetBtn.height + 4
                        x: presetBtn.width - width
                        width: 140
                        padding: 4
                        background: Rectangle {
                            color: Theme.bgPanel
                            border.color: Theme.border
                            radius: Theme.radiusSm
                        }
                        contentItem: ColumnLayout {
                            spacing: 0
                            Repeater {
                                model: ["ESSENTIALS", "VERTICAL"]
                                delegate: Rectangle {
                                    id: opt
                                    required property int index
                                    required property string modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 28
                                    radius: Theme.radiusSm
                                    color: "transparent"
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 12
                                        text: opt.modelData
                                        font.pixelSize: Theme.fsMd
                                        color: Backend.layoutPreset === opt.modelData ? Theme.accent : Theme.textPrimary
                                    }
                                    TapHandler {
                                        cursorShape: Qt.PointingHandCursor
                                        onTapped: {
                                            Backend.layoutPreset = opt.modelData
                                            presetPopup.close()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                IconButton { iconName: "share-2"; glyphSize: 15; restColor: Theme.textSecondary }

                IconButton {
                    iconName: "list-video"
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    active: progressDashboard.hasActiveJobs
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: "Open Progress Dashboard"
                    onClicked: progressDashboard.open()
                }

                // Workspaces 2x2 grid (drawn — no single lucide asset for it)
                Button {
                    id: gridBtn
                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    implicitWidth: 28
                    implicitHeight: 28
                    flat: true
                    hoverEnabled: false
                    background: Rectangle {
                        radius: Theme.radiusMd
                        color: "transparent"
                    }
                    contentItem: Item {
                        Grid {
                            anchors.centerIn: parent
                            columns: 2
                            rowSpacing: 2
                            columnSpacing: 2
                            Repeater {
                                model: 4
                                delegate: Rectangle {
                                    width: 6; height: 6; radius: 1
                                    color: Theme.textSecondary
                                }
                            }
                        }
                    }
                    onClicked: presetPopup.opened ? presetPopup.close() : presetPopup.open()
                }

                IconButton { iconName: "search"; glyphSize: 15; restColor: Theme.textSecondary }
                IconButton {
                    iconName: "maximize-2"
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    active: window.visibility === Window.FullScreen
                    ToolTip.visible: hovered
                    ToolTip.text: window.visibility === Window.FullScreen
                                  ? "Exit full screen (Esc)"
                                  : "Full screen (F11)"
                    onClicked: window.toggleFullScreen()
                }
                IconButton {
                    iconName: "settings"
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    ToolTip.visible: hovered
                    ToolTip.text: "Settings"
                    onClicked: settingsDialog.open()
                }
            }

            // Bottom seam
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        // ---- 2. Workspace view switcher ---------------------------------
        // The Import / Edit / Export tabs are routed pages (mirrors App.tsx).
        Loader {
            id: workspaceLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: Backend.activeWorkspace === "Import" ? importComp
                           : Backend.activeWorkspace === "Export" ? exportComp
                           : editComp
        }

        Component {
            id: editComp
            EditView {
                ownerWindow: window
                layoutPreset: Backend.layoutPreset
                onRequestImport: mediaDialog.open()
                onTranscriptionProgressJobsChanged: (jobs, currentId) => {
                    progressDashboard.transcriptionJobs = jobs
                    progressDashboard.transcriptionCurrentId = currentId
                }
                onVocalRemovalProgressJobsChanged: (jobs, currentId) => {
                    progressDashboard.vocalRemovalJobs = jobs
                    progressDashboard.vocalRemovalCurrentId = currentId
                }
            }
        }

        Component {
            id: importComp
            ImportView {
                onRequestCreate: Backend.activeWorkspace = "Edit"
            }
        }

        Component {
            id: exportComp
            ExportView {}
        }
    }
}
