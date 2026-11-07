pragma ComponentBehavior: Bound
// qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CutPro 1.0
import "../theme"
import "../components/common"

// The workspace / home screen. Shown first on launch (startupWorkspace
// defaults to "Home"): a left rail for identity + navigation + the workspace
// folder, and a main column with a "Create project" banner over a grid of the
// projects living in the workspace folder. Its own layout language - it is not
// a copy of any one editor's start page - but it fills the same role: the place
// a session begins and where finished work is picked back up.
Rectangle {
    id: root
    color: Theme.bgPrimary

    property var ownerWindow: null

    // The project list is a directory scan (Backend.listWorkspaceProjects),
    // so it is pulled fresh whenever the view is shown, a project is
    // created/renamed/deleted, or the workspace folder changes. Held in a
    // property so the search field can filter it without re-scanning on every
    // keystroke.
    property var allProjects: []
    property string filter: ""

    // Which left-rail section is active. "home" shows the welcome banner +
    // project grid; "projects" focuses on just the project grid.
    property string homeSection: "home"

    function refresh() {
        allProjects = Backend.listWorkspaceProjects()
    }

    readonly property var visibleProjects: {
        if (filter.trim().length === 0)
            return allProjects
        var needle = filter.toLowerCase()
        return allProjects.filter(function (p) {
            return p.name.toLowerCase().indexOf(needle) !== -1
        })
    }

    Component.onCompleted: refresh()

    Connections {
        target: Backend
        function onWorkspaceLocationChanged() { root.refresh() }
        function onProjectChanged() { root.refresh() }
    }

    // ---- Create-project name prompt --------------------------------------
    // A small centered overlay rather than a native window: the home screen is
    // full-window already, so a floating card reads as part of it.
    property bool namingProject: false
    property string renameTargetPath: ""   // empty => creating, else renaming

    function beginCreate() {
        renameTargetPath = ""
        nameField.text = ""
        namingProject = true
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function beginRename(path, currentName) {
        renameTargetPath = path
        nameField.text = currentName
        namingProject = true
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function commitName() {
        var name = nameField.text.trim()
        if (name.length === 0)
            return
        // Close the overlay first. On create, the backend switches to the Edit
        // workspace synchronously, which tears this whole view down - so nothing
        // after the call may touch HomeView's own properties.
        namingProject = false
        if (renameTargetPath.length > 0) {
            Backend.renameWorkspaceProject(renameTargetPath, name)
            refresh()
        } else {
            var path = Backend.createWorkspaceProject(name)
            // On success the view is already on Edit; on failure we stay on Home
            // and Backend.lastError drives the error banner below.
            if (path.length === 0)
                refresh()
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose workspace folder")
        onAccepted: {
            Backend.chooseWorkspaceLocation(selectedFolder.toString())
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ============================ SIDEBAR ============================
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 236
            color: Theme.bgSidebar

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 20
                anchors.bottomMargin: 16
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 4

                // Primary nav
                HomeNavItem {
                    Layout.fillWidth: true
                    iconName: "home"
                    label: qsTr("Home")
                    selected: root.homeSection === "home"
                    onActivated: root.homeSection = "home"
                }
                HomeNavItem {
                    Layout.fillWidth: true
                    iconName: "film"
                    label: qsTr("My projects")
                    badge: root.allProjects.length
                    selected: root.homeSection === "projects"
                    onActivated: {
                        root.homeSection = "projects"
                        projectsGrid.forceActiveFocus()
                    }
                }

                // Management group
                Text {
                    text: qsTr("MANAGEMENT")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsXs
                    font.letterSpacing: 1.2
                    Layout.topMargin: 18
                    Layout.bottomMargin: 4
                    Layout.leftMargin: 4
                }

                HomeNavItem {
                    Layout.fillWidth: true
                    iconName: "folder-open"
                    label: qsTr("Workspace folder")
                    onActivated: folderDialog.open()
                }

                Item { Layout.fillHeight: true }

                // Workspace path readout
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: pathCol.implicitHeight + 20
                    radius: Theme.radiusMd
                    color: Theme.bgPrimary

                    ColumnLayout {
                        id: pathCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 2
                        Text {
                            text: qsTr("SAVING TO")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsXs
                            font.letterSpacing: 1.0
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Backend.workspaceLocation
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fsSm
                            elide: Text.ElideMiddle
                        }
                    }
                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: folderDialog.open()
                    }
                }

                HomeNavItem {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    iconName: "settings"
                    label: qsTr("Settings")
                    onActivated: {
                        if (root.ownerWindow && root.ownerWindow.openSettings)
                            root.ownerWindow.openSettings()
                    }
                }
            }

            // Right seam
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
        }

        // ============================ MAIN ==============================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Header row: title + search
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 28
                Layout.bottomMargin: 8
                spacing: 16

                ColumnLayout {
                    spacing: 2
                    Text {
                        text: root.homeSection === "projects" ? "My projects" : "Welcome back"
                        color: Theme.textPrimary
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: root.homeSection === "projects"
                              ? "Everything saved to your workspace."
                              : "Start something new or jump back into a project."
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsLg
                    }
                }

                Item { Layout.fillWidth: true }

                SearchField {
                    Layout.preferredWidth: 240
                    placeholder: "Search projects"
                    onTextChanged: root.filter = text
                }
            }

            // Create-project banner
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                Layout.preferredHeight: 132
                visible: root.homeSection === "home"
                radius: 12
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.darker(Theme.accent, 1.15) }
                    GradientStop { position: 1.0; color: Qt.darker(Theme.accent, 2.1) }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 28
                    anchors.rightMargin: 28
                    spacing: 20

                    ColumnLayout {
                        spacing: 6
                        Text {
                            text: qsTr("New project")
                            color: "#ffffff"
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: qsTr("A blank timeline, saved to your workspace and auto-saved as you go.")
                            color: Qt.rgba(1, 1, 1, 0.82)
                            font.pixelSize: Theme.fsLg
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // Create button
                    Rectangle {
                        Layout.preferredWidth: createRow.implicitWidth + 36
                        Layout.preferredHeight: 44
                        radius: 22
                        color: createArea.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.92)

                        RowLayout {
                            id: createRow
                            anchors.centerIn: parent
                            spacing: 8
                            Text {
                                text: qsTr("Create project")
                                color: "#1a1a1a"
                                font.pixelSize: Theme.fsLg
                                font.weight: Font.DemiBold
                            }
                        }
                        HoverHandler {
                            id: createArea
                            property bool containsMouse: hovered
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler { onTapped: root.beginCreate() }
                    }
                }
            }

            // "Recent" strip label
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                Layout.topMargin: 22
                Layout.bottomMargin: 10
                Text {
                    text: qsTr("Projects")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsXl
                    font.weight: Font.DemiBold
                }
                Text {
                    text: root.visibleProjects.length + (root.visibleProjects.length === 1 ? " project" : " projects")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsMd
                    Layout.leftMargin: 4
                    Layout.alignment: Qt.AlignBottom
                    bottomPadding: 1
                }
                Item { Layout.fillWidth: true }
            }

            // Project grid (or empty state)
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Empty state
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: root.allProjects.length === 0
                    spacing: 12
                    Image {
                        Layout.alignment: Qt.AlignHCenter
                        source: "../assets/icons/film.svg"
                        sourceSize.width: 40
                        sourceSize.height: 40
                        opacity: 0.3
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("No projects yet")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fsXl
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Create your first project to get started.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsMd
                    }
                }

                // No-search-match state
                Text {
                    anchors.centerIn: parent
                    visible: root.allProjects.length > 0 && root.visibleProjects.length === 0
                    text: qsTr("No projects match \"%1\"").arg(root.filter)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsLg
                }

                GridView {
                    id: projectsGrid
                    anchors.fill: parent
                    anchors.leftMargin: 28
                    anchors.rightMargin: 20
                    anchors.bottomMargin: 20
                    visible: root.visibleProjects.length > 0
                    clip: true
                    cellWidth: 232
                    cellHeight: 196
                    model: root.visibleProjects
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: ProjectCard {
                        required property var modelData
                        width: projectsGrid.cellWidth - 16
                        height: projectsGrid.cellHeight - 16
                        projectName: modelData.name
                        modifiedLabel: modelData.modifiedLabel
                        thumbnailUrl: modelData.thumbnailUrl || ""
                        mediaKind: modelData.mediaKind || ""
                        onOpen: Backend.openWorkspaceProject(modelData.path)
                        onRename: root.beginRename(modelData.path, modelData.name)
                        onRemove: confirmDelete.ask(modelData.path, modelData.name)
                    }
                }
            }
        }
    }

    // ---- Name prompt overlay --------------------------------------------
    Rectangle {
        anchors.fill: parent
        visible: root.namingProject
        color: Qt.rgba(0, 0, 0, 0.55)

        // Swallow clicks behind the card, and dismiss on backdrop click.
        TapHandler { onTapped: root.namingProject = false }

        Rectangle {
            anchors.centerIn: parent
            width: 380
            implicitHeight: promptCol.implicitHeight + 40
            height: implicitHeight
            radius: 12
            color: Theme.bgCard
            border.color: Theme.outline

            // Keep clicks on the card from reaching the backdrop dismisser.
            TapHandler {}

            ColumnLayout {
                id: promptCol
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                Text {
                    text: root.renameTargetPath.length > 0 ? "Rename project" : "Name your project"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsXl
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    radius: Theme.radiusSm
                    color: Theme.bgPrimary
                    border.color: nameField.activeFocus ? Theme.accent : Theme.outline

                    TextField {
                        id: nameField
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        background: null
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsLg
                        placeholderText: qsTr("Untitled Project")
                        placeholderTextColor: Theme.textMuted
                        selectByMouse: true
                        onAccepted: root.commitName()
                        Keys.onEscapePressed: root.namingProject = false
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Item { Layout.fillWidth: true }

                    HomeButton {
                        text: qsTr("Cancel")
                        primary: false
                        onClicked: root.namingProject = false
                    }
                    HomeButton {
                        text: root.renameTargetPath.length > 0 ? "Rename" : "Create"
                        primary: true
                        enabled: nameField.text.trim().length > 0
                        onClicked: root.commitName()
                    }
                }
            }
        }
    }

    // ---- Delete confirm overlay -----------------------------------------
    Rectangle {
        id: confirmDelete
        anchors.fill: parent
        visible: false
        color: Qt.rgba(0, 0, 0, 0.55)

        property string pendingPath: ""
        property string pendingName: ""

        function ask(path, name) {
            pendingPath = path
            pendingName = name
            visible = true
        }

        TapHandler { onTapped: confirmDelete.visible = false }

        Rectangle {
            anchors.centerIn: parent
            width: 380
            height: delCol.implicitHeight + 40
            radius: 12
            color: Theme.bgCard
            border.color: Theme.outline
            TapHandler {}

            ColumnLayout {
                id: delCol
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                Text {
                    text: qsTr("Delete project?")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsXl
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("\"%1\" and its saved data will be permanently deleted. This can't be undone.").arg(confirmDelete.pendingName)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsMd
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 10
                    Item { Layout.fillWidth: true }
                    HomeButton {
                        text: qsTr("Cancel")
                        primary: false
                        onClicked: confirmDelete.visible = false
                    }
                    HomeButton {
                        text: qsTr("Delete")
                        primary: true
                        danger: true
                        onClicked: {
                            Backend.deleteWorkspaceProject(confirmDelete.pendingPath)
                            confirmDelete.visible = false
                            root.refresh()
                        }
                    }
                }
            }
        }
    }

    // ---- Error banner ----------------------------------------------------
    // Backend records failures in lastError (e.g. no writable workspace folder,
    // a name clash on rename). Without this a failed action just looked like a
    // no-op. Shows the latest error until dismissed or the next success clears
    // the field.
    Rectangle {
        id: errorBanner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        height: 44
        radius: Theme.radiusMd
        color: Qt.darker(Theme.danger, 2.4)
        border.color: Theme.danger
        visible: Backend.lastError.length > 0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 8
            spacing: 10
            Image {
                source: "../assets/icons/volume-x.svg"
                sourceSize.width: 16
                sourceSize.height: 16
                visible: false
            }
            Text {
                Layout.fillWidth: true
                text: Backend.lastError
                color: "#ffd9d9"
                font.pixelSize: Theme.fsMd
                elide: Text.ElideRight
            }
            IconButton {
                iconName: "x"
                glyphSize: 14
                restColor: "#ffd9d9"
                onClicked: Backend.clearError()
            }
        }
    }
}
