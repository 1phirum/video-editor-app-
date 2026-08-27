import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

SpinBox {
    id: control

    implicitWidth: 118
    implicitHeight: 30
    leftPadding: 30
    rightPadding: 30
    editable: true
    hoverEnabled: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: TextInput {
        z: 2
        text: control.textFromValue(control.value, control.locale)
        color: Theme.textPrimary
        selectionColor: Theme.accent
        selectedTextColor: "white"
        font.pixelSize: Theme.fsMd
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        onEditingFinished: {
            var parsed = control.valueFromText(text, control.locale)
            control.value = Math.max(control.from, Math.min(control.to, parsed))
            control.valueModified()
        }
    }

    down.indicator: Rectangle {
        x: 0
        y: 0
        implicitWidth: 30
        implicitHeight: control.height
        color: downHover.hovered ? Theme.hover : "transparent"

        HoverHandler { id: downHover }

        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 1
            color: control.enabled ? Theme.textSecondary : Theme.textMuted
        }
    }

    up.indicator: Rectangle {
        x: control.width - width
        y: 0
        implicitWidth: 30
        implicitHeight: control.height
        color: upHover.hovered ? Theme.hover : "transparent"

        HoverHandler { id: upHover }

        Image {
            anchors.centerIn: parent
            width: 13
            height: 13
            source: "../../assets/icons/plus.svg"
            opacity: control.enabled ? 0.78 : 0.35
        }
    }

    background: Rectangle {
        color: control.activeFocus ? Theme.hover : Theme.bgPrimary
        border.color: control.activeFocus ? Theme.accent : Theme.border
        radius: Theme.radiusSm

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 29
            width: 1
            color: Theme.border
        }
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.rightMargin: 29
            width: 1
            color: Theme.border
        }
    }
}
