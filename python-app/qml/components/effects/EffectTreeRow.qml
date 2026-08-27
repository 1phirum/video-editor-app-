pragma ComponentBehavior: Bound
// qmllint disable
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

// One row of the Premiere-style Effect Controls tree. Every row is exactly
// Theme.ecRowHeight tall and every column starts at the same x, which is what
// makes the panel read as a grid instead of a stack of widgets. The keyframe
// lanes on the right are driven by the same row model, so lane N always lines
// up with row N.
//
// Row kinds: band | group | param | slider | bool | select | masks | blurmask
//            | note
Rectangle {
    id: root

    required property var rowData
    property string clipId: ""
    property var currentValue: undefined
    property bool expanded: false
    signal expandRequested
    // Live value while a slider or number is being dragged. The panel holds it
    // so the backend (and the undo stack) only sees the finished gesture.
    signal draftChanged(real value)
    signal draftCleared

    readonly property string kind: String(root.rowData.kind || "param")
    readonly property string paramId: String(root.rowData.id || "")
    readonly property string instanceId: String(root.rowData.instanceId || "")
    readonly property bool isBand: root.kind === "band"
    readonly property bool isGroup: root.kind === "group"
    readonly property bool isSlider: root.kind === "slider"
    readonly property bool isNote: root.kind === "note"
    readonly property bool expandable: root.rowData.expandable === true
    readonly property bool rowEnabled: root.rowData.disabled !== true
    readonly property bool bypassed: root.rowData.bypassed === true

    readonly property real defaultNumber:
        Number(root.rowData.def === undefined ? 0 : root.rowData.def)
    readonly property real numberValue:
        Number(root.currentValue === undefined || root.currentValue === null
               ? root.defaultNumber : root.currentValue)
    readonly property bool boolValue:
        Boolean(root.currentValue === undefined || root.currentValue === null
                ? root.rowData.def : root.currentValue)
    readonly property string stringValue:
        String(root.currentValue === undefined || root.currentValue === null
               ? (root.rowData.def === undefined ? "" : root.rowData.def)
               : root.currentValue)

    // Instance parameters are not wired to the keyframe engine (its channels are
    // keyed by clip + built-in property), so they show no stopwatch.
    readonly property bool keyframable: root.rowData.kf === true
                                        && root.instanceId === ""
    property bool keyframed: false

    readonly property bool modified:
        root.kind === "param" ? Math.abs(root.numberValue - root.defaultNumber) > 0.0001
        : root.kind === "bool" ? root.boolValue !== Boolean(root.rowData.def)
        : root.kind === "select" ? root.stringValue !== String(root.rowData.def || "")
        : true

    // ---- Column grid ---------------------------------------------------
    readonly property int indentX:
        8 + ((root.isBand || root.isGroup) ? 0 : Theme.ecIndent)
    readonly property int labelX: root.indentX + Theme.ecGutter + Theme.ecBadge
    readonly property int trailX:
        root.width - Theme.ecResetWidth - Theme.ecNavWidth - 4
    readonly property int valueX: root.trailX - Theme.ecValueWidth - 6

    height: Theme.ecRowHeight
    color: root.isBand ? Theme.ecBand
           : rowHover.hovered && !root.isSlider && !root.isNote ? Theme.ecRowHover
           : "transparent"

    HoverHandler { id: rowHover }

    // ---- Backend plumbing ----------------------------------------------
    function commitValue(value) {
        if (root.clipId === "" || root.paramId === "")
            return
        if (root.instanceId !== "")
            Backend.setClipEffectParameter(root.clipId, root.instanceId,
                                           root.paramId, value)
        else
            Backend.setClipEffectSetting(root.clipId, root.paramId, value)
    }

    function commitNumber(value) {
        root.commitValue(value)
        if (root.keyframed)
            Backend.keyframeEngine.addKeyframe(root.clipId, root.paramId,
                                               Math.max(0, Number(Backend.playheadMs)),
                                               value)
    }

    function resetRow() {
        if (root.clipId === "")
            return
        if (root.isGroup) {
            if (root.instanceId !== "") {
                Backend.resetClipEffectInstance(root.clipId, root.instanceId)
                return
            }
            var keys = root.rowData.resetKeys || []
            for (var i = 0; i < keys.length; ++i)
                Backend.setClipEffectSetting(root.clipId, String(keys[i].id),
                                             keys[i].def)
            return
        }
        root.commitValue(root.rowData.def)
    }

    // Premiere's stopwatch drops the whole animation channel when switched off.
    function toggleKeyframing() {
        if (root.clipId === "" || root.paramId === "")
            return
        if (root.keyframed) {
            var frames = Backend.keyframeEngine.keyframesFor(root.clipId,
                                                             root.paramId)
            for (var i = frames.length - 1; i >= 0; --i)
                Backend.keyframeEngine.removeKeyframe(root.clipId, root.paramId,
                                                      Number(frames[i].timeMs))
        } else {
            Backend.keyframeEngine.addKeyframe(root.clipId, root.paramId,
                                               Math.max(0, Number(Backend.playheadMs)),
                                               root.numberValue)
        }
        root.refreshKeyframed()
    }

    function refreshKeyframed() {
        root.keyframed = root.keyframable && root.clipId !== ""
                         && Backend.keyframeEngine.isKeyframed(root.clipId,
                                                               root.paramId)
    }

    Component.onCompleted: root.refreshKeyframed()
    onClipIdChanged: root.refreshKeyframed()
    onParamIdChanged: root.refreshKeyframed()

    Connections {
        target: Backend.keyframeEngine
        function onKeyframesChanged(clipId) {
            if (String(clipId) === root.clipId)
                root.refreshKeyframed()
        }
    }

    // ---- Expand / collapse hit area (sits under the buttons) -----------
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(0, root.valueX)
        enabled: root.expandable
        cursorShape: root.expandable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.expandRequested()
    }

    // ---- Disclosure triangle -------------------------------------------
    Image {
        x: root.indentX + 1
        anchors.verticalCenter: parent.verticalCenter
        visible: root.expandable && !root.isBand
        source: "../../assets/icons/"
                + (root.expanded ? "chevron-down" : "chevron-right") + ".svg"
        sourceSize.width: 11
        sourceSize.height: 11
        fillMode: Image.PreserveAspectFit
        opacity: 0.7
    }

    // ---- "fx" badge — doubles as the bypass switch on applied effects ---
    Text {
        id: fxBadge
        x: root.indentX + Theme.ecGutter
        anchors.verticalCenter: parent.verticalCenter
        visible: root.isGroup
        text: "fx"
        font.pixelSize: Theme.fsMd
        font.italic: true
        font.family: "Georgia"
        color: root.bypassed ? Theme.textMuted
               : fxHover.hovered ? Theme.textPrimary : Theme.textSecondary
        opacity: root.bypassed ? 0.45 : 1

        HoverHandler {
            id: fxHover
            enabled: root.instanceId !== ""
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: root.instanceId !== ""
            onTapped: Backend.setClipEffectEnabled(root.clipId, root.instanceId,
                                                   root.bypassed)
        }
        ToolTip.visible: fxHover.hovered
        ToolTip.text: root.bypassed ? "Enable effect" : "Bypass effect"
    }

    // ---- Stopwatch (toggle animation) ----------------------------------
    IconButton {
        x: root.indentX + Theme.ecGutter - 1
        anchors.verticalCenter: parent.verticalCenter
        visible: root.keyframable
        boxSize: 17
        glyphSize: 12
        hoverEnabled: true
        iconName: "clock"
        restColor: root.keyframed ? Theme.accent : Theme.textMuted
        hoverColor: root.keyframed ? Theme.accent : Theme.textPrimary
        onClicked: root.toggleKeyframing()
        ToolTip.visible: hovered
        ToolTip.text: root.keyframed ? "Turn off animation (removes keyframes)"
                                     : "Toggle animation"
    }

    // ---- Parameter / effect / band name --------------------------------
    Text {
        id: nameLabel
        x: root.isBand ? 10 : root.labelX
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, root.valueX - 6 - x)
        visible: !root.isSlider
        text: String(root.rowData.label || "")
        elide: Text.ElideRight
        font.pixelSize: root.isBand ? Theme.fsMd : Theme.fsSm
        font.weight: root.isBand || root.isGroup ? Font.DemiBold : Font.Normal
        font.italic: root.isNote
        color: !root.rowEnabled || root.bypassed ? Theme.textMuted
               : root.isNote ? Theme.textMuted
               : root.isBand || root.isGroup ? Theme.textPrimary
               : Theme.textSecondary
    }

    // ---- Band trailing controls ----------------------------------------
    IconButton {
        anchors.right: bandChevron.left
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        visible: root.isBand && root.clipId !== ""
        boxSize: 20
        glyphSize: 12
        hoverEnabled: true
        iconName: "plus"
        restColor: Theme.textSecondary
        onClicked: Backend.requestEffectsBrowser(root.clipId)
        ToolTip.visible: hovered
        ToolTip.text: "Browse effects"
    }

    Image {
        id: bandChevron
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: root.isBand
        source: "../../assets/icons/"
                + (root.expanded ? "chevron-up" : "chevron-down") + ".svg"
        sourceSize.width: 11
        sourceSize.height: 11
        fillMode: Image.PreserveAspectFit
        opacity: 0.8
    }

    // ---- Value column: number ------------------------------------------
    EffectValueText {
        x: root.valueX
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.ecValueWidth
        visible: root.kind === "param"
        enabled: root.rowEnabled
        value: root.numberValue
        from: Number(root.rowData.from === undefined ? -100 : root.rowData.from)
        to: Number(root.rowData.to === undefined ? 100 : root.rowData.to)
        decimals: Number(root.rowData.decimals || 0)
        unit: String(root.rowData.unit || "")
        onValueDrafted: (value) => root.draftChanged(value)
        onValueEdited: (value) => {
            root.draftCleared()
            root.commitNumber(value)
        }
    }

    // ---- Value column: checkbox -----------------------------------------
    LumetriCheckBox {
        x: root.valueX
        anchors.verticalCenter: parent.verticalCenter
        visible: root.kind === "bool"
        enabled: root.rowEnabled
        checked: root.boolValue
        onToggled: root.commitValue(checked)
    }

    // ---- Value column: mask shape tools ---------------------------------
    Row {
        x: root.valueX - 2
        anchors.verticalCenter: parent.verticalCenter
        visible: root.kind === "masks"
        spacing: 1

        Repeater {
            model: [{ id: "ellipse", icon: "circle", tip: "Ellipse mask" },
                    { id: "rectangle", icon: "square", tip: "Four-point mask" },
                    { id: "pen", icon: "pen-tool", tip: "Free draw mask" }]
            delegate: IconButton {
                required property var modelData
                boxSize: 22
                glyphSize: 13
                hoverEnabled: true
                adobeStyle: true
                iconName: modelData.icon
                active: root.stringValue === modelData.id
                enabled: root.clipId !== ""
                onClicked: root.commitValue(root.stringValue === modelData.id
                                            ? "none" : modelData.id)
                ToolTip.visible: hovered
                ToolTip.text: modelData.tip
            }
        }
    }

    // ---- Value column: draw-mask handoff to the program monitor ---------
    IconButton {
        x: root.valueX - 2
        anchors.verticalCenter: parent.verticalCenter
        visible: root.kind === "blurmask"
        boxSize: 22
        glyphSize: 13
        hoverEnabled: true
        adobeStyle: true
        iconName: "pen-tool"
        enabled: root.rowEnabled && root.clipId !== ""
        active: Backend.customBlurEditClipId === root.clipId
                && Backend.customBlurEditInstanceId === root.instanceId
        onClicked: {
            if (active)
                Backend.endCustomBlurMaskEdit()
            else
                Backend.beginCustomBlurMaskEdit(root.clipId, root.instanceId)
        }
        ToolTip.visible: hovered
        ToolTip.text: active ? "Finish drawing blur mask"
                             : "Draw blur mask in the Program Monitor"
    }

    // ---- Value column: dropdown ----------------------------------------
    Loader {
        id: selectLoader
        x: root.valueX
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(70, root.width - root.valueX - Theme.ecResetWidth - 10)
        height: 20
        active: root.kind === "select"
        visible: selectLoader.active
        sourceComponent: Component {
            ComboBox {
                id: combo
                anchors.fill: parent
                model: root.rowData.options || []
                textRole: "label"
                valueRole: "value"
                currentIndex: combo.indexOfValue(root.stringValue)
                font.pixelSize: Theme.fsXs
                onActivated: root.commitValue(combo.currentValue)

                contentItem: Text {
                    leftPadding: 6
                    rightPadding: 18
                    text: combo.displayText
                    color: Theme.textPrimary
                    font: combo.font
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: combo.hovered ? Theme.hover : Theme.bgPrimary
                    border.width: 1
                    border.color: combo.activeFocus ? Theme.accent : "#3a3a3a"
                    radius: 2
                }
                indicator: Image {
                    x: combo.width - width - 6
                    y: (combo.height - height) / 2
                    source: "../../assets/icons/chevron-down.svg"
                    sourceSize.width: 10
                    sourceSize.height: 10
                    opacity: 0.7
                }
                delegate: ItemDelegate {
                    id: option
                    required property var modelData
                    required property int index
                    width: ListView.view ? ListView.view.width : combo.width
                    height: 22
                    highlighted: combo.highlightedIndex === option.index
                    contentItem: Text {
                        leftPadding: 6
                        text: String(option.modelData.label || "")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsXs
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: option.highlighted ? Theme.accent : "transparent"
                    }
                }
                popup: Popup {
                    y: combo.height
                    width: combo.width
                    implicitHeight: Math.min(280, contentItem.implicitHeight + 2)
                    padding: 1
                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: combo.popup.visible ? combo.delegateModel : null
                        currentIndex: combo.highlightedIndex
                        ScrollIndicator.vertical: ScrollIndicator {}
                    }
                    background: Rectangle {
                        color: Theme.bgPrimary
                        border.width: 1
                        border.color: Theme.border
                    }
                }
            }
        }
    }

    // ---- Revealed slider: its own row, so the lanes stay aligned --------
    Slider {
        id: strip
        visible: root.isSlider
        enabled: root.rowEnabled
        x: root.labelX
        width: Math.max(60, root.trailX - 6 - x)
        anchors.verticalCenter: parent.verticalCenter
        height: 16
        from: Number(root.rowData.from === undefined ? -100 : root.rowData.from)
        to: Number(root.rowData.to === undefined ? 100 : root.rowData.to)
        stepSize: Number(root.rowData.decimals || 0) > 0 ? 0 : 1
        live: true

        Binding {
            target: strip
            property: "value"
            value: root.numberValue
            when: !strip.pressed
            restoreMode: Binding.RestoreBinding
        }
        onMoved: root.draftChanged(strip.value)
        onPressedChanged: {
            if (!strip.pressed) {
                root.draftCleared()
                root.commitNumber(strip.value)
            }
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }

        background: Rectangle {
            x: strip.leftPadding
            y: strip.topPadding + strip.availableHeight / 2 - height / 2
            width: strip.availableWidth
            height: 3
            radius: 1
            color: Theme.ecTrack
            Rectangle {
                width: strip.visualPosition * parent.width
                height: parent.height
                radius: 1
                color: strip.enabled ? Theme.accent : Theme.textMuted
            }
        }
        handle: Rectangle {
            x: strip.leftPadding + strip.visualPosition * (strip.availableWidth - width)
            y: strip.topPadding + strip.availableHeight / 2 - height / 2
            width: 11
            height: 11
            radius: 6
            color: strip.pressed ? "#ffffff" : "#dcdcdc"
            border.width: 1
            border.color: Theme.border
        }
    }

    // ---- Keyframe navigator ---------------------------------------------
    EffectKeyframeNav {
        x: root.trailX
        anchors.verticalCenter: parent.verticalCenter
        visible: root.kind === "param" && root.keyframed
        clipId: root.clipId
        effectKey: root.paramId
        currentValue: root.numberValue
    }

    // ---- Applied-effect ordering / removal (revealed on hover) ----------
    Row {
        x: root.trailX
        anchors.verticalCenter: parent.verticalCenter
        visible: root.isGroup && root.instanceId !== ""
                 && (rowHover.hovered || stackHover.hovered)
        spacing: 0

        HoverHandler { id: stackHover }

        IconButton {
            boxSize: 18
            glyphSize: 11
            hoverEnabled: true
            iconName: "chevron-up"
            restColor: Theme.textSecondary
            enabled: root.rowData.canMoveUp === true
            opacity: enabled ? 1 : 0.35
            onClicked: Backend.moveClipEffect(root.clipId, root.instanceId, -1)
            ToolTip.visible: hovered
            ToolTip.text: "Move effect up"
        }
        IconButton {
            boxSize: 18
            glyphSize: 11
            hoverEnabled: true
            iconName: "chevron-down"
            restColor: Theme.textSecondary
            enabled: root.rowData.canMoveDown === true
            opacity: enabled ? 1 : 0.35
            onClicked: Backend.moveClipEffect(root.clipId, root.instanceId, 1)
            ToolTip.visible: hovered
            ToolTip.text: "Move effect down"
        }
        IconButton {
            boxSize: 18
            glyphSize: 11
            hoverEnabled: true
            iconName: "trash-2"
            restColor: Theme.textSecondary
            hoverColor: Theme.danger
            onClicked: Backend.removeClipEffect(root.clipId, root.instanceId)
            ToolTip.visible: hovered
            ToolTip.text: "Remove effect"
        }
    }

    // ---- Per-row reset ---------------------------------------------------
    IconButton {
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        visible: root.rowData.resettable !== false
                 && (root.isGroup || root.kind === "param" || root.kind === "bool"
                     || root.kind === "select" || root.kind === "masks")
        boxSize: 18
        glyphSize: 12
        hoverEnabled: true
        iconName: "undo-2"
        restColor: Theme.textSecondary
        enabled: root.modified && root.clipId !== ""
        opacity: enabled ? 1 : 0.28
        onClicked: root.resetRow()
        ToolTip.visible: hovered
        ToolTip.text: root.isGroup ? "Reset effect" : "Reset parameter"
    }

    // ---- Row separator ---------------------------------------------------
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.isBand ? Theme.border : Theme.ecRowLine
    }
}
