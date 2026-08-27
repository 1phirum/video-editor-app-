pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
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

// Port of ImportView.tsx in its no-backend / empty state:
//   - no folder opened (currentPath = null) -> "Select a folder to view files."
//   - known folders unavailable -> LOCAL items disabled
//   All controls (inputs, toggles, sliders, section accordions) are live locally.
Rectangle {
    id: root
    color: Theme.bgPanel

    signal requestExit()
    signal requestCreate()

    // ---- Local UI state -------------------------------------------------
    property string projectName: "Untitled"
    property string sequenceName: "Sequence 01"
    property real itemScale: 1.0
    property bool gridView: false          // viewMode: 'list' default
    property bool mediaOnly: true
    property bool showHidden: false
    property bool sortAscending: true
    property bool copyMedia: false
    property bool createSequence: true
    property bool autoTranscribe: false

    // Section accordions
    property bool favOpen: true
    property bool localOpen: true
    property bool devicesOpen: true
    property bool secOrganize: true
    property bool secCopy: false
    property bool secSequence: true
    property bool secTranscribe: false

    // ---- Reusable pieces ------------------------------------------------

    // Rounded pill switch (matches the role="switch" toggles).
    component Toggle: Rectangle {
        id: tg
        property bool checked: false
        signal toggled()
        implicitWidth: 28
        implicitHeight: 16
        radius: 8
        color: checked ? Theme.accent : Theme.border
        Rectangle {
            width: 12; height: 12; radius: 6
            color: "white"
            y: 2
            x: tg.checked ? tg.width - width - 2 : 2
        }
        TapHandler { cursorShape: Qt.PointingHandCursor; onTapped: tg.toggled() }
    }

    // A small filled folder silhouette (no folder.svg in the icon set).
    component FolderGlyph: Item {
        id: fg
        property color tint: Theme.textMuted
        property int px: 14
        implicitWidth: px
        implicitHeight: px
        Rectangle {   // tab
            x: 0; y: fg.px * 0.16
            width: fg.px * 0.5; height: fg.px * 0.24
            radius: 1; color: fg.tint
        }
        Rectangle {   // body
            x: 0; y: fg.px * 0.3
            width: fg.px; height: fg.px * 0.54
            radius: 1.5; color: fg.tint
        }
    }

    // chevron-right rotated to point down when `down` (no chevron-down.svg).
    component Chevron: Image {
        property bool down: false
        source: "../../assets/icons/chevron-right.svg"
        sourceSize.width: 14; sourceSize.height: 14
        opacity: 0.65
        rotation: down ? 90 : 0
    }

    // Bordered dark field shell (bg-input + border).
    component FieldBox: Rectangle {
        color: Theme.bgPrimary
        border.color: Theme.border
        radius: Theme.radiusSm
        implicitHeight: 28
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ==== Top bar: project name / location / template ================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.bgSidebar

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 16

                // Project name
                RowLayout {
                    spacing: 8
                    Text { text: "Project name"; color: Theme.textMuted; font.pixelSize: Theme.fsMd }
                    FieldBox {
                        Layout.preferredWidth: 200
                        Layout.preferredHeight: 28
                        TextField {
                            id: nameField
                            anchors.fill: parent
                            leftPadding: 8; rightPadding: 8; topPadding: 0; bottomPadding: 0
                            verticalAlignment: TextInput.AlignVCenter
                            background: null
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsMd
                            text: root.projectName
                            selectionColor: Theme.accent
                            onTextChanged: root.projectName = text
                        }
                    }
                }

                // Project location (fills the middle)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Project location"; color: Theme.textMuted; font.pixelSize: Theme.fsMd }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 420
                        Layout.preferredHeight: 28
                        radius: Theme.radiusSm
                        color: Theme.bgPrimary
                        border.color: Theme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            Text {
                                Layout.fillWidth: true
                                text: "None selected"
                                elide: Text.ElideRight
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsMd
                            }
                            Chevron { down: true }
                        }
                        TapHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }

                // Project template
                RowLayout {
                    spacing: 8
                    Text { text: "Project template"; color: Theme.textMuted; font.pixelSize: Theme.fsMd }
                    Rectangle {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 28
                        radius: Theme.radiusSm
                        color: Theme.bgPrimary
                        border.color: Theme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            Text { Layout.fillWidth: true; text: "None"; color: Theme.textPrimary; font.pixelSize: Theme.fsMd }
                            Chevron { down: true }
                        }
                    }
                }
            }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

        // ==== Middle: 3 columns =========================================
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- Left aside: FAVORITES / LOCAL / DEVICES ----
            Rectangle {
                Layout.preferredWidth: Math.min(300, Math.max(180, root.width * 0.18))
                Layout.fillHeight: true
                color: Theme.bgSidebar

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 8
                    spacing: 8

                    // section header button
                    component SectionHeader: Item {
                        id: sh
                        property string title: ""
                        property bool open: true
                        signal clicked()
                        Layout.fillWidth: true
                        implicitHeight: 22
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            spacing: 4
                            Chevron { down: sh.open; sourceSize.width: 12; sourceSize.height: 12 }
                            Text {
                                text: sh.title
                                color: Theme.textMuted
                                font.pixelSize: Theme.fsXs
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1.2
                            }
                            Item { Layout.fillWidth: true }
                        }
                        TapHandler { cursorShape: Qt.PointingHandCursor; onTapped: sh.clicked() }
                    }

                    // FAVORITES (empty in this state)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        SectionHeader { title: "FAVORITES"; open: root.favOpen; onClicked: root.favOpen = !root.favOpen }
                    }

                    // LOCAL
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        SectionHeader { title: "LOCAL"; open: root.localOpen; onClicked: root.localOpen = !root.localOpen }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            spacing: 1
                            visible: root.localOpen

                            Repeater {
                                model: [
                                    { label: "Home",      icon: "home" },
                                    { label: "Desktop",   icon: "" },
                                    { label: "Documents", icon: "" },
                                    { label: "Downloads", icon: "" },
                                    { label: "Movies",    icon: "film" },
                                    { label: "Music",     icon: "music" },
                                    { label: "Pictures",  icon: "image" }
                                ]
                                delegate: Rectangle {
                                    id: li
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 28
                                    radius: Theme.radiusSm
                                    color: "transparent"
                                    opacity: 0.4     // disabled (no known folders)

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        spacing: 8
                                        Item {
                                            Layout.preferredWidth: 14
                                            Layout.preferredHeight: 14
                                            Image {
                                                anchors.fill: parent
                                                visible: li.modelData.icon !== ""
                                                source: li.modelData.icon !== "" ? "../../assets/icons/" + li.modelData.icon + ".svg" : ""
                                                sourceSize.width: 14; sourceSize.height: 14
                                            }
                                            FolderGlyph { anchors.centerIn: parent; visible: li.modelData.icon === ""; tint: Theme.textPrimary }
                                        }
                                        Text { text: li.modelData.label; color: Theme.textPrimary; font.pixelSize: Theme.fsMd }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }
                        }
                    }

                    // DEVICES
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        SectionHeader { title: "DEVICES"; open: root.devicesOpen; onClicked: root.devicesOpen = !root.devicesOpen }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            implicitHeight: 28
                            radius: Theme.radiusSm
                            visible: root.devicesOpen
                            color: "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                spacing: 8
                                FolderGlyph { tint: Theme.accent }
                                Text { text: "Browse Local Folder..."; color: Theme.accent; font.pixelSize: Theme.fsMd; font.weight: Font.Medium }
                                Item { Layout.fillWidth: true }
                            }
                            TapHandler { cursorShape: Qt.PointingHandCursor }
                        }
                    }
                }

                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
            }

            // ---- Main column ----
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bgPanel

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Toolbar
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        color: "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 8

                            // path breadcrumb
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Button {
                                    id: pathFolderBtn
                                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                    implicitWidth: 28; implicitHeight: 28
                                    flat: true; hoverEnabled: false
                                    background: Rectangle { radius: Theme.radiusSm; color: "transparent" }
                                    contentItem: Item { FolderGlyph { anchors.centerIn: parent; px: 16; tint: Theme.textPrimary } }
                                }
                                Chevron {}
                                Text {
                                    Layout.fillWidth: true
                                    text: "No folder selected"
                                    elide: Text.ElideRight
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fsMd
                                }
                            }

                            // item-scale slider
                            Item {
                                id: scaleSlider
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 16
                                Layout.alignment: Qt.AlignVCenter
                                readonly property real frac: (root.itemScale - 0.8) / (1.8 - 0.8)
                                function seek(mx) { root.itemScale = Math.max(0.8, Math.min(1.8, 0.8 + (mx / width) * 1.0)) }
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width; height: 4; radius: 2
                                    color: Theme.bgPrimary
                                    Rectangle {
                                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                                        width: parent.width * scaleSlider.frac; radius: 2; color: Theme.accent
                                    }
                                }
                                Rectangle {
                                    width: 10; height: 10; radius: 5; color: Theme.textPrimary
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: (parent.width - 10) * scaleSlider.frac
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onPressed: (m) => scaleSlider.seek(m.x)
                                    onPositionChanged: (m) => { if (pressed) scaleSlider.seek(m.x) }
                                }
                            }

                            // grid view button (drawn 2x2 glyph)
                            Button {
                                id: gridViewBtn
                                HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                implicitWidth: 28; implicitHeight: 28
                                flat: true; hoverEnabled: false
                                background: Rectangle {
                                    radius: Theme.radiusSm
                                    color: root.gridView ? Theme.hover : "transparent"
                                }
                                contentItem: Item {
                                    Grid {
                                        anchors.centerIn: parent
                                        columns: 2; rowSpacing: 2; columnSpacing: 2
                                        Repeater {
                                            model: 4
                                            delegate: Rectangle {
                                                required property int index
                                                width: 5; height: 5; radius: 1
                                                color: root.gridView ? Theme.textPrimary : Theme.textMuted
                                            }
                                        }
                                    }
                                }
                                onClicked: root.gridView = true
                            }

                            // list view button (drawn 3-line glyph)
                            Button {
                                id: listViewBtn
                                HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                implicitWidth: 28; implicitHeight: 28
                                flat: true; hoverEnabled: false
                                background: Rectangle {
                                    radius: Theme.radiusSm
                                    color: !root.gridView ? Theme.hover : "transparent"
                                }
                                contentItem: Item {
                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 3
                                        Repeater {
                                            model: 3
                                            delegate: Rectangle {
                                                required property int index
                                                width: 14; height: 2; radius: 1
                                                color: !root.gridView ? Theme.textPrimary : Theme.textMuted
                                            }
                                        }
                                    }
                                }
                                onClicked: root.gridView = false
                            }

                            IconButton {
                                iconName: "arrow-up-down"; boxSize: 28; glyphSize: 14
                                onClicked: root.sortAscending = !root.sortAscending
                            }
                            IconButton {
                                iconName: "filter"; boxSize: 28; glyphSize: 14
                                restColor: root.mediaOnly ? Theme.accent : Theme.textMuted
                                onClicked: root.mediaOnly = !root.mediaOnly
                            }
                            IconButton {
                                iconName: root.showHidden ? "eye" : "eye-off"; boxSize: 28; glyphSize: 14
                                onClicked: root.showHidden = !root.showHidden
                            }
                            SearchField { Layout.preferredWidth: 192; Layout.leftMargin: 4; placeholder: "" }
                        }

                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                    }

                    // List column header (list mode only)
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        color: "transparent"
                        visible: !root.gridView
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 0
                            Item { Layout.preferredWidth: 32 }
                            Item { Layout.preferredWidth: 32 }
                            Text { Layout.fillWidth: true; text: "NAME"; color: Theme.textMuted; font.pixelSize: Theme.fsXs; font.letterSpacing: 1.2 }
                            Text { Layout.preferredWidth: 96; text: "SIZE"; color: Theme.textMuted; font.pixelSize: Theme.fsXs; font.letterSpacing: 1.2 }
                            Text { Layout.preferredWidth: 128; text: "MODIFIED"; color: Theme.textMuted; font.pixelSize: Theme.fsXs; font.letterSpacing: 1.2 }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(0.05, 0.05, 0.05, 0.5) }
                    }

                    // File body (empty state)
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Text {
                            anchors.centerIn: parent
                            text: "Select a folder to view files."
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsMd
                        }
                    }
                }

                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
            }

            // ---- Right aside: Import settings ----
            Rectangle {
                Layout.preferredWidth: Math.min(400, Math.max(250, root.width * 0.22))
                Layout.fillHeight: true
                color: Theme.bgSidebar

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // header
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        color: "transparent"
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Import settings"; color: Theme.textPrimary
                            font.pixelSize: 14; font.weight: Font.DemiBold
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                    }

                    // settings sections
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 0

                        // A settings accordion row: header (chevron + title + optional toggle) + body.
                        component SettingsSection: ColumnLayout {
                            id: ss
                            property string title: ""
                            property bool open: false
                            property bool hasToggle: false
                            property bool toggleOn: false
                            default property alias body: bodyHolder.data
                            signal headerClicked()
                            signal toggled()

                            Layout.fillWidth: true
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 44
                                color: "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 16
                                    anchors.rightMargin: 16
                                    spacing: 8
                                    Chevron { down: ss.open }
                                    Text { text: ss.title; color: Theme.textPrimary; font.pixelSize: Theme.fsMd }
                                    Item { Layout.fillWidth: true }
                                    Toggle {
                                        visible: ss.hasToggle
                                        checked: ss.toggleOn
                                        onToggled: ss.toggled()
                                    }
                                }
                                TapHandler { cursorShape: Qt.PointingHandCursor; onTapped: ss.headerClicked() }
                            }

                            Item {
                                id: bodyHolder
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                Layout.topMargin: ss.open ? 0 : 0
                                Layout.bottomMargin: ss.open ? 16 : 0
                                visible: ss.open
                                implicitHeight: visible ? childrenRect.height : 0
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(0.05, 0.05, 0.05, 0.5) }
                        }

                        SettingsSection {
                            title: "Organize Media"
                            open: root.secOrganize
                            onHeaderClicked: root.secOrganize = !root.secOrganize
                            Text { width: parent.width; text: "0 media files selected"; color: Theme.textMuted; font.pixelSize: Theme.fsMd; wrapMode: Text.WordWrap }
                        }

                        SettingsSection {
                            title: "Copy media"
                            open: root.secCopy
                            hasToggle: true
                            toggleOn: root.copyMedia
                            onHeaderClicked: root.secCopy = !root.secCopy
                            onToggled: root.copyMedia = !root.copyMedia
                            Text {
                                width: parent.width
                                text: "Copy selected files into " + (root.projectName || "project") + "/Media."
                                color: Theme.textMuted; font.pixelSize: Theme.fsMd; wrapMode: Text.WordWrap
                            }
                        }

                        SettingsSection {
                            title: "Create new sequence"
                            open: root.secSequence
                            hasToggle: true
                            toggleOn: root.createSequence
                            onHeaderClicked: root.secSequence = !root.secSequence
                            onToggled: root.createSequence = !root.createSequence
                            ColumnLayout {
                                width: parent.width
                                spacing: 4
                                Text { text: "Name"; color: Theme.textMuted; font.pixelSize: Theme.fsMd }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 30
                                    radius: Theme.radiusSm
                                    color: Theme.bgPrimary
                                    border.color: Theme.border
                                    opacity: root.createSequence ? 1.0 : 0.4
                                    TextField {
                                        anchors.fill: parent
                                        enabled: root.createSequence
                                        leftPadding: 8; rightPadding: 8; topPadding: 0; bottomPadding: 0
                                        verticalAlignment: TextInput.AlignVCenter
                                        background: null
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fsMd
                                        text: root.sequenceName
                                        selectionColor: Theme.accent
                                        onTextChanged: root.sequenceName = text
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: "Automatic transcription"
                            open: root.secTranscribe
                            hasToggle: true
                            toggleOn: root.autoTranscribe
                            onHeaderClicked: root.secTranscribe = !root.secTranscribe
                            onToggled: root.autoTranscribe = !root.autoTranscribe
                            ColumnLayout {
                                width: parent.width
                                spacing: 4
                                Text { text: "Language"; color: Theme.textMuted; font.pixelSize: Theme.fsMd }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 30
                                    radius: Theme.radiusSm
                                    color: Theme.bgPrimary
                                    border.color: Theme.border
                                    opacity: root.autoTranscribe ? 1.0 : 0.4
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        Text { Layout.fillWidth: true; text: "Auto-detect"; color: Theme.textPrimary; font.pixelSize: Theme.fsMd }
                                        Chevron { down: true }
                                    }
                                }
                                Text {
                                    Layout.topMargin: 6
                                    Layout.fillWidth: true
                                    text: "Requires the local speech model downloaded from the Text panel."
                                    color: Theme.textMuted; font.pixelSize: Theme.fsSm; wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }

                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border }
            }
        }

        // ==== Bottom action bar =========================================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.bgSidebar

            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

            // centered info pill
            Rectangle {
                anchors.centerIn: parent
                width: pillRow.implicitWidth + 32
                height: 34
                radius: Theme.radiusSm
                color: Theme.accent
                RowLayout {
                    id: pillRow
                    anchors.centerIn: parent
                    spacing: 8
                    Item {
                        Layout.preferredWidth: 16; Layout.preferredHeight: 16
                        Rectangle { anchors.fill: parent; radius: 8; color: "transparent"; border.color: "white"; border.width: 1.5 }
                        Text { anchors.centerIn: parent; text: "i"; color: "white"; font.pixelSize: 10; font.weight: Font.Bold }
                    }
                    Text { text: "Create blank project (or select files/folders to import)"; color: "white"; font.pixelSize: Theme.fsMd }
                }
            }

            // right action buttons
            RowLayout {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Button {
                    id: exitBtn
                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    implicitHeight: 30
                    implicitWidth: exitLabel.implicitWidth + 48
                    flat: true; hoverEnabled: false
                    background: Rectangle {
                        radius: height / 2
                        color: "transparent"
                        border.color: Theme.textSecondary
                        border.width: 1
                    }
                    contentItem: Text {
                        id: exitLabel
                        text: "Exit"; color: Theme.textPrimary
                        font.pixelSize: Theme.fsMd; font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: root.requestExit()
                }

                Button {
                    id: createBtn
                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    implicitHeight: 30
                    implicitWidth: createLabel.implicitWidth + 48
                    flat: true; hoverEnabled: false
                    enabled: root.projectName.trim().length > 0
                    opacity: enabled ? 1.0 : 0.4
                    background: Rectangle { radius: height / 2; color: Theme.accent; opacity: 1.0 }
                    contentItem: Text {
                        id: createLabel
                        text: "Create"; color: "white"
                        font.pixelSize: Theme.fsMd; font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        Backend.newProject(root.projectName, "", root.createSequence, root.sequenceName)
                        root.requestCreate()
                    }
                }
            }
        }
    }
}
