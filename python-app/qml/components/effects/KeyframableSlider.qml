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

RowLayout {
    id: root
    property string clipId: ""
    property string effectKey: ""
    property string label: ""
    property real from: -100
    property real to: 100
    property real value: 0
    property int decimals: 0
    property string suffix: ""
    property bool keyframed: false
    signal valueCommitted(real value)

    Layout.fillWidth: true
    Layout.preferredHeight: keyframed ? 31 : 28
    spacing: 3

    IconButton {
        id: stopwatch
        boxSize: 22
        glyphSize: 13
        iconName: "clock"
        restColor: root.keyframed ? Theme.accent : Theme.textMuted
        active: root.keyframed
        enabled: root.clipId !== "" && root.effectKey !== ""
        onClicked: {
            var t = Math.max(0, Number(Backend.playheadMs))
            Backend.keyframeEngine.toggleKeyframing(root.clipId,
                                                    root.effectKey, t,
                                                    root.value)
            root.refreshKeyframed()
        }
        ToolTip.visible: hovered
        ToolTip.text: root.keyframed ? "Remove keyframe" : "Add keyframe"
    }

    LumetriSlider {
        id: slider
        Layout.fillWidth: true
        label: root.label
        from: root.from
        to: root.to
        value: root.value
        decimals: root.decimals
        suffix: root.suffix
        onValueCommitted: value => {
            root.valueCommitted(value)
            if (root.keyframed)
                Backend.keyframeEngine.addKeyframe(root.clipId, root.effectKey,
                                                   Math.max(0, Number(Backend.playheadMs)), value)
        }
    }

    RowLayout {
        visible: root.keyframed
        spacing: 0
        IconButton {
            boxSize: 20; glyphSize: 11; iconName: "chevron-left"
            enabled: Backend.keyframeEngine.previousTime(root.clipId,
                                                          root.effectKey,
                                                          Backend.playheadMs) >= 0
            onClicked: {
                var t = Backend.keyframeEngine.previousTime(root.clipId,
                                                             root.effectKey,
                                                             Backend.playheadMs)
                if (t >= 0) Backend.playheadMs = t
            }
            ToolTip.visible: hovered; ToolTip.text: "Previous keyframe"
        }
        IconButton {
            boxSize: 20; glyphSize: 11; iconName: "chevron-right"
            enabled: Backend.keyframeEngine.nextTime(root.clipId,
                                                      root.effectKey,
                                                      Backend.playheadMs) >= 0
            onClicked: {
                var t = Backend.keyframeEngine.nextTime(root.clipId,
                                                        root.effectKey,
                                                        Backend.playheadMs)
                if (t >= 0) Backend.playheadMs = t
            }
            ToolTip.visible: hovered; ToolTip.text: "Next keyframe"
        }
    }

    function refreshKeyframed() {
        root.keyframed = Backend.keyframeEngine.isKeyframed(root.clipId,
                                                            root.effectKey)
    }
    Component.onCompleted: refreshKeyframed()
    onClipIdChanged: refreshKeyframed()
    onEffectKeyChanged: refreshKeyframed()
    Connections {
        target: Backend.keyframeEngine
        function onKeyframesChanged(clipId) {
            if (String(clipId) === root.clipId)
                root.refreshKeyframed()
        }
    }
}
