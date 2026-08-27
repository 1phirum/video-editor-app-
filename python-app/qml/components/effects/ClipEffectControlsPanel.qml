pragma ComponentBehavior: Bound
// qmllint disable

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

// Effect Controls, laid out the way Premiere Pro does it: a fixed row grid on
// the left and keyframe lanes on the right, both driven by one flat row model,
// so lane N always lines up with row N and the separators run straight across.
Item {
    id: root

    readonly property var clipData: Backend.selectedClip
    readonly property var effects: root.clipData && root.clipData.effects
                                   ? root.clipData.effects : ({})
    readonly property var mediaData: root.mediaForClip(root.clipData)
    readonly property bool hasClip: Boolean(root.clipData && root.clipData.id)
    readonly property string clipId:
        root.hasClip ? String(root.clipData.id) : ""
    readonly property bool isSubtitle:
        root.hasClip && root.clipData.kind === "subtitle"
    readonly property bool hasVisual: root.hasClip && !root.isSubtitle
                                      && root.clipData.kind !== "audio"
    readonly property int channelCount:
        Number(root.mediaData ? (root.mediaData.channels || 0) : 0)
    readonly property bool hasAudio: root.channelCount > 0
    readonly property var effectStack: root.clipData && root.clipData.effectStack
                                       ? root.clipData.effectStack : []
    readonly property string clipLabel: root.hasClip
        ? String(root.clipData.name
                 || (root.mediaData ? root.mediaData.name : "Clip")) : ""
    readonly property real clipStartMs:
        root.hasClip ? Number(root.clipData.startMs || 0) : 0
    readonly property real clipSpanMs: root.hasClip
        ? Math.max(1, Number(root.clipData.durationMs || 1)) : 1

    // Split between the parameter tree and the keyframe lanes.
    property real dividerX: 336
    // Flat row model shared by both halves of the panel.
    property var rows: []
    property var expandedMap: ({})
    // One live gesture at a time: the row being dragged reads its value from
    // here so the backend (and the undo stack) only sees the finished edit.
    property string draftKey: ""
    property real draftValue: 0

    function mediaForClip(clip) {
        if (!clip || !clip.mediaId)
            return null
        for (var i = 0; i < Backend.media.length; ++i) {
            if (Backend.media[i].id === clip.mediaId)
                return Backend.media[i]
        }
        return null
    }

    function settingValue(key, fallback) {
        var current = root.effects[key]
        return current === undefined || current === null ? fallback : current
    }

    function definitionForId(definitionId) {
        for (var i = 0; i < Backend.effectDefinitions.length; ++i) {
            if (Backend.effectDefinitions[i].id === definitionId)
                return Backend.effectDefinitions[i]
        }
        return ({ id: definitionId, name: "Missing effect",
                  mediaType: "video", parameters: [] })
    }

    // Row value: the live draft while a gesture is running, otherwise the
    // stored setting (or applied-effect parameter), otherwise the default.
    function valueFor(row, effectMap, stack, gestureKey, gestureValue) {
        if (row.key === gestureKey)
            return gestureValue
        if (String(row.instanceId || "") !== "") {
            for (var i = 0; i < stack.length; ++i) {
                if (String(stack[i].id) !== String(row.instanceId))
                    continue
                var params = stack[i].parameters || ({})
                var stored = params[row.id]
                return stored === undefined || stored === null ? row.def : stored
            }
            return row.def
        }
        var value = effectMap[row.id]
        return value === undefined || value === null ? row.def : value
    }

    function isExpanded(key, fallback) {
        var state = root.expandedMap[key]
        return state === undefined ? fallback === true : state === true
    }

    function toggleExpanded(key, fallback) {
        var next = {}
        for (var existing in root.expandedMap)
            next[existing] = root.expandedMap[existing]
        next[key] = !root.isExpanded(key, fallback)
        root.expandedMap = next
    }

    function timecode(milliseconds) {
        var ms = Math.max(0, Number(milliseconds))
        var frames = Math.floor((ms % 1000) / 40)
        var total = Math.floor(ms / 1000)
        function pad(value) { return value < 10 ? "0" + value : String(value) }
        return pad(Math.floor(total / 3600)) + ":" + pad(Math.floor(total / 60) % 60)
               + ":" + pad(total % 60) + ":" + pad(frames)
    }

    // ---- Row constructors -----------------------------------------------
    function bandRow(id, label) {
        return { kind: "band", key: "band:" + id, id: id, label: label,
                 expandable: true, openByDefault: true, resettable: false }
    }
    function groupRow(id, label, openByDefault) {
        return { kind: "group", key: "grp:" + id, id: id, label: label,
                 instanceId: "", resetKeys: [], expandable: true,
                 openByDefault: openByDefault === true }
    }
    function paramRow(id, label, def, from, to, decimals, unit) {
        return { kind: "param", key: "p::" + id, id: id, instanceId: "",
                 label: label, def: def, from: from, to: to,
                 decimals: decimals, unit: unit, kf: true, expandable: true }
    }
    function boolRow(id, label, def) {
        return { kind: "bool", key: "b::" + id, id: id, instanceId: "",
                 label: label, def: def }
    }
    function selectRow(id, label, def, options) {
        return { kind: "select", key: "s::" + id, id: id, instanceId: "",
                 label: label, def: def, options: options }
    }
    function masksRow(id, label, def) {
        return { kind: "masks", key: "m::" + id, id: id, instanceId: "",
                 label: label, def: def }
    }
    function noteRow(key, label) {
        return { kind: "note", key: "n:" + key, id: "", instanceId: "",
                 label: label, resettable: false }
    }
    function withDisabled(row, disabled) {
        row.disabled = disabled === true
        return row
    }
    // The revealed slider shares its parameter's key so a drag on either one
    // feeds the same draft.
    function sliderRowFor(row) {
        return { kind: "slider", key: row.key, id: row.id,
                 instanceId: row.instanceId || "", label: row.label,
                 def: row.def, from: row.from, to: row.to,
                 decimals: row.decimals, kf: row.kf === true,
                 disabled: row.disabled === true, resettable: false }
    }

    function instanceGroupRow(instance, definition, index, count) {
        return { kind: "group", key: "grp:" + instance.id,
                 id: String(definition.id || ""), instanceId: String(instance.id),
                 label: String(definition.name || "Effect"),
                 expandable: true, openByDefault: true, resetKeys: [],
                 bypassed: instance.enabled === false,
                 canMoveUp: index > 0, canMoveDown: index + 1 < count }
    }

    function instanceParamRow(instance, parameter) {
        var key = "p:" + instance.id + ":" + parameter.id
        var type = String(parameter.type || "number")
        var label = String(parameter.name || parameter.id || "")
        var off = instance.enabled === false
        if (type === "mask") {
            return { kind: "blurmask", key: key, id: String(parameter.id),
                     instanceId: String(instance.id), label: label,
                     resettable: false, disabled: off }
        }
        if (type === "bool") {
            return { kind: "bool", key: key, id: String(parameter.id),
                     instanceId: String(instance.id), label: label,
                     def: parameter.default, disabled: off }
        }
        return { kind: "param", key: key, id: String(parameter.id),
                 instanceId: String(instance.id), label: label,
                 def: Number(parameter.default || 0),
                 from: Number(parameter.minimum || 0),
                 to: Number(parameter.maximum === undefined ? 100 : parameter.maximum),
                 decimals: Number(parameter.decimals || 0),
                 unit: String(parameter.unit || ""),
                 kf: false, expandable: true, disabled: off }
    }

    function blendModeOptions() {
        var names = [["normal", "Normal"], ["dissolve", "Dissolve"],
                     ["darken", "Darken"], ["multiply", "Multiply"],
                     ["colorBurn", "Color Burn"], ["linearBurn", "Linear Burn"],
                     ["darkerColor", "Darker Color"], ["lighten", "Lighten"],
                     ["screen", "Screen"], ["colorDodge", "Color Dodge"],
                     ["linearDodge", "Linear Dodge (Add)"],
                     ["lighterColor", "Lighter Color"], ["overlay", "Overlay"],
                     ["softLight", "Soft Light"], ["hardLight", "Hard Light"],
                     ["vividLight", "Vivid Light"], ["linearLight", "Linear Light"],
                     ["pinLight", "Pin Light"], ["hardMix", "Hard Mix"],
                     ["difference", "Difference"], ["exclusion", "Exclusion"],
                     ["subtract", "Subtract"], ["divide", "Divide"],
                     ["hue", "Hue"], ["saturation", "Saturation"],
                     ["color", "Color"], ["luminosity", "Luminosity"]]
        var options = []
        for (var i = 0; i < names.length; ++i)
            options.push({ value: names[i][0], label: names[i][1] })
        return options
    }

    function stackSignature(stack) {
        var parts = []
        for (var i = 0; i < stack.length; ++i) {
            parts.push(String(stack[i].id) + ":" + String(stack[i].definitionId)
                       + ":" + (stack[i].enabled === false ? "0" : "1"))
        }
        return parts.join(",")
    }

    // Everything that changes the shape of the tree — not the values in it.
    readonly property string structureKey:
        root.clipId + "|" + root.hasVisual + "|" + root.hasAudio + "|"
        + root.channelCount + "|"
        + Boolean(root.settingValue("uniformScale", true)) + "|"
        + root.stackSignature(root.effectStack) + "|"
        + JSON.stringify(root.expandedMap)

    // Rebuilding the row list re-creates every EffectTreeRow on the left and
    // every keyframe lane on the right - a few hundred QML objects for a clip
    // with effects on it. It used to run inside emit selectionChanged(), which
    // put all of it inside the timeline's drop handler: the GUI thread was
    // measured unresponsive for 401-458 ms there, and deactivating this panel
    // made that stall disappear entirely.
    //
    // One turn of deferral takes it out of the gesture, and restarting the timer
    // collapses a burst - a drop that selects a clip and then moves the playhead
    // is one rebuild, not three. Nothing downstream can tell the difference,
    // because the rows are rebuilt from properties that are already correct.
    Timer {
        id: rebuildTimer
        interval: 0
        repeat: false
        onTriggered: {
            // Names this turn for the watchdog. Without it the rebuild reports as
            // "no marked scope", which is how it stayed unattributed for so long.
            Backend.markGuiScope("ClipEffectControlsPanel.rebuildRows")
            root.rebuildRows()
        }
    }

    onStructureKeyChanged: rebuildTimer.restart()
    // The first build stays synchronous: this panel is incubated asynchronously
    // by its Loader, so it is already off the critical path, and doing it here
    // avoids showing an empty grid for a frame on open.
    Component.onCompleted: root.rebuildRows()

    function rebuildRows() {
        var list = []
        if (!root.hasClip || root.isSubtitle) {
            root.rows = list
            return
        }
        var uniform = Boolean(root.settingValue("uniformScale", true))
        var stack = root.effectStack

        // A group plus its parameters. Collapsed groups still carry the reset
        // keys so the row's undo button works without expanding it.
        function pushSection(group, params) {
            var keys = []
            for (var k = 0; k < params.length; ++k) {
                var candidate = params[k]
                if (candidate.kind === "param" || candidate.kind === "bool"
                        || candidate.kind === "select" || candidate.kind === "masks")
                    keys.push({ id: candidate.id, def: candidate.def })
            }
            if (String(group.instanceId || "") === "")
                group.resetKeys = keys
            list.push(group)
            if (!root.isExpanded(group.key, group.openByDefault))
                return
            for (var i = 0; i < params.length; ++i) {
                var row = params[i]
                list.push(row)
                if (row.kind === "param" && root.isExpanded(row.key, false))
                    list.push(root.sliderRowFor(row))
            }
        }

        function pushInstances(mediaType) {
            for (var i = 0; i < stack.length; ++i) {
                var instance = stack[i]
                var definition = root.definitionForId(instance.definitionId)
                var kind = String(definition.mediaType || "video") === "audio"
                           ? "audio" : "video"
                if (kind !== mediaType)
                    continue
                var declared = definition.parameters || []
                var params = []
                for (var p = 0; p < declared.length; ++p)
                    params.push(root.instanceParamRow(instance, declared[p]))
                if (params.length === 0)
                    params.push(root.noteRow("empty:" + instance.id, "No parameters"))
                pushSection(root.instanceGroupRow(instance, definition, i,
                                                  stack.length), params)
            }
        }

        if (root.hasVisual) {
            var videoBand = root.bandRow("video", "Video")
            list.push(videoBand)
            if (root.isExpanded(videoBand.key, true)) {
                pushSection(root.groupRow("motion", "Motion", true), [
                    root.paramRow("positionX", "Position X", 0, -100, 100, 1, ""),
                    root.paramRow("positionY", "Position Y", 0, -100, 100, 1, ""),
                    root.paramRow("scale", "Scale", 100, 10, 400, 1, "%"),
                    root.withDisabled(root.paramRow("scaleWidth", "Scale Width",
                                                    100, 10, 600, 1, "%"), uniform),
                    root.withDisabled(root.paramRow("scaleHeight", "Scale Height",
                                                    100, 10, 600, 1, "%"), uniform),
                    root.boolRow("uniformScale", "Uniform Scale", true),
                    root.paramRow("rotation", "Rotation", 0, -180, 180, 1, "°"),
                    root.paramRow("anchorPointX", "Anchor Point X", 0.5, 0, 1, 2, ""),
                    root.paramRow("anchorPointY", "Anchor Point Y", 0.5, 0, 1, 2, ""),
                    root.paramRow("antiFlicker", "Anti-flicker Filter", 0, 0, 1, 2, ""),
                    root.boolRow("horizontalFlip", "Flip Horizontal", false),
                    root.boolRow("verticalFlip", "Flip Vertical", false)
                ])
                pushSection(root.groupRow("opacity", "Opacity", true), [
                    root.paramRow("opacity", "Opacity", 100, 0, 100, 1, "%"),
                    root.masksRow("maskType", "Mask", "none"),
                    root.paramRow("maskFeather", "Mask Feather", 0, 0, 250, 0, "px"),
                    root.paramRow("maskOpacity", "Mask Opacity", 100, 0, 100, 1, "%"),
                    root.paramRow("maskExpansion", "Mask Expansion", 0, -250, 250, 0, "px"),
                    root.boolRow("maskInverted", "Inverted", false),
                    root.selectRow("blendMode", "Blend Mode", "normal",
                                   root.blendModeOptions())
                ])
                pushSection(root.groupRow("crop", "Crop", false), [
                    root.paramRow("cropLeft", "Left", 0, 0, 49, 1, "%"),
                    root.paramRow("cropRight", "Right", 0, 0, 49, 1, "%"),
                    root.paramRow("cropTop", "Top", 0, 0, 49, 1, "%"),
                    root.paramRow("cropBottom", "Bottom", 0, 0, 49, 1, "%")
                ])
                pushSection(root.groupRow("blur", "Gaussian Blur", false), [
                    root.paramRow("blur", "Blurriness", 0, 0, 100, 1, "")
                ])
                pushSection(root.groupRow("time", "Time Remapping", false), [
                    root.paramRow("speed", "Speed", 100, 1, 10000, 1, "%")
                ])
                pushInstances("video")
            }
        }

        if (root.hasAudio) {
            var audioBand = root.bandRow("audio", "Audio")
            list.push(audioBand)
            if (root.isExpanded(audioBand.key, true)) {
                pushSection(root.groupRow("volume", "Volume", true), [
                    root.paramRow("volumeDb", "Level", 0, -60, 12, 1, "dB"),
                    root.boolRow("volumeBypass", "Bypass", false)
                ])
                pushSection(root.groupRow("channelVolume", "Channel Volume", false), [
                    root.paramRow("channelVolumeLeft", "Left", 0, -60, 15, 1, "dB"),
                    root.paramRow("channelVolumeRight", "Right", 0, -60, 15, 1, "dB")
                ])
                pushSection(root.groupRow("panner", "Panner", false), [
                    root.paramRow("balance", "Balance", 0, -100, 100, 0, "%"),
                    root.paramRow("pan", "Pan", 0, -1, 1, 2, "")
                ])
                var cleanup = [
                    root.paramRow("noiseReduction", "Noise Reduction", 0, 0, 100, 0, "%"),
                    root.paramRow("highPassHz", "High Pass", 0, 0, 1000, 0, "Hz"),
                    root.paramRow("lowPassHz", "Low Pass", 0, 0, 22000, 0, "Hz"),
                    root.boolRow("compressor", "Compressor", false),
                    root.withDisabled(root.boolRow("vocalRemoval",
                                                   "Remove Vocals (AI)", false),
                                      root.channelCount < 2)
                ]
                if (root.channelCount < 2)
                    cleanup.push(root.noteRow("vocals", "Stereo media required"))
                pushSection(root.groupRow("cleanup", "Audio Cleanup", false), cleanup)
                pushInstances("audio")
            }
        }

        root.rows = list
    }

    Loader {
        anchors.fill: parent
        active: root.isSubtitle
        sourceComponent: SubtitleBlurControls {}
    }

    Text {
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 36)
        visible: !root.hasClip
        text: "Select a timeline clip or drop an effect onto it."
        color: Theme.textMuted
        font.pixelSize: Theme.fsSm
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }

    ColumnLayout {
        anchors.fill: parent
        visible: root.hasClip && !root.isSubtitle
        spacing: 0

        // ---- Source / sequence selector, as in Premiere's title bar ------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: Theme.bgPanel

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 9

                Text {
                    text: "Source • " + root.clipLabel
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    elide: Text.ElideRight
                }
                Item {
                    width: 1
                    height: 18
                    Rectangle {
                        anchors.centerIn: parent
                        width: 1
                        height: 12
                        color: "#4d4d4d"
                    }
                }
                Item {
                    width: sequenceLabel.implicitWidth
                    height: 18
                    Text {
                        id: sequenceLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: (Backend.sequenceName || "Sequence") + " • " + root.clipLabel
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsSm
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        color: Theme.accent
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

        // ---- Timecode ruler over the lanes ------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.rulerHeight

            Rectangle {
                anchors.fill: parent
                color: Theme.bgPanel
            }
            EffectTimeRuler {
                x: root.dividerX + 1
                width: Math.max(0, parent.width - root.dividerX - 1)
                height: parent.height
                startMs: root.clipStartMs
                spanMs: root.clipSpanMs
            }
            Rectangle {
                x: root.dividerX
                width: 1
                height: parent.height
                color: dividerHandle.pressed ? Theme.accent
                                               : (dividerHandle.containsMouse
                                                  ? Qt.lighter(Theme.accent, 1.15)
                                                  : "#1b1c1f")
            }
        }

        // ---- The grid: parameter rows left, keyframe lanes right ---------
        Item {
            id: gridHost
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: Theme.bgPanel
            }
            Rectangle {
                x: root.dividerX + 1
                width: Math.max(0, parent.width - root.dividerX - 1)
                height: parent.height
                color: Theme.bgPrimary
            }

            Flickable {
                id: grid
                anchors.fill: parent
                clip: true
                contentWidth: grid.width
                contentHeight: root.rows.length * Theme.ecRowHeight
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {
                    id: vbar
                    policy: ScrollBar.AsNeeded
                    width: 8
                    contentItem: Rectangle {
                        implicitWidth: 8
                        radius: 3
                        color: vbar.pressed ? Theme.textSecondary : "#5f5f5f"
                        opacity: vbar.active || vbar.hovered ? 0.9 : 0.4
                    }
                    background: Rectangle { color: "transparent" }
                }

                Column {
                    id: treeColumn
                    x: 0
                    width: root.dividerX
                    spacing: 0

                    Repeater {
                        model: root.rows
                        delegate: EffectTreeRow {
                            required property var modelData
                            width: treeColumn.width
                            rowData: modelData
                            clipId: root.clipId
                            currentValue: root.valueFor(modelData, root.effects,
                                                        root.effectStack,
                                                        root.draftKey,
                                                        root.draftValue)
                            expanded: root.isExpanded(modelData.key,
                                                      modelData.openByDefault === true)
                            onExpandRequested: root.toggleExpanded(
                                                   modelData.key,
                                                   modelData.openByDefault === true)
                            onDraftChanged: (value) => {
                                root.draftKey = modelData.key
                                root.draftValue = value
                            }
                            onDraftCleared: root.draftKey = ""
                        }
                    }
                }

                EffectKeyframeTimeline {
                    x: root.dividerX + 1
                    width: Math.max(0, grid.width - root.dividerX - 1)
                    height: Math.max(grid.height,
                                     root.rows.length * Theme.ecRowHeight)
                    rows: root.rows
                    clipId: root.clipId
                    clipLabel: root.clipLabel
                    startMs: root.clipStartMs
                    spanMs: root.clipSpanMs
                }
            }

            Rectangle {
                x: root.dividerX
                width: 1
                height: parent.height
                color: dividerHandle.pressed ? Theme.accent
                                               : (dividerHandle.containsMouse
                                                  ? Qt.lighter(Theme.accent, 1.15)
                                                  : "#1b1c1f")
            }
            MouseArea {
                id: dividerHandle
                x: root.dividerX - 3
                width: 7
                height: parent.height
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                property real pressX: 0
                onPressed: (mouse) => {
                    dividerHandle.pressX =
                            dividerHandle.mapToItem(gridHost, mouse.x, 0).x
                }
                onPositionChanged: (mouse) => {
                    if (!dividerHandle.pressed)
                        return
                    var here = dividerHandle.mapToItem(gridHost, mouse.x, 0).x
                    root.dividerX = Math.max(
                                250, Math.min(gridHost.width - 150,
                                              root.dividerX + here
                                              - dividerHandle.pressX))
                    dividerHandle.pressX = here
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.pressed || parent.containsMouse ? 3 : 1
                    color: parent.pressed ? Theme.accent
                                          : (parent.containsMouse
                                             ? Qt.lighter(Theme.accent, 1.15)
                                             : "#1b1c1f")
                    opacity: parent.pressed || parent.containsMouse ? 0.95 : 0.8
                }
            }
        }

        // ---- Footer ------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Theme.bgPanel

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.border
            }

            LumetriActionButton {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 64
                height: 18
                text: "Reset All"
                enabled: root.hasClip
                onClicked: Backend.resetClipEffectSettings(root.clipId)
                ToolTip.visible: hovered
                ToolTip.text: "Reset every built-in parameter on this clip"
            }

            Text {
                x: root.dividerX + 8
                anchors.verticalCenter: parent.verticalCenter
                text: root.timecode(Backend.playheadMs)
                color: Theme.textSecondary
                font.family: Theme.monoFont
                font.pixelSize: Theme.fsXs
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: root.timecode(root.clipSpanMs)
                color: Theme.textMuted
                font.family: Theme.monoFont
                font.pixelSize: Theme.fsXs
            }
        }
    }
}
