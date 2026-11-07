pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "../theme"

ComboBox {
    id: root
    implicitWidth: 220
    implicitHeight: 32
    leftPadding: 10
    rightPadding: 30

    HoverHandler { cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    contentItem: Text {
        text: root.displayText
        color: Theme.textPrimary
        font.pixelSize: Theme.fsSm
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Image {
        width: 14
        height: 14
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        source: root.popup.visible ? "../../assets/icons/chevron-up.svg"
                                   : "../../assets/icons/chevron-down.svg"
        opacity: 0.8
    }
    background: Rectangle {
        color: root.activeFocus ? Theme.hover : Theme.bgPrimary
        border.color: root.activeFocus ? Theme.accent : Theme.border
        radius: Theme.radiusSm
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        width: root.popup.width - 2
        height: 34
        highlighted: root.highlightedIndex === index
        contentItem: Text {
            text: root.textAt(option.index)
            color: option.highlighted ? Theme.textPrimary : Theme.textSecondary
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: option.highlighted ? Theme.hover : Theme.bgPanel
        }
    }
    popup: Popup {
        y: root.height
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 280)
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: Theme.bgPanel
            border.color: Theme.border
            radius: Theme.radiusSm
        }
    }
}
