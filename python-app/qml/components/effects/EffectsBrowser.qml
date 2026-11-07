pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

Item {
    id: root

    property string filterText: ""
    readonly property var selectedClip: Backend.selectedClip
    readonly property var selectedMedia: mediaForClip(selectedClip)
    property var expandedFolders: ({ "video": true, "audio": true })

    // "folder" is the Premiere-style tree this panel shipped with; "sidebar" is
    // the CapCut arrangement - categories down the left, the effects of the one
    // that was clicked on the right. Both read the same definitions through the
    // same two helpers below, so neither layout can show a set of effects the
    // other does not.
    //
    // Seeded from the setting the layout dropdown writes, so the choice survives
    // a restart. Assigning it drops that binding, which is what is wanted: from
    // the first press onwards the dropdown is what decides.
    property string viewMode: Backend.appSettings.effectsBrowserView === "sidebar"
                              ? "sidebar" : "folder"

    // One of the six bucket keys below - the same keys the folder tree uses for
    // its own top-level rows, so both layouts name a bucket the same way.
    property string sidebarKey: "video"

    // The rail lists the tree's top level and nothing under it. Per-category rows
    // were what it listed before, and eleven of "Adjust / Blur & Sharpen /
    // Distort / Keying ..." is a column you have to read rather than aim at. The
    // categories have not gone anywhere - they are headings in the pane beside it.
    //
    // In the tree's order, which puts Video Effects before Video Transitions, so
    // that switching layout does not reorder the same six names.
    // A bucket is picked out by the first word of a definition's category, and
    // narrowed by media type where the same word covers both - "Transition /
    // Dissolve" is video and "Transition / Fade" is audio, and each belongs in a
    // different bucket. An empty mediaType means the bucket takes both, which is
    // what Presets does: it holds the audio ones next to the picture ones.
    //
    // This pair is the whole of what used to be missing. Selecting on media type
    // alone could name two of the six buckets and no more, so the other four had
    // "" for a type and were drawn empty by construction.
    readonly property var sidebarBuckets: [
        { key: "presets", label: qsTr("Presets"), root: "Preset", mediaType: "" },
        { key: "lumetri-presets", label: qsTr("Lumetri Presets"), root: "Lumetri",
          mediaType: "" },
        { key: "audio", label: qsTr("Audio Effects"), root: "Audio",
          mediaType: "audio" },
        { key: "audio-transitions", label: qsTr("Audio Transitions"),
          root: "Transition", mediaType: "audio" },
        { key: "video", label: qsTr("Video Effects"), root: "Video",
          mediaType: "video" },
        { key: "video-transitions", label: qsTr("Video Transitions"),
          root: "Transition", mediaType: "video" }
    ]

    // The registry, the effect presets and the Lumetri looks in one list. The
    // three come from separate C++ catalogs because they are applied in three
    // different ways, but the browser only ever asks what a thing is called and
    // which bucket it goes in, so from here they are one set of entries. Every
    // preset carries a "kind" - that is what applyEffect() branches on.
    readonly property var allDefinitions: Backend.effectDefinitions.concat(
                                              Backend.effectPresets,
                                              Backend.lumetriPresets)

    // The rail's rows, as a plain array rather than a second ListModel: a row is
    // then an ordinary object instead of a set of roles whose names have to avoid
    // colliding with the ones a delegate is handed for free, and the delegate
    // below reads it through the same required-property form as the dropdown's
    // Repeater, which is the one arrangement this file has already proven works.
    property var sidebarRows: []

    // Set while a rebuild is picking the category itself. Choosing one writes
    // sidebarKey, and that write rebuilds the rows on its own; during a rebuild
    // that would be the second of two builds for one action, and the one that
    // ran first would be thrown away.
    property bool suspendRowRebuild: false

    function mediaForClip(clip) {
        if (!clip || !clip.mediaId)
            return null
        var media = Backend.mediaById(String(clip.mediaId))
        return media && media.id ? media : null
    }

    // Split out from compatible() so the grid can ask the question once per media
    // type instead of once per card: a row of tiles is handed the two answers and
    // each picks its own, which is one property read rather than a call per tile.
    function compatibleMediaType(mediaType) {
        if (!selectedClip || !selectedClip.id || selectedClip.kind === "subtitle")
            return false
        var selection = Backend.effectEditSelection
        return Backend.editableClipIds(selection.indexOf(selectedClip.id) >= 0
            ? selection : [selectedClip.id], mediaType).length > 0
    }

    function compatible(definition) {
        return compatibleMediaType(String(definition.mediaType))
    }

    function matches(definition) {
        if (filterText === "")
            return true
        var query = filterText.toLowerCase()
        return String(definition.name).toLowerCase().indexOf(query) >= 0
                || String(definition.category).toLowerCase().indexOf(query) >= 0
                || String(definition.description).toLowerCase().indexOf(query) >= 0
    }

    // Three things arrive here through one signal, because a card cannot tell you
    // which of the three it is drawing - the entry it was built from can.
    //
    // A plain effect is one call. An effect preset is a short list of effects with
    // their parameters already set, applied in order; each step is an ordinary
    // addClipEffect, so the stack it leaves behind is one the user could have
    // built by hand and can take apart the same way. A Lumetri look is not on the
    // effect stack at all - it is the clip's own colour settings, so it goes
    // through applyClipColorPreset, which writes the whole grade as one undo step.
    function applyEffect(definition) {
        if (!compatible(definition))
            return
        var kind = String(definition.kind || "effect")
        if (kind === "lumetri")
            Backend.applyEffectSelectionCommand(selectedClip.id, "colorPreset", {settings: definition.settings})
        else if (kind === "preset")
            Backend.applyEffectSelectionCommand(selectedClip.id, "preset", {steps: definition.steps})
        else
            Backend.applyEffectSelectionCommand(selectedClip.id, "add", {effectId: definition.id})
    }

    function isExpanded(key) {
        return Boolean(expandedFolders[key])
    }

    // A key the user has never touched and a key they have closed are not the same
    // state, and isExpanded() cannot tell them apart. The sidebar's categories
    // start open, so it needs the difference: without this every one of them would
    // be shut on arrival and the pane would open on a column of folder rows with
    // nothing under them.
    function expandedIn(key, fallback) {
        return expandedFolders[key] === undefined ? fallback
                                                  : Boolean(expandedFolders[key])
    }

    // The state to write is passed in rather than flipped out of the map. The
    // sidebar's categories are open before the map holds anything for them, and
    // !undefined is true: flipping it there would have written the state the row
    // was already in, and the first click on a category would have done nothing
    // visible at all.
    function setFolderExpanded(key, expanded) {
        var next = Object.assign({}, expandedFolders)
        next[key] = expanded
        expandedFolders = next
        rebuildRows(root.viewMode)
    }

    function setViewMode(mode) {
        if ((mode !== "folder" && mode !== "sidebar") || root.viewMode === mode)
            return
        root.viewMode = mode
        // Written through to the settings rather than kept as session state: a
        // layout the user picked and then found reset on the next launch reads as
        // the button having failed. applyAppSettings starts from what is already
        // saved, so nothing else in the file moves.
        var next = Object.assign({}, Backend.appSettings)
        next.effectsBrowserView = mode
        Backend.applyAppSettings(next)
    }

    function bucketFor(key) {
        for (var i = 0; i < root.sidebarBuckets.length; ++i) {
            if (root.sidebarBuckets[i].key === key)
                return root.sidebarBuckets[i]
        }
        return null
    }

    // The one test every list below is built on: does this entry belong in this
    // bucket, and does it survive the search box.
    function inBucket(definition, bucket) {
        if (!bucket)
            return false
        var parts = String(definition.category).split(" / ")
        if (parts[0] !== bucket.root)
            return false
        if (bucket.mediaType !== "" && definition.mediaType !== bucket.mediaType)
            return false
        return matches(definition)
    }

    function definitionsFor(bucket, categoryName) {
        var result = []
        for (var i = 0; i < root.allDefinitions.length; ++i) {
            var definition = root.allDefinitions[i]
            if (!inBucket(definition, bucket))
                continue
            var parts = String(definition.category).split(" / ")
            var name = parts.length > 1 ? parts[1] : "Other"
            if (name === categoryName)
                result.push(definition)
        }
        return result
    }

    function categoryNames(bucket) {
        var names = []
        for (var i = 0; i < root.allDefinitions.length; ++i) {
            var definition = root.allDefinitions[i]
            if (!inBucket(definition, bucket))
                continue
            var parts = String(definition.category).split(" / ")
            var name = parts.length > 1 ? parts[1] : "Other"
            if (names.indexOf(name) < 0)
                names.push(name)
        }
        names.sort()
        return names
    }

    function appendFolder(name, key, depth, expanded) {
        browserModel.append({
            rowData: {
                type: "folder", name: name, key: key,
                depth: depth, expanded: expanded
            }
        })
    }

    function appendEffect(definition, depth) {
        browserModel.append({
            rowData: { type: "effect", definition: definition, depth: depth }
        })
    }

    // A whole category's cards as one row. The definitions are not put in the
    // model with it - a ListModel turns a nested array into a model of its own and
    // the delegate would be handed roles instead of entries - so the row carries
    // the category name and the delegate asks for the list by name.
    function appendGrid(categoryName) {
        browserModel.append({
            rowData: { type: "grid", category: categoryName, depth: 0 }
        })
    }

    // What a grid row draws, looked up when the delegate is built. Sidebar mode is
    // the only mode with grid rows in it, so the bucket is the selected one.
    function gridDefinitions(categoryName) {
        return definitionsFor(bucketFor(root.sidebarKey), categoryName)
    }

    function appendEffectTree(label, key) {
        var bucket = bucketFor(key)
        var searching = filterText !== ""
        var categories = categoryNames(bucket)
        if (categories.length === 0 && searching)
            return
        appendFolder(label, key, 0, isExpanded(key) || searching)
        if (!isExpanded(key) && !searching)
            return
        for (var i = 0; i < categories.length; ++i) {
            var category = categories[i]
            var categoryKey = key + "/" + category
            var definitions = definitionsFor(bucket, category)
            appendFolder(category, categoryKey, 1,
                         isExpanded(categoryKey) || searching)
            if (!isExpanded(categoryKey) && !searching)
                continue
            for (var j = 0; j < definitions.length; ++j)
                appendEffect(definitions[j], 2)
        }
    }

    function countFor(bucket) {
        var total = 0
        for (var i = 0; i < root.allDefinitions.length; ++i) {
            if (inBucket(root.allDefinitions[i], bucket))
                ++total
        }
        return total
    }

    // Six rows, always the same six, each with what is actually in it beside the
    // name. No bucket is empty any more, but the count is still hidden at nought
    // rather than drawn: a search that matches nothing in a bucket should leave the
    // name plain, not label it "0".
    function rebuildSidebar() {
        var rows = []
        for (var i = 0; i < root.sidebarBuckets.length; ++i) {
            var bucket = root.sidebarBuckets[i]
            rows.push({
                label: bucket.label,
                key: bucket.key,
                effectCount: countFor(bucket)
            })
        }
        root.sidebarRows = rows
        ModelGuard.note("effectsBrowser.sidebar", rows.length)
    }

    // A key saved by the build that keyed this rail by category - "video/Adjust" -
    // matches no bucket, and would leave the rail with nothing marked and the pane
    // empty. Video Effects is the fallback because it holds the most.
    function ensureSidebarSelection() {
        for (var i = 0; i < root.sidebarBuckets.length; ++i) {
            if (root.sidebarBuckets[i].key === root.sidebarKey)
                return
        }
        root.sidebarKey = "video"
    }

    // The categories the rail used to list are drawn here instead, as the pane's
    // own headings with their effects under them, open unless the user closed one.
    // Keyed apart from the tree's own category rows: the two layouts disagree about
    // what a category does when it has never been touched, and one map cannot hold
    // both answers for one key.
    //
    // One row per category rather than one per effect - the row is the grid, and it
    // wraps its cards across however wide the pane is.
    function appendSidebarEffects() {
        var bucket = bucketFor(root.sidebarKey)
        if (!bucket)
            return
        var searching = filterText !== ""
        var categories = categoryNames(bucket)
        for (var i = 0; i < categories.length; ++i) {
            var category = categories[i]
            var categoryKey = "sidebar/" + bucket.key + "/" + category
            var open = expandedIn(categoryKey, true) || searching
            appendFolder(category, categoryKey, 0, open)
            if (!open)
                continue
            appendGrid(category)
        }
    }

    // Every bucket is a real folder now. The four that were drawn as bare rows with
    // nothing under them - Presets, Lumetri Presets and the two transition sets -
    // open like the other two, because there is finally something inside them.
    function rebuildTree() {
        appendEffectTree(qsTr("Presets"), "presets")
        appendEffectTree(qsTr("Lumetri Presets"), "lumetri-presets")
        appendEffectTree(qsTr("Audio Effects"), "audio")
        appendEffectTree(qsTr("Audio Transitions"), "audio-transitions")
        appendEffectTree(qsTr("Video Effects"), "video")
        appendEffectTree(qsTr("Video Transitions"), "video-transitions")
    }

    // One list for both layouts: the rows in it differ, the delegate that draws
    // them and the guard that counts them do not.
    //
    // The layout is passed in rather than read back off a property. This runs
    // from onViewModeChanged, and a property derived from viewMode is not
    // guaranteed to have caught up with the write that emitted that signal -
    // which is what left the tree showing a flat list of the sidebar's effects:
    // the row build had already run with the layout that was being left behind.
    function rebuildRows(mode) {
        browserModel.clear()
        if (mode === "sidebar")
            appendSidebarEffects()
        else
            rebuildTree()
        // clear() above is what keeps this bounded. Recorded so that if it ever
        // stops being called, the growing count says so instead of the memory
        // graph saying it an hour later.
        ModelGuard.note("effectsBrowser.rows", browserModel.count)
    }

    // Every change that alters what the panel shows comes through here, and this
    // always ends in a row build - no branch of it can return before that, which
    // is the other half of what went wrong: a rebuild that stopped early once the
    // category had been chosen left whichever rows were already in the list.
    function refresh(mode) {
        if (mode === "sidebar") {
            rebuildSidebar()
            root.suspendRowRebuild = true
            ensureSidebarSelection()
            root.suspendRowRebuild = false
        } else {
            // An invisible ListView still holds a delegate per row, so the rail
            // is emptied rather than left standing behind the tree.
            root.sidebarRows = []
        }
        rebuildRows(mode)
    }

    onFilterTextChanged: root.refresh(root.viewMode)
    onViewModeChanged: root.refresh(root.viewMode)
    onSidebarKeyChanged: {
        if (!root.suspendRowRebuild)
            root.rebuildRows(root.viewMode)
    }
    Component.onCompleted: root.refresh(root.viewMode)

    ListModel { id: browserModel }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Theme.bgPanel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: root.selectedClip && root.selectedClip.id
                          ? String(root.selectedClip.name || qsTr("Selected clip"))
                          : qsTr("No clip selected")
                    color: root.selectedClip && root.selectedClip.id
                           ? Theme.textSecondary : Theme.textMuted
                    font.pixelSize: Theme.fsXs
                    elide: Text.ElideMiddle
                }
                Text {
                    text: root.selectedClip && root.selectedClip.track
                          ? String(root.selectedClip.track) : ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsXs
                }

                // The layout switch. A dropdown rather than a bare toggle so that
                // the two arrangements can be named: an icon on its own does not
                // say what it would change to, and this row is the only place a
                // user who has never seen the sidebar can find it.
                Rectangle {
                    id: layoutButton
                    Layout.preferredWidth: layoutRow.implicitWidth + 12
                    Layout.preferredHeight: 22
                    radius: Theme.radiusSm
                    color: layoutHover.hovered || layoutPopup.opened
                           ? Theme.ecRowHover : "transparent"

                    RowLayout {
                        id: layoutRow
                        anchors.centerIn: parent
                        spacing: 4

                        Image {
                            Layout.alignment: Qt.AlignVCenter
                            width: 13
                            height: 13
                            sourceSize.width: 13
                            sourceSize.height: 13
                            source: "../../assets/icons/"
                                    + (root.viewMode === "sidebar" ? "menu" : "folder")
                                    + ".svg"
                            opacity: 0.82
                        }
                        Image {
                            Layout.alignment: Qt.AlignVCenter
                            width: 9
                            height: 9
                            sourceSize.width: 9
                            sourceSize.height: 9
                            source: "../../assets/icons/chevron-down.svg"
                            opacity: 0.7
                        }
                    }

                    HoverHandler {
                        id: layoutHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: layoutPopup.opened ? layoutPopup.close()
                                                     : layoutPopup.open()
                    }
                    ToolTip.visible: layoutHover.hovered && !layoutPopup.opened
                    ToolTip.text: root.viewMode === "sidebar"
                                  ? qsTr("Effects layout: sidebar")
                                  : qsTr("Effects layout: folder tree")

                    Popup {
                        id: layoutPopup
                        y: layoutButton.height + 4
                        x: layoutButton.width - width
                        width: 156
                        padding: 4
                        background: Rectangle {
                            color: Theme.bgPanel
                            border.color: Theme.border
                            radius: Theme.radiusSm
                        }
                        contentItem: ColumnLayout {
                            spacing: 0
                            Repeater {
                                model: [{ mode: "sidebar", label: qsTr("Sidebar"),
                                          icon: "menu" },
                                        { mode: "folder", label: qsTr("Folder tree"),
                                          icon: "folder" }]
                                delegate: Rectangle {
                                    id: option
                                    required property int index
                                    required property var modelData
                                    readonly property bool current:
                                        root.viewMode === option.modelData.mode
                                    Layout.fillWidth: true
                                    implicitHeight: 28
                                    radius: Theme.radiusSm
                                    color: optionHover.hovered ? Theme.ecRowHover
                                                              : "transparent"

                                    Image {
                                        id: optionIcon
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 13
                                        height: 13
                                        sourceSize.width: 13
                                        sourceSize.height: 13
                                        source: "../../assets/icons/"
                                                + option.modelData.icon + ".svg"
                                        opacity: 0.8
                                    }
                                    Text {
                                        anchors.left: optionIcon.right
                                        anchors.leftMargin: 8
                                        anchors.right: optionCheck.left
                                        anchors.rightMargin: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: option.modelData.label
                                        color: option.current ? Theme.accent
                                                              : Theme.textPrimary
                                        font.pixelSize: Theme.fsMd
                                        elide: Text.ElideRight
                                    }
                                    Image {
                                        id: optionCheck
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 12
                                        height: 12
                                        sourceSize.width: 12
                                        sourceSize.height: 12
                                        source: "../../assets/icons/check.svg"
                                        visible: option.current
                                    }

                                    HoverHandler {
                                        id: optionHover
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                    TapHandler {
                                        onTapped: {
                                            root.setViewMode(option.modelData.mode)
                                            layoutPopup.close()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Category rail. Left out of the layout in folder mode rather than
            // sized down to nothing - a RowLayout skips an invisible item, so the
            // tree gets the whole width back without either side having to know
            // which layout is on.
            Rectangle {
                visible: root.viewMode === "sidebar"
                // Narrow, and narrower still in a narrow panel: the cards beside
                // it carry a 118 px preview, and a rail that will not give way is
                // what would clip them. Wider than the per-category rail was,
                // because "Lumetri Presets" is a longer name than "Blur & Sharpen"
                // and this column is now the whole of the navigation.
                Layout.preferredWidth: Math.round(
                    Math.min(150, Math.max(96, root.width * 0.32)))
                Layout.fillHeight: true
                color: Theme.bgSidebar

                ListView {
                    anchors.fill: parent
                    anchors.rightMargin: 1
                    clip: true
                    model: root.sidebarRows
                    // Six rows will not always fit a short panel, so the column
                    // still scrolls - but it stops where the list stops. The
                    // rubber-band snap back is the one piece of motion a rail this
                    // plain would still have shown.
                    boundsBehavior: Flickable.StopAtBounds

                    // Inline, and reading modelData rather than model roles. The
                    // first version of this rail put the same delegate in a
                    // Component of its own further up the file; under
                    // ComponentBehavior: Bound its required model property was
                    // then never filled, so every label resolved to nothing and
                    // the rail drew as an empty column the width it was asked for.
                    delegate: Rectangle {
                        id: railRow
                        required property var modelData
                        readonly property bool current:
                            String(railRow.modelData.key) === root.sidebarKey

                        width: ListView.view.width
                        implicitHeight: 30
                        height: implicitHeight
                        // Flat fills, no tint over the accent, and no Behavior on
                        // any of it - the colour is whatever it is on the frame the
                        // row is pressed. The open row is the lighter grey, the one
                        // the effect controls already use for a band, so the rail
                        // reads as part of the window instead of as a highlight.
                        color: railRow.current
                               ? Theme.ecBand
                               : (railHover.hovered ? Theme.ecRowHover
                                                    : "transparent")

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.right: railCount.left
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: String(railRow.modelData.label)
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsSm
                            font.weight: railRow.current ? Font.DemiBold
                                                         : Font.Normal
                            elide: Text.ElideRight
                        }

                        Text {
                            id: railCount
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            // Hidden rather than drawn as a nought: a bucket that
                            // is empty because nothing is registered in it yet
                            // should not look like one that failed to load.
                            visible: Number(railRow.modelData.effectCount) > 0
                            text: String(railRow.modelData.effectCount)
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsXs
                        }

                        HoverHandler {
                            id: railHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: root.sidebarKey = String(railRow.modelData.key)
                        }
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: Theme.border
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    anchors.fill: parent
                    clip: true
                    model: browserModel
                    // A grid row can be taller than the pane in a narrow panel, and
                    // the view has to be able to stop between two of them rather
                    // than snapping to a row boundary it cannot reach.
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: EffectBrowserCell {
                        id: paneRow
                        required property var model
                        width: ListView.view.width
                        rowData: model.rowData
                        // Looked up here rather than stored in the model: the row
                        // knows which category it is, and the list of cards in it is
                        // a plain array, which a ListModel would turn into a model.
                        gridDefinitions: paneRow.rowData.type === "grid"
                                         ? root.gridDefinitions(
                                               String(paneRow.rowData.category))
                                         : []
                        canApply: rowData.type === "effect"
                                  && root.compatible(rowData.definition)
                        canApplyVideo: root.compatibleMediaType("video")
                        canApplyAudio: root.compatibleMediaType("audio")
                        clipId: root.selectedClip && root.selectedClip.id
                                ? String(root.selectedClip.id) : ""
                        basePreviewUrl: root.selectedMedia
                                        ? String(root.selectedMedia.thumbnailUrl || "")
                                        : ""
                        // The row it was clicked on is what knows whether it is
                        // open, so that is what decides which way it goes.
                        onFolderToggled: key => root.setFolderExpanded(
                                             key, !paneRow.rowData.expanded)
                        onEffectRequested: definition => root.applyEffect(definition)
                    }
                }

                // Only reachable through the search box now that all six buckets
                // hold something.
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 32
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: browserModel.count === 0
                    text: root.filterText !== "" ? qsTr("No effect matches this search")
                                                 : qsTr("Nothing in here yet")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsXs
                }
            }
        }
    }
}
