// qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

// Premiere's editable parameter value: borderless blue text that scrubs when
// dragged sideways and turns into a numeric field when clicked. No box, no
// spinner — the value itself is the control.
Item {
    id: root

    property real value: 0
    property real from: -100
    property real to: 100
    property int decimals: 0
    property string unit: ""
    property bool editing: false
    // While scrubbing only the draft is published (one undo entry per gesture);
    // valueEdited fires when the drag ends or typed input is confirmed.
    signal valueDrafted(real value)
    signal valueEdited(real value)

    implicitWidth: Theme.ecValueWidth
    implicitHeight: 18

    readonly property real span: Math.max(0.000001, to - from)
    // ~220 px of travel covers the whole range; Ctrl scrubs five times finer.
    readonly property real pixelStep: span / 220

    function clampValue(candidate) {
        return Math.max(root.from, Math.min(root.to, candidate))
    }
    function roundValue(candidate) {
        return Number(Number(candidate).toFixed(root.decimals))
    }
    function displayText(candidate) {
        return Number(candidate).toFixed(root.decimals)
               + (root.unit === "" ? "" : " " + root.unit)
    }
    function beginEditing() {
        field.text = Number(root.value).toFixed(root.decimals)
        root.editing = true
        field.selectAll()
        field.forceActiveFocus()
    }
    function commitTyped() {
        var parsed = Number(field.text)
        root.editing = false
        if (field.text !== "" && !isNaN(parsed))
            root.valueEdited(root.roundValue(root.clampValue(parsed)))
    }

    Text {
        id: label
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        visible: !root.editing
        text: root.displayText(root.value)
        color: !root.enabled ? Theme.textMuted
               : scrub.pressed ? "#ffffff"
               : scrub.containsMouse ? Qt.lighter(Theme.ecValueText, 1.2)
               : Theme.ecValueText
        font.pixelSize: Theme.fsSm
        font.family: Theme.monoFont
    }

    // Hover underline — Premiere's hint that the number is draggable.
    Rectangle {
        anchors.left: label.left
        anchors.right: label.right
        anchors.top: label.bottom
        height: 1
        visible: label.visible && root.enabled && scrub.containsMouse
        color: label.color
    }

    MouseArea {
        id: scrub
        anchors.fill: parent
        enabled: root.enabled && !root.editing
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.SizeHorCursor : Qt.ArrowCursor
        property real pressX: 0
        property real startValue: 0
        property bool scrubbed: false

        onPressed: (mouse) => {
            scrub.pressX = mouse.x
            scrub.startValue = root.value
            scrub.scrubbed = false
        }
        onPositionChanged: (mouse) => {
            if (!scrub.pressed)
                return
            var dx = mouse.x - scrub.pressX
            if (!scrub.scrubbed && Math.abs(dx) < 3)
                return
            scrub.scrubbed = true
            var fine = (mouse.modifiers & Qt.ControlModifier) !== 0
            var next = root.roundValue(root.clampValue(
                           scrub.startValue + dx * root.pixelStep * (fine ? 0.2 : 1)))
            if (next !== root.value)
                root.valueDrafted(next)
        }
        onReleased: {
            if (scrub.scrubbed)
                root.valueEdited(root.value)
            else
                root.beginEditing()
        }
        onCanceled: {
            if (scrub.scrubbed)
                root.valueEdited(root.value)
        }
    }

    TextInput {
        id: field
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: root.implicitHeight
        visible: root.editing
        color: Theme.textPrimary
        font.pixelSize: Theme.fsSm
        font.family: Theme.monoFont
        selectionColor: Theme.accent
        selectedTextColor: "#ffffff"
        selectByMouse: true
        leftPadding: 3
        verticalAlignment: TextInput.AlignVCenter
        validator: DoubleValidator {
            bottom: root.from
            top: root.to
            decimals: root.decimals
            notation: DoubleValidator.StandardNotation
        }
        onEditingFinished: root.commitTyped()
        onActiveFocusChanged: {
            if (!field.activeFocus && root.editing)
                root.commitTyped()
        }
        Keys.onEscapePressed: root.editing = false

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: -1
            anchors.bottomMargin: -1
            z: -1
            color: Theme.bgPrimary
            border.width: 1
            border.color: Theme.accent
            radius: 2
        }
    }
}
