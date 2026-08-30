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

Rectangle {
    id: root

    required property string clipId
    required property var instanceData
    required property var definitionData
    required property int stackIndex
    required property int stackCount
    property bool expanded: true
    readonly property bool isCustomBlur:
        root.instanceData.definitionId === "custom_blur"

    Layout.fillWidth: true
    implicitHeight: header.height + (expanded ? parameterColumn.implicitHeight + 16 : 0)
    color: Theme.bgPanel

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 38
        color: headerHover.hovered ? Theme.hover : Theme.bgPanel

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 2

            IconButton {
                iconName: root.expanded ? "chevron-down" : "chevron-right"
                boxSize: 24
                glyphSize: 12
                onClicked: root.expanded = !root.expanded
                ToolTip.visible: hovered
                ToolTip.text: root.expanded ? "Collapse" : "Expand"
            }
            IconButton {
                iconName: root.instanceData.enabled === false ? "eye-off" : "eye"
                boxSize: 24
                glyphSize: 13
                active: root.instanceData.enabled !== false
                onClicked: Backend.setClipEffectEnabled(
                               root.clipId, root.instanceData.id,
                               root.instanceData.enabled === false)
                ToolTip.visible: hovered
                ToolTip.text: root.instanceData.enabled === false
                              ? "Enable effect" : "Bypass effect"
            }
            Text {
                Layout.fillWidth: true
                text: String(root.definitionData.name || "Missing effect")
                color: root.instanceData.enabled === false
                       ? Theme.textMuted : Theme.textPrimary
                font.pixelSize: Theme.fsSm
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            IconButton {
                iconName: "chevron-up"
                boxSize: 22
                glyphSize: 12
                enabled: root.stackIndex > 0
                onClicked: Backend.moveClipEffect(root.clipId,
                                                  root.instanceData.id, -1)
                ToolTip.visible: hovered
                ToolTip.text: "Move effect up"
            }
            IconButton {
                iconName: "chevron-down"
                boxSize: 22
                glyphSize: 12
                enabled: root.stackIndex + 1 < root.stackCount
                onClicked: Backend.moveClipEffect(root.clipId,
                                                  root.instanceData.id, 1)
                ToolTip.visible: hovered
                ToolTip.text: "Move effect down"
            }
            IconButton {
                iconName: "undo-2"
                boxSize: 22
                glyphSize: 12
                onClicked: Backend.resetClipEffectInstance(
                               root.clipId, root.instanceData.id)
                ToolTip.visible: hovered
                ToolTip.text: "Reset effect"
            }
            IconButton {
                iconName: "trash-2"
                boxSize: 22
                glyphSize: 12
                onClicked: Backend.removeClipEffect(root.clipId,
                                                    root.instanceData.id)
                ToolTip.visible: hovered
                ToolTip.text: "Remove effect"
            }
        }

        HoverHandler { id: headerHover }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    ColumnLayout {
        id: parameterColumn
        x: 14
        y: header.height + 8
        width: Math.max(0, parent.width - 28)
        visible: root.expanded
        spacing: 6

        Repeater {
            model: root.definitionData.parameters || []
            // Registry-fixed: a definition declares its parameters once at
            // startup, so this should never move. Recorded because "should
            // never move" is the class of assumption that hid the last freeze.
            onCountChanged: ModelGuard.note("effectStack.parameters", count)
            delegate: EffectParameterControl {
                required property var modelData
                parameterData: modelData
                currentValue: root.instanceData.parameters
                              ? root.instanceData.parameters[modelData.id]
                              : modelData.default
                onValueCommitted: value => Backend.setClipEffectParameter(
                                      root.clipId, root.instanceData.id,
                                      modelData.id, value)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            visible: root.isCustomBlur

            Text {
                Layout.fillWidth: true
                text: "Mask"
                color: Theme.textSecondary
                font.pixelSize: Theme.fsXs
            }
            IconButton {
                iconName: "pen-tool"
                boxSize: 26
                glyphSize: 14
                enabled: root.instanceData.enabled !== false
                active: Backend.customBlurEditClipId === root.clipId
                        && Backend.customBlurEditInstanceId
                           === root.instanceData.id
                onClicked: {
                    if (active)
                        Backend.endCustomBlurMaskEdit()
                    else
                        Backend.beginCustomBlurMaskEdit(root.clipId,
                                                        root.instanceData.id)
                }
                ToolTip.visible: hovered
                ToolTip.text: active ? "Finish drawing blur mask"
                                     : "Draw blur mask in Program Monitor"
            }
        }

        Text {
            Layout.fillWidth: true
            visible: !root.definitionData.parameters
                     || root.definitionData.parameters.length === 0
            text: "No parameters"
            color: Theme.textMuted
            font.pixelSize: Theme.fsXs
        }
    }
}
