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
import "pool"
import "settings"
import "views"

ApplicationWindow {
    id: window
    property bool aiAssistantVisible: false
    visible: true
    width: 1280
    height: 720
    title: "Cut pro"
    flags: Qt.Window | Qt.FramelessWindowHint
    minimumWidth: 960
    minimumHeight: 540
    property int visibilityBeforeFullScreen: Window.Windowed

    color: Theme.bgPanel

    // Probe the machine for Python / edge-tts / ffmpeg (and the optional offline
    // engines) once at launch. Drives the navbar setup cloud icon below, which
    // shows only while a required dependency is missing.
    Component.onCompleted: {
        // No requirement probe on launch: the Requirements page shows the last
        // saved result (Backend restores it at startup) and the user re-probes
        // manually with Recheck. First-run onboarding still checks once below.
        // First launch on a fresh install: greet the client and offer to install
        // the optional engines before they hit a feature that needs one.
        if (!Backend.appSettings.onboardingComplete)
            welcomeDialog.begin()
        // If the GitHub check already resolved to an available update before the
        // UI finished loading, pop the native update window now (the Connections
        // below only fires on subsequent changes).
        if (Updater.state === "available" && !window.updateDialogAutoShown) {
            window.updateDialogAutoShown = true
            updateDialog.openNative()
        }
    }

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

    // The Google account pool ("GodMode Pool"): sign in accounts, rotate them
    // for the Gemini workers. Mechanics live in tools/pool_tool.py.
    PoolWindow {
        id: poolWindow
        ownerWindow: window
    }

    // App-wide failure popup. The backend emits operationFailed(title, reason)
    // when a transcription/translation (or similar) fails, so instead of only a
    // muted status line the user gets a modal with the real reason.
    AppErrorDialog {
        id: appErrorDialog
    }

    // First-run welcome / dependency check (see WelcomeDialog.qml). Opened once
    // from Component.onCompleted when onboardingComplete is still false.
    WelcomeDialog {
        id: welcomeDialog
    }
    Connections {
        target: Backend
        function onOperationFailed(title, reason) {
            appErrorDialog.show(title, reason)
        }
    }

    // Dependency status sheet, dropped out of the navbar cloud icon. Lists every
    // requirement the startup probe checked, with the feature each one unlocks,
    // and a Recheck button for after the user installs something.
    QuickExportPopup {
        id: quickExportPopup
        parent: Overlay.overlay
        readonly property point iconAnchor:
            quickExportBtn.parent.mapToItem(quickExportPopup.parent,
                                            quickExportBtn.x, quickExportBtn.y)
        x: Math.max(8, quickExportPopup.iconAnchor.x + quickExportBtn.width - width)
        y: quickExportPopup.iconAnchor.y + quickExportBtn.height + 6
    }

    Popup {
        id: requirementsPopup
        parent: Overlay.overlay
        readonly property point iconAnchor:
            setupBtn.parent.mapToItem(requirementsPopup.parent, setupBtn.x, setupBtn.y)
        x: Math.max(8, requirementsPopup.iconAnchor.x + setupBtn.width - width)
        y: requirementsPopup.iconAnchor.y + setupBtn.height + 6
        width: 340
        padding: 0
        modal: false
        dim: false
        background: Rectangle {
            color: Theme.bgPanel
            border.color: Theme.border
            radius: Theme.radiusMd
        }
        contentItem: ColumnLayout {
            spacing: 0
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 14
                spacing: 2
                Text {
                    text: qsTr("Setup")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsLg
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: Backend.requirementsSatisfied
                          ? qsTr("Everything required is installed. Optional engines below.")
                          : qsTr("Some features need extra components to work.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsXs
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Repeater {
                model: Backend.systemRequirements
                delegate: RowLayout {
                    id: reqRow
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    Layout.topMargin: 9
                    Layout.bottomMargin: 9
                    spacing: 10
                    readonly property bool ok: reqRow.modelData.status === "ok"
                    Image {
                        source: reqRow.ok ? "../assets/icons/check.svg"
                                          : "../assets/icons/alert-circle.svg"
                        sourceSize.width: 16
                        sourceSize.height: 16
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        RowLayout {
                            spacing: 6
                            Text {
                                text: reqRow.modelData.label
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsSm
                                font.weight: Font.DemiBold
                            }
                            Text {
                                visible: !reqRow.modelData.required
                                text: qsTr("optional")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fsXs
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: reqRow.modelData.feature
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fsXs
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: reqRow.modelData.detail
                            color: reqRow.ok ? "#67b26f"
                                             : (reqRow.modelData.required ? "#e0b070"
                                                                          : Theme.textMuted)
                            font.pixelSize: Theme.fsXs
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 12
                spacing: 8
                BusyIndicator {
                    running: Backend.requirementsChecking
                    visible: running
                    implicitWidth: 18
                    implicitHeight: 18
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: Backend.requirementsChecking ? qsTr("Checking...") : qsTr("Recheck")
                    enabled: !Backend.requirementsChecking
                    implicitHeight: 30
                    implicitWidth: 96
                    onClicked: Backend.checkSystemRequirements()
                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.down ? Theme.hover : Theme.bgPrimary
                        border.color: Theme.border
                        radius: Theme.radiusSm
                        opacity: parent.enabled ? 1.0 : 0.5
                    }
                }
            }
        }
    }

    // Native OS window offering the update, shown automatically the first time
    // the GitHub check reports a newer build (and reopenable from the top-bar
    // update icon). Replaces the old in-canvas dropdown. Bound entirely to the
    // Updater singleton's read-only state.
    UpdateAvailableDialog {
        id: updateDialog
        ownerWindow: window
    }

    // Pop the native window automatically the moment an update becomes
    // available, but only once per session - "Update later" closes it and we
    // must not immediately reopen it. The top-bar icon reopens it on demand.
    property bool updateDialogAutoShown: false
    Connections {
        target: Updater
        function onChanged() {
            if (Updater.state === "available" && !window.updateDialogAutoShown) {
                window.updateDialogAutoShown = true
                updateDialog.openNative()
            }
        }
    }

    // Premiere drops its progress sheet out of the toolbar button that opened
    // it instead of floating it in the middle of the window, so the panel and
    // its icon read as one control. Same here: right edge lined up under the
    // icon, top just under the menu bar.
    ProgressDashboard {
        id: progressDashboard
        parent: Overlay.overlay

        // progressBtn's own x and y are read here on purpose. mapToItem is a
        // plain function call and creates no binding, so without them this
        // would freeze at wherever the button sat when the window first opened
        // and the sheet would drift off its icon as the toolbar row reflows.
        readonly property point iconAnchor:
            progressBtn.parent.mapToItem(progressDashboard.parent,
                                         progressBtn.x, progressBtn.y)

        width: Math.min(640, Math.max(460, window.width - 24))
        // The sheet asks for the height its jobs actually need (it caps itself
        // and scrolls past that), so an idle or one-job dashboard is a short
        // card instead of a half-screen panel of empty trough.
        height: Math.min(progressDashboard.implicitHeight,
                         Math.max(240, window.height - y - 16))
        x: Math.max(8, Math.min(Math.round(iconAnchor.x + progressBtn.width - width),
                                window.width - width - 8))
        y: Math.round(iconAnchor.y + progressBtn.height) + 6
    }


    // The home screen's Settings row calls this through its ownerWindow, so the
    // one SettingsDialog instance is shared rather than duplicated per view.
    function openSettings() {
        settingsDialog.open()
    }

    // A quiet "Saved" toast in the bottom-right whenever auto-save writes.
    // Backend emits autoSaveCompleted(path) already; until now nothing showed
    // the user it had happened, so an auto-save was indistinguishable from no
    // save at all.
    Rectangle {
        id: autoSaveToast
        parent: Overlay.overlay
        z: 9999
        width: toastRow.implicitWidth + 28
        height: 34
        radius: 17
        color: Theme.bgCard
        border.color: Theme.outline
        opacity: 0
        visible: opacity > 0
        x: parent ? parent.width - width - 20 : 0
        y: parent ? parent.height - height - 20 : 0

        Row {
            id: toastRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: "#35c15a"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Project auto-saved")
                color: Theme.textPrimary
                font.pixelSize: Theme.fsMd
            }
        }

        Timer {
            id: toastTimer
            interval: 2200
            onTriggered: autoSaveToast.opacity = 0
        }
        function flash() {
            opacity = 1
            toastTimer.restart()
        }
    }

    Connections {
        target: Backend
        function onAutoSaveCompleted(path) {
            autoSaveToast.flash()
        }
    }

    // Full-window license gate. Covers everything (including the menu bar) until
    // the app is activated; the workspace Loader below stays empty meanwhile.
    ActivationOverlay {
        parent: Overlay.overlay
        anchors.topMargin: menuBar.height
        z: 10000
        visible: !Backend.licensed
    }

    function toggleMaximized() {
        if (visibility === Window.FullScreen)
            return
        if (visibility === Window.Maximized)
            showNormal()
        else
            showMaximized()
    }

    WindowResizeHandles {
        parent: Overlay.overlay
        anchors.fill: parent
        z: 10001
        ownerWindow: window
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
        // Comma-read so the binding re-evaluates when a rebind lands (see the
        // QML-binding memory: a bare Backend.shortcut() call would go stale).
        sequence: (Backend.appSettings, Backend.shortcut("app.fullscreen"))
        onActivated: window.toggleFullScreen()
    }

    Shortcut {
        sequence: (Backend.appSettings, Backend.shortcut("app.settings"))
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

    // Ctrl+Shift+D prints the whole diagnosis to the console and writes the same
    // text to a file. Deliberately no window and no overlay any more.
    //
    // Both of those existed, and both were measured making the app they were
    // measuring slower: the overlay polled three C++ reports on a 700 ms timer
    // into ~40 live Text items, and the report window's wrapping text blocks cost
    // a 400 ms GUI-thread stall on every resize. An instrument that changes the
    // reading is worse than no instrument. The console form carries everything
    // they showed - the same DiagnosticAnalyzer verdict, findings and raw
    // sections - and costs one keypress, nothing per frame.
    //
    // `cutpro --diagnose` prints the file back afterwards, so evidence outlives
    // the terminal that was scrolling.
    Shortcut {
        sequence: "Ctrl+Shift+D"
        onActivated: Diagnostics.printReport("manual, Ctrl+Shift+D")
    }

    FileDialog {
        id: mediaDialog
        title: qsTr("Import media")
        fileMode: FileDialog.OpenFiles
        // Kept in step with MediaScan's suffix tables: a file the dialog hides is
        // a file the user believes the app cannot open, even though the probe
        // would have accepted it.
        readonly property string videoFilter: "*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mts *.m2ts *.mpg *.mpeg *.m2v *.wmv *.asf *.flv *.f4v *.3gp *.3g2 *.ts *.vob *.mxf *.ogv *.dv *.divx *.mjpeg *.y4m *.mod *.qt"
        readonly property string audioFilter: "*.mp3 *.wav *.aac *.m4a *.flac *.ogg *.opus *.wma *.aiff *.aif *.m4b *.mka *.oga *.amr *.ac3 *.eac3 *.dts *.mp2 *.caf *.au *.w64 *.wv *.ape *.tta *.ra"
        readonly property string imageFilter: "*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff *.jfif *.avif *.heic *.heif *.tga *.ppm *.dds *.exr *.hdr *.pcx *.jp2 *.psd *.qoi"
        nameFilters: ["Media files (" + videoFilter + " " + audioFilter + " " + imageFilter + ")",
                      "Video (" + videoFilter + ")",
                      "Audio (" + audioFilter + ")",
                      "Images (" + imageFilter + ")",
                      "All files (*)"]
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

        // ---- 1. Custom title bar + workspace toolbar ----------------------
        Rectangle {
            id: menuBar
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.menuBarHeight
            color: Theme.bgSidebar

            WindowDragArea {
                anchors.fill: parent
                ownerWindow: window
                enabled: !Backend.licensed
                onToggleMaximized: window.toggleMaximized()
            }

            Text {
                visible: !Backend.licensed
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: window.title
                color: Theme.textSecondary
                font.pixelSize: Theme.fsLg
            }

            WindowControls {
                id: windowControls
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                ownerWindow: window
                visible: window.visibility !== Window.FullScreen
                onToggleMaximized: window.toggleMaximized()
            }

            RowLayout {
                anchors.left: parent.left
                anchors.right: windowControls.visible ? windowControls.left : parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                visible: Backend.licensed

                // Left: home + workspace tabs
                IconButton {
                    iconName: "home"
                    glyphSize: 16
                    restColor: Theme.textSecondary
                    active: Backend.activeWorkspace === "Home"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Home")
                    onClicked: Backend.activeWorkspace = "Home"
                }

                Repeater {
                    // Import is not shipped yet, so it is left out of the tab row
                    // entirely rather than shown greyed - a dead tab reads as a
                    // broken app.
                    model: ["Edit", "Export"]
                    delegate: Item {
                        id: ws
                        required property int index
                        required property string modelData
                        readonly property bool active: Backend.activeWorkspace === modelData
                        readonly property bool available: true
                        // The editor workspaces are meaningless on the home
                        // screen (there is no open sequence to switch views of),
                        // so the tab row collapses there and the home button
                        // stands alone.
                        visible: Backend.activeWorkspace !== "Home"
                        Layout.fillHeight: true
                        // Sized to the bold (active) width at all times via the
                        // hidden sizer below, so toggling a tab's font.weight on
                        // switch never resizes it or shifts the row — no pop.
                        implicitWidth: visible ? wsSizer.implicitWidth + 16 : 0

                        Text {
                            id: wsSizer
                            visible: false
                            text: ws.modelData
                            font.pixelSize: Theme.fsLg
                            font.weight: Font.DemiBold
                        }
                        Text {
                            id: wsLabel
                            anchors.centerIn: parent
                            text: ws.modelData
                            font.pixelSize: Theme.fsLg
                            font.weight: ws.active ? Font.DemiBold : Font.Normal
                            color: !ws.available ? Theme.textMuted
                                   : ws.active ? Theme.textPrimary : Theme.textSecondary
                            opacity: ws.available ? 1.0 : 0.55
                        }
                        Rectangle {
                            anchors.top: wsLabel.bottom
                            anchors.topMargin: 5
                            anchors.horizontalCenter: wsLabel.horizontalCenter
                            width: wsLabel.implicitWidth
                            height: 2
                            color: Theme.accent
                            visible: ws.active && ws.available
                        }
                        TapHandler {
                            enabled: ws.available
                            cursorShape: Qt.PointingHandCursor
                            onTapped: Backend.activeWorkspace = ws.modelData
                        }
                    }
                }

                WindowDragArea {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ownerWindow: window
                    onToggleMaximized: window.toggleMaximized()
                }

                // Center: project name (hidden on the home screen, where no one
                // project is "open" in the editor sense).
                Text {
                    visible: Backend.activeWorkspace !== "Home"
                    text: Backend.projectName
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsLg
                    elide: Text.ElideRight
                    Layout.maximumWidth: Math.max(120, window.width * 0.25)

                    WindowDragArea {
                        anchors.fill: parent
                        ownerWindow: window
                        onToggleMaximized: window.toggleMaximized()
                    }
                }

                WindowDragArea {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ownerWindow: window
                    onToggleMaximized: window.toggleMaximized()
                }

                Button {
                    objectName: "aiAssistantToggle"
                    visible: Backend.activeWorkspace !== "Home"
                    text: qsTr("AI")
                    icon.source: "qrc:/assets/icons/sparkles.svg"
                    icon.width: 15; icon.height: 15
                    icon.color: window.aiAssistantVisible ? "white" : Theme.accent
                    implicitHeight: 28
                    palette.buttonText: window.aiAssistantVisible ? "white" : Theme.textPrimary
                    background: Rectangle {
                        color: window.aiAssistantVisible ? Theme.accent : "transparent"
                        radius: Theme.radiusSm
                    }
                    Accessible.name: qsTr("Toggle AI assistant")
                    onClicked: {
                        if (Backend.activeWorkspace !== "Edit") Backend.activeWorkspace = "Edit"
                        window.aiAssistantVisible = !window.aiAssistantVisible
                    }
                }

                // Right: workspace preset + tool icons. These act on an open
                // sequence (layout preset, share, export progress, workspaces,
                // search), so they are meaningless on the home screen and are
                // hidden there — the home bar keeps only full screen + settings.
                // Workspace switcher icon. Opens the same layout-preset menu as
                // the label beside it, giving the toolbar a recognisable
                // "workspace" affordance (the panel-layout glyph) rather than a
                // bare word.
                IconButton {
                    id: workspaceBtn
                    visible: Backend.activeWorkspace !== "Home"
                    iconName: "layout-panel"
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    active: presetPopup.opened
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Workspace: %1").arg(Backend.layoutPreset)
                    onClicked: presetPopup.opened ? presetPopup.close() : presetPopup.open()
                }

                Button {
                    id: presetBtn
                    visible: Backend.activeWorkspace !== "Home"
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
                                model: ["ESSENTIALS", "VERTICAL", "EFFECT EDITOR"]
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

                IconButton {
                    id: progressBtn
                    visible: Backend.activeWorkspace !== "Home"
                    iconName: "list-video"
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    // Stays lit for as long as the sheet it dropped is open,
                    // which is what ties the panel to this icon.
                    active: progressDashboard.visible || progressDashboard.hasActiveJobs
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open Progress Dashboard")
                    onClicked: progressDashboard.open()
                }

                // Quick Export. Only while there is a sequence to export; on the
                // Home screen it would have nothing to act on.
                IconButton {
                    id: quickExportBtn
                    visible: Backend.activeWorkspace !== "Home"
                    iconName: "share-2"
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    active: quickExportPopup.opened || Backend.exportInProgress
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: Backend.exportInProgress
                                  ? qsTr("Exporting - %1%").arg(Math.round(Backend.exportProgress * 100))
                                  : qsTr("Quick Export")
                    onClicked: quickExportPopup.opened ? quickExportPopup.close()
                                                       : quickExportPopup.open()
                }

                IconButton {
                    iconName: "maximize-2"
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    active: window.visibility === Window.FullScreen
                    ToolTip.visible: hovered
                    ToolTip.text: window.visibility === Window.FullScreen
                                  ? qsTr("Exit full screen (Esc)")
                                  : qsTr("Full screen (F11)")
                    onClicked: window.toggleFullScreen()
                }
                // Setup indicator. Shown only while a required dependency is
                // missing; it disappears once everything needed is in place.
                IconButton {
                    id: setupBtn
                    visible: !Backend.requirementsSatisfied
                    iconName: "cloud-download"
                    glyphSize: 15
                    restColor: "#e0b070"
                    active: requirementsPopup.opened
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Setup required - some features need extra components")
                    onClicked: requirementsPopup.opened ? requirementsPopup.close()
                                                        : requirementsPopup.open()
                }
                // Update indicator. Shown only when the GitHub check found a
                // newer published build; dropping its popup offers Download &
                // Install (or the release page as a fallback).
                IconButton {
                    id: updateBtn
                    visible: Updater.available
                    iconName: "arrow-up-circle"
                    glyphSize: 15
                    restColor: Theme.accent
                    active: updateDialog.visible
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Update available - version %1").arg(Updater.latestVersion)
                    onClicked: updateDialog.visible ? updateDialog.close()
                                                    : updateDialog.openNative()
                }
                IconButton {
                    iconName: "users"
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Account pool (Gemini without API keys)")
                    onClicked: poolWindow.openNative()
                }
                IconButton {
                    iconName: "settings"
                    glyphSize: 15
                    restColor: Theme.textSecondary
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Settings")
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
        // ---- 2. Workspace view switcher ---------------------------------
        // The Home / Import / Edit / Export tabs are routed pages (mirrors
        // App.tsx). Each page loads lazily on first visit and is then KEPT
        // ALIVE — switching only toggles visibility. Previously one shared
        // Loader swapped sourceComponent, which destroyed the current page and
        // rebuilt the next from scratch: every Edit↔Export switch tore down and
        // re-instantiated the whole EditView tree (monitor, timeline, every
        // clip waveform/filmstrip). That rebuild was the switch lag. Resident
        // pages make repeat switches instant, at the cost of holding each
        // visited page in memory.
        Item {
            id: workspaceSwitcher
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Empty while unlicensed → no page instantiates behind the gate.
            readonly property string current: Backend.licensed ? Backend.activeWorkspace : ""

            Loader {
                anchors.fill: parent
                readonly property bool selected: workspaceSwitcher.current === "Home"
                active: false; visible: selected
                sourceComponent: homeComp
                onSelectedChanged: if (selected) active = true
                Component.onCompleted: if (selected) active = true
            }
            Loader {
                anchors.fill: parent
                readonly property bool selected: workspaceSwitcher.current === "Import"
                active: false; visible: selected
                sourceComponent: importComp
                onSelectedChanged: if (selected) active = true
                Component.onCompleted: if (selected) active = true
            }
            Loader {
                anchors.fill: parent
                readonly property bool selected: workspaceSwitcher.current === "Edit"
                active: false; visible: selected
                sourceComponent: editComp
                onSelectedChanged: if (selected) active = true
                Component.onCompleted: if (selected) active = true
            }
            Loader {
                anchors.fill: parent
                readonly property bool selected: workspaceSwitcher.current === "Export"
                active: false; visible: selected
                sourceComponent: exportComp
                onSelectedChanged: if (selected) active = true
                Component.onCompleted: if (selected) active = true
            }
        }

        Component {
            id: homeComp
            HomeView { ownerWindow: window }
        }

        Component {
            id: editComp
            EditView {
                ownerWindow: window
                layoutPreset: Backend.layoutPreset
                assistant: Ai
                assistantVisible: window.aiAssistantVisible
                onRequestCloseAi: window.aiAssistantVisible = false
                onRequestPools: poolWindow.openNative()
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
