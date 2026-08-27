pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
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

    function mediaForClip(clip) {
        if (!clip || !clip.mediaId)
            return null
        for (var i = 0; i < Backend.media.length; ++i) {
            if (Backend.media[i].id === clip.mediaId)
                return Backend.media[i]
        }
        return null
    }

    function compatible(definition) {
        if (!selectedClip || !selectedClip.id || selectedClip.kind === "subtitle")
            return false
        if (definition.mediaType === "video")
            return selectedClip.kind === "video" || selectedClip.kind === "image"
        var media = mediaForClip(selectedClip)
        return media !== null && Number(media.channels || 0) > 0
    }

    function matches(definition) {
        if (filterText === "")
            return true
        var query = filterText.toLowerCase()
        return String(definition.name).toLowerCase().indexOf(query) >= 0
                || String(definition.category).toLowerCase().indexOf(query) >= 0
                || String(definition.description).toLowerCase().indexOf(query) >= 0
    }

    function applyEffect(definition) {
        if (!compatible(definition))
            return
        Backend.addClipEffect(selectedClip.id, definition.id)
    }

    function isExpanded(key) {
        return Boolean(expandedFolders[key])
    }

    function toggleFolder(key) {
        var next = Object.assign({}, expandedFolders)
        next[key] = !Boolean(next[key])
        expandedFolders = next
        rebuildRows()
    }

    function definitionsFor(mediaType, categoryName) {
        var result = []
        for (var i = 0; i < Backend.effectDefinitions.length; ++i) {
            var definition = Backend.effectDefinitions[i]
            var parts = String(definition.category).split(" / ")
            if (definition.mediaType === mediaType
                    && parts.length > 1 && parts[1] === categoryName
                    && matches(definition))
                result.push(definition)
        }
        return result
    }

    function categoryNames(mediaType) {
        var names = []
        for (var i = 0; i < Backend.effectDefinitions.length; ++i) {
            var definition = Backend.effectDefinitions[i]
            if (definition.mediaType !== mediaType || !matches(definition))
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

    function appendEffectTree(label, key, mediaType) {
        var searching = filterText !== ""
        var categories = categoryNames(mediaType)
        if (categories.length === 0 && searching)
            return
        appendFolder(label, key, 0, isExpanded(key) || searching)
        if (!isExpanded(key) && !searching)
            return
        for (var i = 0; i < categories.length; ++i) {
            var category = categories[i]
            var categoryKey = key + "/" + category
            var definitions = definitionsFor(mediaType, category)
            appendFolder(category, categoryKey, 1,
                         isExpanded(categoryKey) || searching)
            if (!isExpanded(categoryKey) && !searching)
                continue
            for (var j = 0; j < definitions.length; ++j)
                appendEffect(definitions[j], 2)
        }
    }

    function rebuildRows() {
        browserModel.clear()
        if (filterText === "") {
            appendFolder("Presets", "presets", 0, isExpanded("presets"))
            appendFolder("Lumetri Presets", "lumetri-presets", 0,
                         isExpanded("lumetri-presets"))
        }
        appendEffectTree("Audio Effects", "audio", "audio")
        if (filterText === "")
            appendFolder("Audio Transitions", "audio-transitions", 0,
                         isExpanded("audio-transitions"))
        appendEffectTree("Video Effects", "video", "video")
        if (filterText === "")
            appendFolder("Video Transitions", "video-transitions", 0,
                         isExpanded("video-transitions"))
    }

    onFilterTextChanged: rebuildRows()
    Component.onCompleted: rebuildRows()

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
                          ? String(root.selectedClip.name || "Selected clip")
                          : "No clip selected"
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
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: browserModel

            delegate: EffectBrowserRow {
                required property var model
                width: ListView.view.width
                rowData: model.rowData
                canApply: rowData.type === "effect"
                          && root.compatible(rowData.definition)
                clipId: root.selectedClip && root.selectedClip.id
                        ? String(root.selectedClip.id) : ""
                basePreviewUrl: root.selectedMedia
                                ? String(root.selectedMedia.thumbnailUrl || "")
                                : ""
                onFolderToggled: key => root.toggleFolder(key)
                onEffectRequested: definition => root.applyEffect(definition)
            }
        }
    }
}
