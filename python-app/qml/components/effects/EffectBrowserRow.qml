pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import QtQuick.Controls
import CutPro 1.0
import "../../theme"
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

Rectangle {
    id: root

    property var rowData: ({})
    property bool canApply: false
    property string clipId: ""
    property string basePreviewUrl: ""
    property string stillPreviewUrl: ""
    property string motionPreviewUrl: ""
    signal folderToggled(string key)
    signal effectRequested(var definition)

    readonly property bool isFolder: rowData.type === "folder"
    readonly property int depth: Number(rowData.depth || 0)
    readonly property bool videoPreview: !isFolder && rowData.definition
                                         && rowData.definition.mediaType === "video"

    implicitHeight: isFolder ? 30 : 112
    color: rowHover.hovered ? Theme.hover : "transparent"

    function requestPreview(animated) {
        if (!videoPreview || clipId === "")
            return
        var url = Backend.requestEffectPreview(
                    clipId, String(rowData.definition.id), animated)
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

    // Do not render previews for every row while the browser opens. Preview
    // generation starts only when the user inspects a specific effect.
    Component.onCompleted: resetPreview()
    onClipIdChanged: resetPreview()
    onRowDataChanged: resetPreview()

    Connections {
        target: Backend
        function onEffectPreviewReady(readyClipId, readyEffectId,
                                      animated, url) {
            if (String(readyClipId) !== root.clipId || root.isFolder
                    || String(readyEffectId)
                       !== String(root.rowData.definition.id))
                return
            if (animated)
                root.motionPreviewUrl = url
            else
                root.stillPreviewUrl = url
        }
    }

    Image {
        id: disclosure
        anchors.left: parent.left
        anchors.leftMargin: 8 + root.depth * 14
        anchors.verticalCenter: parent.verticalCenter
        width: 11
        height: 11
        source: root.isFolder
                ? "../../assets/icons/" + (root.rowData.expanded
                                             ? "chevron-down" : "chevron-right") + ".svg"
                : ""
        visible: root.isFolder
        opacity: 0.72
    }

    Image {
        id: rowIcon
        anchors.left: disclosure.right
        anchors.leftMargin: 5
        anchors.verticalCenter: parent.verticalCenter
        width: 15
        height: 15
        source: root.isFolder
                ? "../../assets/icons/" + (root.rowData.expanded
                                             ? "folder-open" : "folder") + ".svg"
                : ""
        visible: root.isFolder
        opacity: 0.74
    }

    Rectangle {
        id: previewFrame
        visible: !root.isFolder
        anchors.left: parent.left
        anchors.leftMargin: 10 + root.depth * 14
        anchors.verticalCenter: parent.verticalCenter
        width: 118
        height: 78
        radius: 4
        color: Theme.bgPrimary
        border.width: rowHover.hovered ? 1 : 0
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
            playing: rowHover.hovered && source !== ""
            visible: rowHover.hovered && source !== ""
        }

        Image {
            anchors.centerIn: parent
            width: 25
            height: 25
            source: root.isFolder ? ""
                    : "../../assets/icons/"
                      + String(root.rowData.definition.icon) + ".svg"
            visible: !stillImage.visible && !motionImage.visible
            opacity: 0.7
        }
    }

    Text {
        id: primaryText
        anchors.left: root.isFolder ? rowIcon.right : previewFrame.right
        anchors.leftMargin: 7
        anchors.right: root.isFolder ? parent.right : applyButton.left
        anchors.rightMargin: 8
        anchors.verticalCenter: root.isFolder ? parent.verticalCenter : undefined
        anchors.top: root.isFolder ? undefined : parent.top
        anchors.topMargin: root.isFolder ? 0 : 14
        text: root.isFolder ? String(root.rowData.name)
                            : String(root.rowData.definition.name)
        color: Theme.textPrimary
        font.pixelSize: Theme.fsSm
        font.weight: root.isFolder ? Font.DemiBold : Font.Medium
        elide: Text.ElideRight
    }

    Text {
        anchors.left: primaryText.left
        anchors.right: applyButton.left
        anchors.rightMargin: 8
        anchors.top: primaryText.bottom
        anchors.topMargin: 5
        visible: !root.isFolder
        text: root.isFolder ? "" : String(root.rowData.definition.description)
        color: Theme.textMuted
        font.pixelSize: Theme.fsXs
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        elide: Text.ElideRight
    }

    IconButton {
        id: applyButton
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.top: parent.top
        anchors.topMargin: 10
        visible: !root.isFolder
        iconName: "plus"
        boxSize: 25
        glyphSize: 13
        enabled: root.canApply
        onClicked: root.effectRequested(root.rowData.definition)
        ToolTip.visible: hovered
        ToolTip.text: "Apply effect"
    }

    HoverHandler {
        id: rowHover
        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: {
            if (hovered) {
                root.requestPreview(false)
                root.requestPreview(true)
            }
        }
    }
    TapHandler {
        enabled: root.isFolder
        onTapped: root.folderToggled(String(root.rowData.key))
    }
    EffectDragHandle {
        anchors.left: parent.left
        anchors.right: applyButton.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        enabled: !root.isFolder
        visible: !root.isFolder
        dragEffectId: root.isFolder ? "" : String(root.rowData.definition.id)
        dragEffectName: root.isFolder ? "" : String(root.rowData.definition.name)
        dragEffectIcon: root.isFolder ? "sliders-horizontal"
                                      : String(root.rowData.definition.icon)
        dragMediaType: root.isFolder ? "video"
                                     : String(root.rowData.definition.mediaType)
        onDoubleClicked: root.effectRequested(root.rowData.definition)
    }


}
