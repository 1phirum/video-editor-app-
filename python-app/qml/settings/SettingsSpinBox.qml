import QtQuick
import QtQuick.Controls
import "../theme"

SpinBox {
    id: root
    implicitWidth: 116
    implicitHeight: 32
    editable: true

    HoverHandler { cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    contentItem: TextInput {
        text: root.textFromValue(root.value, root.locale)
        color: Theme.textPrimary
        font.pixelSize: Theme.fsSm
        horizontalAlignment: TextInput.AlignRight
        verticalAlignment: TextInput.AlignVCenter
        rightPadding: 30
        selectByMouse: true
        validator: root.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }
    up.indicator: Image {
        x: root.width - width - 7
        y: 3
        width: 12
        height: 12
        source: "../../assets/icons/chevron-up.svg"
        opacity: root.up.pressed ? 1 : 0.75
    }
    down.indicator: Image {
        x: root.width - width - 7
        y: root.height - height - 3
        width: 12
        height: 12
        source: "../../assets/icons/chevron-down.svg"
        opacity: root.down.pressed ? 1 : 0.75
    }
    background: Rectangle {
        color: Theme.bgPrimary
        border.color: root.activeFocus ? Theme.accent : Theme.border
        radius: Theme.radiusSm
    }
}
