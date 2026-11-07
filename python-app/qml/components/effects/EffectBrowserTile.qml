pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import QtQuick.Controls
import CutPro 1.0
import "../../theme"
import "../common"

// One card in the sidebar layout's grid. The same thing EffectBrowserRow draws,
// with the parts that only make sense in a full-width row taken out: the
// description moves into the tooltip, and the name is allowed two lines because
// a tile is about a third as wide as a row.
//
// It also draws what a row never had to: presets. A preset is handed here in the
// same shape as an effect definition, with a "kind" that says which it is, and
// the two differ in exactly two ways - a preset cannot be dragged onto the
// timeline (the drop target applies one effect id, which a preset is not), and
// it has no preview to render, because previews are rendered per effect id.
Rectangle {
    id: root

    property var definition: ({})
    property bool canApply: false
    property string clipId: ""
    property string basePreviewUrl: ""
    property string stillPreviewUrl: ""
    property string motionPreviewUrl: ""
    signal effectRequested(var definition)

    readonly property string kind: String(root.definition.kind || "effect")
    readonly property bool draggable: root.kind === "effect"
    readonly property bool videoPreview: root.draggable
                                         && root.definition.mediaType === "video"
    readonly property int thumbHeight: Math.round(root.width * 9 / 16)
    readonly property int nameHeight: Math.ceil(Theme.fsXs * 2.7)

    implicitHeight: root.thumbHeight + root.nameHeight + 12
    radius: Theme.radiusSm
    color: tileHover.hovered ? Theme.hover : "transparent"

    function requestPreview(animated) {
        if (!videoPreview || clipId === "")
            return
        var url = Backend.requestEffectPreview(
                    clipId, String(root.definition.id), animated)
        if (url !== "") {
            if (animated)
                motionPreviewUrl = url
            else
                stillPreviewUrl = url
        }
    }

    function resetPreview() {
        stillPreviewUrl = ""
        motionPreviewUrl = ""
    }

    // As in the row: nothing is rendered until the pointer picks a tile out. A
    // grid shows three times as many cards at once, so starting a render per
    // visible card would be three times the work for the same one look.
    Component.onCompleted: resetPreview()
    onClipIdChanged: resetPreview()
    onDefinitionChanged: resetPreview()

    Connections {
        target: Backend
        function onEffectPreviewReady(readyClipId, readyEffectId,
                                      animated, url) {
            if (String(readyClipId) !== root.clipId || !root.videoPreview
                    || String(readyEffectId) !== String(root.definition.id))
                return
            if (animated)
                root.motionPreviewUrl = url
            else
                root.stillPreviewUrl = url
        }
    }

    Rectangle {
        id: thumb
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 4
        height: root.thumbHeight
        radius: 3
        color: Theme.bgPrimary
        border.width: tileHover.hovered ? 1 : 0
        border.color: Theme.accent
        clip: true

        Image {
            id: stillImage
            anchors.fill: parent
            anchors.margins: 1
            source: root.videoPreview
                    ? (root.stillPreviewUrl !== ""
                       ? root.stillPreviewUrl : root.basePreviewUrl)
                    : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            visible: source !== ""
        }

        AnimatedImage {
            id: motionImage
            anchors.fill: parent
            anchors.margins: 1
            source: root.motionPreviewUrl
            fillMode: Image.PreserveAspectCrop
            cache: false
            playing: tileHover.hovered && source !== ""
            visible: tileHover.hovered && source !== ""
        }

        Image {
            anchors.centerIn: parent
            width: 22
            height: 22
            source: "../../assets/icons/"
                    + String(root.definition.icon || "sliders-horizontal") + ".svg"
            visible: !stillImage.visible && !motionImage.visible
            opacity: 0.7
        }
    }

    Text {
        id: nameText
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        anchors.top: thumb.bottom
        anchors.topMargin: 3
        height: root.nameHeight
        text: String(root.definition.name || "")
        color: Theme.textPrimary
        font.pixelSize: Theme.fsXs
        font.weight: Font.Medium
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        verticalAlignment: Text.AlignTop
    }

    HoverHandler {
        id: tileHover
        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: {
            if (hovered) {
                root.requestPreview(false)
                root.requestPreview(true)
            }
        }
    }

    // The description has nowhere to go on a card this size, and it is the only
    // thing that says what "Bleach Bypass" does before you apply it.
    ToolTip.visible: tileHover.hovered
                     && String(root.definition.description || "") !== ""
    ToolTip.delay: 500
    ToolTip.text: String(root.definition.description || "")

    // Declared before the apply button so the button sits above it: the handle's
    // MouseArea sets preventStealing, and underneath it the press would never
    // reach the button at all.
    EffectDragHandle {
        anchors.fill: thumb
        enabled: root.draggable
        visible: root.draggable
        dragEffectId: root.draggable ? String(root.definition.id) : ""
        dragEffectName: String(root.definition.name || "")
        dragEffectIcon: String(root.definition.icon || "sliders-horizontal")
        dragMediaType: String(root.definition.mediaType || "video")
        onDoubleClicked: root.effectRequested(root.definition)
    }

    // A preset has no drag handle to double-click, so it gets its own.
    TapHandler {
        enabled: !root.draggable
        onDoubleTapped: root.effectRequested(root.definition)
    }

    IconButton {
        anchors.right: thumb.right
        anchors.top: thumb.top
        anchors.margins: 2
        iconName: "plus"
        boxSize: 21
        glyphSize: 11
        enabled: root.canApply
        opacity: root.canApply ? 0.95 : 0.4
        onClicked: root.effectRequested(root.definition)

        // Drawn on top of the picture, so it needs something behind it or the
        // glyph disappears into a bright frame.
        Rectangle {
            anchors.fill: parent
            z: -1
            radius: Theme.radiusSm
            color: Qt.rgba(0, 0, 0, 0.5)
        }
    }
}
