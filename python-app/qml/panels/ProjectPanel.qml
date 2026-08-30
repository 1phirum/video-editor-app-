pragma ComponentBehavior: Bound
// qmllint disable

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import CutPro 1.0
import "../theme"
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"
import "../project"

Rectangle {
    id: root
    color: Theme.bgSidebar

    signal requestImport()
    property string filterText: ""
    property string selectedMediaId: ""
    property var selectedMediaIds: []
    property string contextMediaId: ""
    property int selectionAnchor: -1
    property bool gridView: Backend.appSettings.defaultMediaView === "grid"
    readonly property bool effectsMode: panelTabs.currentIndex === 2
    property bool sortDescending: false

    function selectAllMedia() {
        var ids = []
        for (var i = 0; i < root.sortedMedia.length; ++i) {
            if (root.matches(root.sortedMedia[i]))
                ids.push(root.sortedMedia[i].id)
        }
        root.selectedMediaIds = ids
        root.selectedMediaId = ids.length > 0 ? ids[0] : ""
        root.selectionAnchor = ids.length > 0 ? 0 : -1
    }

    function addSelectedMediaToTimeline() {
        if (root.selectedMediaIds.length > 0)
            Backend.addMediaSelectionToTimeline(root.selectedMediaIds.slice(0))
    }

    property var sortedMedia: {
        var arr = []
        for (var i = 0; i < Backend.media.length; i++) {
            if (Backend.media[i].hiddenInProjectPanel !== true
                    && String(Backend.media[i].generatedBy || "")
                       !== "text_to_speech")
                arr.push(Backend.media[i])
        }
        arr.sort(function(a, b) {
            var nameA = (a.name || "").toLowerCase()
            var nameB = (b.name || "").toLowerCase()
            if (nameA < nameB) return root.sortDescending ? 1 : -1
            if (nameA > nameB) return root.sortDescending ? -1 : 1
            return 0
        })
        return arr
    }

    function matches(item) {
        return filterText === "" || item.name.toLowerCase().indexOf(filterText) >= 0
    }

    function formatDuration(milliseconds) {
        var total = Math.max(0, Math.floor(milliseconds / 1000))
        var minutes = Math.floor(total / 60)
        var seconds = total % 60
        return (minutes < 10 ? "0" : "") + minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function selectOrAdd(item, addToTimeline, modifiers, index) {
        // Nothing that arrives while the context menu is up may rewrite the
        // selection that menu is about to act on. On Windows the right-button
        // release can still be delivered to the item after the popup opened,
        // which would otherwise wipe a rubber-band selection down to one item.
        if (mediaContextMenu.visible)
            return
        root.forceActiveFocus()
        modifiers = modifiers || 0
        var ctrl = (modifiers & Qt.ControlModifier) !== 0
        var shift = (modifiers & Qt.ShiftModifier) !== 0
        var next = selectedMediaIds.slice()
        if (shift && selectionAnchor >= 0 && index >= 0) {
            var lo = Math.min(selectionAnchor, index)
            var hi = Math.max(selectionAnchor, index)
            next = []
            for (var i = lo; i <= hi; ++i)
                if (root.sortedMedia[i]) next.push(root.sortedMedia[i].id)
        } else if (ctrl) {
            var at = next.indexOf(item.id)
            if (at >= 0) next.splice(at, 1)
            else next.push(item.id)
        } else {
            next = [item.id]
        }
        selectedMediaIds = next
        selectedMediaId = next.length > 0 ? next[0] : ""
        selectionAnchor = index >= 0 ? index : selectionAnchor
        if (addToTimeline)
            for (var n = 0; n < next.length; ++n)
                Backend.addClip(next[n])
    }

    function mediaForId(mediaId) {
        for (var i = 0; i < root.sortedMedia.length; ++i) {
            if (root.sortedMedia[i].id === mediaId)
                return root.sortedMedia[i]
        }
        return null
    }

    function selectedIdsForAction() {
        var ids = selectedMediaIds.length ? selectedMediaIds.slice()
                  : (contextMediaId !== "" ? [contextMediaId] : [])
        // The right-clicked item must always be part of its own context action:
        // never let a stale selection leave the clicked clip behind while wiping
        // its neighbours.
        if (contextMediaId !== "" && ids.indexOf(contextMediaId) < 0)
            ids.push(contextMediaId)
        return ids
    }

    function requestDeleteSelected() {
        var ids = selectedIdsForAction()
        if (ids.length === 0)
            return
        deleteConfirmDialog.openFor(ids)
    }

    function deleteSelected(ids) {
        if (ids.length > 0)
            Backend.removeMediaSelection(ids)
        selectedMediaIds = []
        selectedMediaId = ""
        contextMediaId = ""
    }

    function requestRenameSelected() {
        var ids = selectedIdsForAction()
        if (ids.length !== 1)
            return
        var media = mediaForId(ids[0])
        if (!media)
            return
        renameDialog.mediaId = ids[0]
        renameField.text = media.name || ""
        renameDialog.open()
        renameField.forceActiveFocus()
        renameField.selectAll()
    }

    function mediaIdsInRect(view, rect) {
        var ids = []
        var isGrid = (view === mediaGrid)
        var itemsPerRow = isGrid ? Math.max(1, Math.floor(view.width / view.cellWidth)) : 1
        var listYOffset = 0

        for (var i = 0; i < root.sortedMedia.length; ++i) {
            var media = root.sortedMedia[i]
            var itemRect

            if (isGrid) {
                if (!root.matches(media)) continue
                var col = i % itemsPerRow
                var row = Math.floor(i / itemsPerRow)
                itemRect = Qt.rect(col * view.cellWidth, row * view.cellHeight, view.cellWidth, view.cellHeight)
            } else {
                var h = root.matches(media) ? 54 : 0
                if (h === 0) continue
                itemRect = Qt.rect(0, listYOffset, view.width, h)
                listYOffset += h + view.spacing
            }

            if (itemRect.x < rect.x + rect.width && itemRect.x + itemRect.width > rect.x
                    && itemRect.y < rect.y + rect.height && itemRect.y + itemRect.height > rect.y) {
                ids.push(media.id)
            }
        }
        return ids
    }

    ProjectMediaContextMenu {
        id: mediaContextMenu
        selectedCount: root.selectedMediaIds.length
        canAdd: root.selectedMediaIds.length > 0
        onAddToTimelineRequested: root.addSelectedMediaToTimeline()
        onRenameRequested: root.requestRenameSelected()
        onOpenRequested: Backend.openMediaExternally(root.contextMediaId)
        onRevealRequested: Backend.revealMediaInFileManager(root.contextMediaId)
        onCopyPathRequested: Backend.copyMediaPath(root.contextMediaId)
        onSelectAllRequested: root.selectAllMedia()
        onImportRequested: root.requestImport()
        onDeleteRequested: root.requestDeleteSelected()
    }

    Dialog {
        id: renameDialog
        property string mediaId: ""
        modal: true
        width: 340
        title: "Rename media"
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        onAccepted: Backend.renameMedia(mediaId, renameField.text)

        background: Rectangle {
            color: Theme.bgSidebar
            border.color: Theme.border
            radius: Theme.radiusMd
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Project item name"
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }
            TextField {
                id: renameField
                Layout.fillWidth: true
                color: Theme.textPrimary
                selectByMouse: true
                onAccepted: if (text.trim().length > 0) renameDialog.accept()
                background: Rectangle {
                    color: Theme.bgPrimary
                    border.color: renameField.activeFocus ? Theme.accent : Theme.border
                    radius: Theme.radiusSm
                }
            }
            Label {
                text: "The source file on disk will not be renamed."
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
            }
        }
    }

    // Window-native confirmation, centered on the application window. The stock
    // MessageDialog fallback draws "Yes"/"Cancel" as bare text with no shape, so
    // the same top-level modal window is kept and dressed with the app's own
    // pill buttons: an outlined Cancel and a filled destructive Delete.
    NativeModalWindow {
        id: deleteConfirmDialog
        property var mediaIds: []
        readonly property bool many: mediaIds.length > 1

        ownerWindow: root.Window.window
        dialogTitle: many ? "Delete selected media?" : "Delete media?"
        dialogWidth: 428
        dialogHeight: 214
        color: Theme.bgSidebar

        function openFor(ids) {
            mediaIds = ids
            openNative()
            confirmContent.forceActiveFocus()
        }
        function confirm() {
            var ids = mediaIds.slice()
            close()
            root.deleteSelected(ids)
        }
        // Enter confirms, unless the user has tabbed onto Cancel.
        function activateFocused() {
            if (deleteCancelButton.activeFocus)
                close()
            else
                confirm()
        }

        Item {
            id: confirmContent
            anchors.fill: parent
            focus: true
            Keys.onEscapePressed: deleteConfirmDialog.close()
            Keys.onReturnPressed: deleteConfirmDialog.activateFocused()
            Keys.onEnterPressed: deleteConfirmDialog.activateFocused()
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        width: 38
                        height: 38
                        radius: 19
                        color: Qt.rgba(0.973, 0.443, 0.443, 0.16)
                        border.width: 1
                        border.color: Qt.rgba(0.973, 0.443, 0.443, 0.45)
                        Image {
                            anchors.centerIn: parent
                            source: "../../assets/icons/trash-2.svg"
                            sourceSize.width: 19
                            sourceSize.height: 19
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5
                        Text {
                            Layout.fillWidth: true
                            text: deleteConfirmDialog.many
                                  ? "Delete these " + deleteConfirmDialog.mediaIds.length
                                    + " project items?"
                                  : "Delete this project item?"
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsLg
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: deleteConfirmDialog.many
                                  ? "This removes all of their timeline clips. The source files stay on disk."
                                  : "This removes all of its timeline clips. The source file stays on disk."
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fsSm
                            lineHeight: 1.25
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Item { Layout.fillHeight: true; Layout.minimumHeight: 4 }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Item { Layout.fillWidth: true }

                    Button {
                        id: deleteCancelButton
                        text: "Cancel"
                        implicitWidth: 96
                        implicitHeight: 34
                        onClicked: deleteConfirmDialog.close()
                        HoverHandler {
                            id: deleteCancelHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        contentItem: Text {
                            text: deleteCancelButton.text
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsMd
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: height / 2
                            color: deleteCancelButton.down
                                   ? Theme.hover
                                   : deleteCancelHover.hovered ? Qt.lighter(Theme.bgPrimary, 1.35)
                                                               : Theme.bgPrimary
                            border.width: 1
                            border.color: deleteCancelButton.activeFocus ? Theme.accent
                                                                         : Theme.textMuted
                        }
                    }
                    Button {
                        id: deleteConfirmButton
                        text: deleteConfirmDialog.many ? "Delete all" : "Delete"
                        implicitWidth: 116
                        implicitHeight: 34
                        onClicked: deleteConfirmDialog.confirm()
                        HoverHandler {
                            id: deleteConfirmHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        contentItem: Text {
                            text: deleteConfirmButton.text
                            color: "#1c1c1c"
                            font.pixelSize: Theme.fsMd
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: height / 2
                            color: deleteConfirmButton.down
                                   ? Qt.darker(Theme.danger, 1.25)
                                   : deleteConfirmHover.hovered ? Qt.lighter(Theme.danger, 1.1)
                                                                : Theme.danger
                            border.width: deleteConfirmButton.activeFocus ? 2 : 0
                            border.color: Theme.textPrimary
                        }
                    }
                }
            }
        }
    }

    ProjectMediaShortcuts {
        id: projectShortcuts
        focusTarget: root
        onSelectAllRequested: root.selectAllMedia()
        onPasteRequested: root.addSelectedMediaToTimeline()
        onDeleteRequested: root.requestDeleteSelected()
        onRenameRequested: root.requestRenameSelected()
    }

    component MediaPreview: Rectangle {
        id: preview
        required property var mediaItem
        property int iconSize: 18
        color: Theme.bgPrimary
        radius: Theme.radiusSm
        clip: true

        Image {
            anchors.fill: parent
            source: preview.mediaItem.thumbnailUrl || ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            visible: status === Image.Ready
        }
        Image {
            anchors.centerIn: parent
            source: preview.mediaItem.kind === "audio" ? "../../assets/icons/music.svg"
                  : preview.mediaItem.kind === "image" ? "../../assets/icons/image.svg"
                  : "../../assets/icons/film.svg"
            sourceSize.width: preview.iconSize
            sourceSize.height: preview.iconSize
            opacity: 0.7
            visible: preview.mediaItem.thumbnailUrl === ""
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelTabs {
            id: panelTabs
            Layout.fillWidth: true
            tabs: ["Project", "Media Browser", "Effects", "Info"]
            currentIndex: 0
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: Theme.bgSidebar

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                SearchField {
                    Layout.fillWidth: true
                    placeholder: root.effectsMode ? "Search effects" : "Search"
                    onTextChanged: root.filterText = text.toLowerCase()
                }
                IconButton {
                    visible: !root.effectsMode
                    iconName: "import"
                    boxSize: 26
                    glyphSize: 14
                    onClicked: root.requestImport()
                    ToolTip.visible: hovered
                    ToolTip.text: "Import media"
                }
                IconButton {
                    visible: !root.effectsMode
                    iconName: "list-video"
                    boxSize: 26
                    glyphSize: 14
                    restColor: !root.gridView ? Theme.accent : Theme.textPrimary
                    onClicked: root.gridView = false
                    ToolTip.visible: hovered
                    ToolTip.text: "List view"
                }
                IconButton {
                    visible: !root.effectsMode
                    iconName: "image"
                    boxSize: 26
                    glyphSize: 14
                    restColor: root.gridView ? Theme.accent : Theme.textPrimary
                    onClicked: root.gridView = true
                    ToolTip.visible: hovered
                    ToolTip.text: "Thumbnail grid"
                }
                IconButton {
                    iconName: "arrow-up-down"
                    boxSize: 26
                    glyphSize: 14
                    onClicked: root.sortDescending = !root.sortDescending
                    ToolTip.visible: hovered
                    ToolTip.text: root.sortDescending ? "Sort descending" : "Sort ascending"
                }
                IconButton {
                    visible: !root.effectsMode
                    iconName: "plus"
                    boxSize: 26
                    glyphSize: 14
                    enabled: root.selectedMediaId !== ""
                    onClicked: Backend.addClip(root.selectedMediaId)
                    ToolTip.visible: hovered
                    ToolTip.text: "Add to timeline"
                }
            }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    anchors.fill: parent
                    z: 30
                    visible: Backend.mediaImportInProgress
                    color: Qt.darker(Theme.bgSidebar, 1.32)
                    Column {
                        anchors.centerIn: parent
                        spacing: 12
                        Item {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 42; height: 42
                            Canvas {
                                anchors.fill: parent
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.lineWidth = 4
                                    ctx.lineCap = "round"
                                    ctx.strokeStyle = Theme.accent
                                    ctx.beginPath()
                                    ctx.arc(width / 2, height / 2, 15,
                                            Math.PI * 0.12, Math.PI * 1.72)
                                    ctx.stroke()
                                }
                            }
                            RotationAnimator on rotation {
                                from: 0; to: 360; duration: 760
                                loops: Animation.Infinite
                                running: Backend.mediaImportInProgress
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Importing media… " + Backend.mediaImportProgress + "%"
                            color: "#ffffff"
                            font.pixelSize: Theme.fsMd
                            font.bold: true
                        }
                        ProgressBar {
                            id: importProgressBar
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 190; height: 5
                            from: 0; to: 100
                            value: Backend.mediaImportProgress
                            background: Rectangle { color: "#3d3d3d"; radius: 2 }
                            contentItem: Item {
                                Rectangle {
                                    width: importProgressBar.visualPosition * parent.width
                                    height: parent.height
                                    radius: 2
                                    color: Theme.accent
                                }
                            }
                        }
                    }
                }

            EffectsBrowser {
                anchors.fill: parent
                visible: root.effectsMode
                filterText: root.filterText
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                visible: !root.effectsMode && root.sortedMedia.length === 0

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 48; height: 48; radius: 24
                    color: Theme.bgPrimary
                    Image {
                        anchors.centerIn: parent
                        source: "../../assets/icons/import.svg"
                        sourceSize.width: 22; sourceSize.height: 22
                        opacity: 0.5
                    }
                    TapHandler { cursorShape: Qt.PointingHandCursor; onTapped: root.requestImport() }
                }
                Text { Layout.alignment: Qt.AlignHCenter; text: "No media in this project"; color: Theme.textSecondary; font.pixelSize: Theme.fsMd }
                Text { Layout.alignment: Qt.AlignHCenter; text: "Import media to start"; color: Theme.textMuted; font.pixelSize: Theme.fsSm }
            }

            ListView {
                id: mediaList
                anchors.fill: parent
                visible: !root.effectsMode && root.sortedMedia.length > 0 && !root.gridView
                clip: true
                model: root.sortedMedia
                onCountChanged: ModelGuard.note("project.media", count)
                spacing: 1

                Rectangle {
                    id: listSelectionVisual
                    parent: mediaList.contentItem
                    visible: listSelectionArea.selecting
                    x: Math.min(listSelectionArea.startPoint.x, listSelectionArea.endPoint.x)
                    y: Math.min(listSelectionArea.startPoint.y, listSelectionArea.endPoint.y)
                    width: Math.abs(listSelectionArea.endPoint.x - listSelectionArea.startPoint.x)
                    height: Math.abs(listSelectionArea.endPoint.y - listSelectionArea.startPoint.y)
                    color: Qt.rgba(0.29, 0.56, 0.96, 0.16)
                    border.color: Theme.accent
                    border.width: 1
                    z: 999
                }

                Timer {
                    id: listAutoScroll
                    interval: 16
                    repeat: true
                    running: listSelectionArea.selecting
                    onTriggered: {
                        var edgeZone = 40
                        var viewportY = listSelectionArea.lastMouseY - mediaList.contentY
                        var scrollSpeed = 0
                        if (viewportY < edgeZone) {
                            scrollSpeed = -Math.round((edgeZone - viewportY) * 0.3)
                        } else if (viewportY > mediaList.height - edgeZone) {
                            scrollSpeed = Math.round((viewportY - (mediaList.height - edgeZone)) * 0.3)
                        }
                        if (scrollSpeed !== 0) {
                            var minY = mediaList.originY
                            var maxY = mediaList.originY + mediaList.contentHeight - mediaList.height
                            var oldContentY = mediaList.contentY
                            mediaList.contentY = Math.max(minY, Math.min(mediaList.contentY + scrollSpeed, maxY))
                            var actualScroll = mediaList.contentY - oldContentY
                            listSelectionArea.lastMouseY += actualScroll
                            listSelectionArea.endPoint.y += actualScroll
                            listSelectionArea.updateSelection()
                        }
                    }
                }

                MouseArea {
                    id: listSelectionArea
                    parent: mediaList.contentItem
                    z: -1
                    width: Math.max(mediaList.width, mediaList.contentWidth)
                    height: Math.max(mediaList.height, mediaList.contentHeight)
                    acceptedButtons: Qt.LeftButton
                    preventStealing: true
                    property bool selecting: false
                    property point startPoint
                    property point endPoint
                    property real lastMouseY: 0
                    property int modifiers: 0

                    function updateSelection() {
                        var rect = Qt.rect(Math.min(startPoint.x, endPoint.x),
                                           Math.min(startPoint.y, endPoint.y),
                                           Math.abs(endPoint.x - startPoint.x),
                                           Math.abs(endPoint.y - startPoint.y))
                        var ids = root.mediaIdsInRect(mediaList, rect)
                        selectedMediaIds = (modifiers & Qt.ControlModifier)
                                           ? selectedMediaIds.concat(ids) : ids
                        selectedMediaId = selectedMediaIds.length ? selectedMediaIds[0] : ""
                    }

                    onPressed: mouse => {
                        root.forceActiveFocus()
                        startPoint = Qt.point(mouse.x, mouse.y)
                        endPoint = startPoint
                        lastMouseY = mouse.y
                        selecting = false
                        modifiers = mouse.modifiers
                        if (!(modifiers & Qt.ControlModifier) && !(modifiers & Qt.ShiftModifier)) {
                            selectedMediaIds = []
                            selectedMediaId = ""
                        }
                    }
                    onPositionChanged: mouse => {
                        if (!pressed) return
                        endPoint = Qt.point(mouse.x, mouse.y)
                        lastMouseY = mouse.y
                        selecting = Math.abs(endPoint.x - startPoint.x)
                                   + Math.abs(endPoint.y - startPoint.y) > 5
                        if (selecting) updateSelection()
                    }
                    onReleased: mouse => {
                        if (selecting) {
                            updateSelection()
                        }
                        selecting = false
                    }
                }

                delegate: Rectangle {
                    id: mediaRow
                    required property var modelData
                    required property int index
                    z: 1
                    readonly property bool itemMatches: root.matches(modelData)
                    width: mediaList.width
                    height: itemMatches ? 54 : 0
                    visible: itemMatches
                    color: root.selectedMediaIds.indexOf(modelData.id) >= 0 ? Qt.rgba(0.29, 0.56, 0.96, 0.22) : rowHover.hovered ? Theme.hover : "transparent"

                    MediaPreview {
                        id: listPreview
                        mediaItem: mediaRow.modelData
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 46; height: 38
                        border.width: root.selectedMediaIds.indexOf(mediaRow.modelData.id) >= 0 ? 2 : 0
                        border.color: Theme.accent
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 62
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text { width: parent.width; text: mediaRow.modelData.name; color: Theme.textPrimary; font.pixelSize: Theme.fsSm; elide: Text.ElideMiddle }
                        Text { text: mediaRow.modelData.kind.toUpperCase() + "  " + root.formatDuration(mediaRow.modelData.durationMs); color: Theme.textMuted; font.pixelSize: Theme.fsXs }
                    }
                    HoverHandler { id: rowHover }
                    MediaDragHandle {
                        anchors.fill: parent
                        dragMediaId: mediaRow.modelData.id
                        dragMediaKind: mediaRow.modelData.kind
                        dragMediaName: mediaRow.modelData.name || "Media"
                        dragMediaIds: root.selectedMediaIds.indexOf(mediaRow.modelData.id) >= 0 ? root.selectedMediaIds : [mediaRow.modelData.id]
                        dragDurationMs: Number(mediaRow.modelData.durationMs || 5000)
                        previewUrl: mediaRow.modelData.thumbnailUrl || ""
                        onClicked: modifiers => root.selectOrAdd(mediaRow.modelData, false, modifiers, mediaRow.index)
                        onDoubleClicked: modifiers => root.selectOrAdd(mediaRow.modelData, true, modifiers, mediaRow.index)
                        onContextRequested: {
                            root.forceActiveFocus()
                            if (root.selectedMediaIds.indexOf(mediaRow.modelData.id) < 0)
                                root.selectOrAdd(mediaRow.modelData, false, 0, mediaRow.index)
                            root.contextMediaId = mediaRow.modelData.id
                            mediaContextMenu.popup()
                        }
                    }
                }
            }

            GridView {
                id: mediaGrid
                model: root.sortedMedia
                anchors.fill: parent
                anchors.margins: 6
                visible: !root.effectsMode && root.sortedMedia.length > 0 && root.gridView
                clip: true
                cellWidth: 126
                cellHeight: 106

                Rectangle {
                    id: gridSelectionVisual
                    parent: mediaGrid.contentItem
                    visible: gridSelectionArea.selecting
                    x: Math.min(gridSelectionArea.startPoint.x, gridSelectionArea.endPoint.x)
                    y: Math.min(gridSelectionArea.startPoint.y, gridSelectionArea.endPoint.y)
                    width: Math.abs(gridSelectionArea.endPoint.x - gridSelectionArea.startPoint.x)
                    height: Math.abs(gridSelectionArea.endPoint.y - gridSelectionArea.startPoint.y)
                    color: Qt.rgba(0.29, 0.56, 0.96, 0.16)
                    border.color: Theme.accent
                    border.width: 1
                    z: 999
                }

                Timer {
                    id: gridAutoScroll
                    interval: 16
                    repeat: true
                    running: gridSelectionArea.selecting
                    onTriggered: {
                        var edgeZone = 40
                        var viewportY = gridSelectionArea.lastMouseY - mediaGrid.contentY
                        var scrollSpeed = 0
                        if (viewportY < edgeZone) {
                            scrollSpeed = -Math.round((edgeZone - viewportY) * 0.3)
                        } else if (viewportY > mediaGrid.height - edgeZone) {
                            scrollSpeed = Math.round((viewportY - (mediaGrid.height - edgeZone)) * 0.3)
                        }
                        if (scrollSpeed !== 0) {
                            var minY = mediaGrid.originY
                            var maxY = mediaGrid.originY + mediaGrid.contentHeight - mediaGrid.height
                            var oldContentY = mediaGrid.contentY
                            mediaGrid.contentY = Math.max(minY, Math.min(mediaGrid.contentY + scrollSpeed, maxY))
                            var actualScroll = mediaGrid.contentY - oldContentY
                            gridSelectionArea.lastMouseY += actualScroll
                            gridSelectionArea.endPoint.y += actualScroll
                            gridSelectionArea.updateSelection()
                        }
                    }
                }

                MouseArea {
                    id: gridSelectionArea
                    parent: mediaGrid.contentItem
                    z: -1
                    width: Math.max(mediaGrid.width, mediaGrid.contentWidth)
                    height: Math.max(mediaGrid.height, mediaGrid.contentHeight)
                    acceptedButtons: Qt.LeftButton
                    preventStealing: true
                    property bool selecting: false
                    property point startPoint
                    property point endPoint
                    property real lastMouseY: 0
                    property int modifiers: 0

                    function updateSelection() {
                        var rect = Qt.rect(Math.min(startPoint.x, endPoint.x),
                                           Math.min(startPoint.y, endPoint.y),
                                           Math.abs(endPoint.x - startPoint.x),
                                           Math.abs(endPoint.y - startPoint.y))
                        var ids = root.mediaIdsInRect(mediaGrid, rect)
                        selectedMediaIds = (modifiers & Qt.ControlModifier)
                                           ? selectedMediaIds.concat(ids) : ids
                        selectedMediaId = selectedMediaIds.length ? selectedMediaIds[0] : ""
                    }

                    onPressed: mouse => {
                        root.forceActiveFocus()
                        startPoint = Qt.point(mouse.x, mouse.y)
                        endPoint = startPoint
                        lastMouseY = mouse.y
                        selecting = false
                        modifiers = mouse.modifiers
                        if (!(modifiers & Qt.ControlModifier) && !(modifiers & Qt.ShiftModifier)) {
                            selectedMediaIds = []
                            selectedMediaId = ""
                        }
                    }
                    onPositionChanged: mouse => {
                        if (!pressed) return
                        endPoint = Qt.point(mouse.x, mouse.y)
                        lastMouseY = mouse.y
                        selecting = Math.abs(endPoint.x - startPoint.x)
                                   + Math.abs(endPoint.y - startPoint.y) > 5
                        if (selecting) updateSelection()
                    }
                    onReleased: mouse => {
                        if (selecting) {
                            updateSelection()
                        }
                        selecting = false
                    }
                }

                delegate: Item {
                    id: gridCell
                    required property var modelData
                    required property int index
                    z: 1
                    width: mediaGrid.cellWidth
                    height: mediaGrid.cellHeight
                    visible: root.matches(modelData)

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 3
                        radius: Theme.radiusSm
                        color: root.selectedMediaIds.indexOf(gridCell.modelData.id) >= 0 ? Qt.rgba(0.29, 0.56, 0.96, 0.2) : gridHover.hovered ? Theme.hover : "transparent"
                        border.width: root.selectedMediaIds.indexOf(gridCell.modelData.id) >= 0 ? 2 : 0
                        border.color: Theme.accent

                        MediaPreview {
                            id: gridPreview
                            mediaItem: gridCell.modelData
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 5
                            height: 68
                            iconSize: 24
                            border.width: root.selectedMediaIds.indexOf(gridCell.modelData.id) >= 0 ? 2 : 0
                            border.color: Theme.accent
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 6
                            text: gridCell.modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsXs
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideMiddle
                        }
                        HoverHandler { id: gridHover }
                        MediaDragHandle {
                            anchors.fill: parent
                            dragMediaId: gridCell.modelData.id
                            dragMediaKind: gridCell.modelData.kind
                            dragMediaName: gridCell.modelData.name || "Media"
                            dragMediaIds: root.selectedMediaIds.indexOf(gridCell.modelData.id) >= 0 ? root.selectedMediaIds : [gridCell.modelData.id]
                            dragDurationMs: Number(gridCell.modelData.durationMs || 5000)
                            previewUrl: gridCell.modelData.thumbnailUrl || ""
                            onClicked: modifiers => root.selectOrAdd(gridCell.modelData, false, modifiers, gridCell.index)
                            onDoubleClicked: modifiers => root.selectOrAdd(gridCell.modelData, true, modifiers, gridCell.index)
                        onContextRequested: {
                            root.forceActiveFocus()
                            if (root.selectedMediaIds.indexOf(gridCell.modelData.id) < 0)
                                    root.selectOrAdd(gridCell.modelData, false, 0, gridCell.index)
                                root.contextMediaId = gridCell.modelData.id
                                mediaContextMenu.popup()
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: Backend
        function onEffectsBrowserRequested() {
            panelTabs.currentIndex = 2
        }
    }
}
