//qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

ComboBox {
    id: control

    property int popupWidth: 180
    property url itemStatusIconSource: ""
    property var itemStatusIconVisible: null

    function hasItemStatusIcon(index) {
        if (index < 0 || !control.itemStatusIconSource
                || typeof control.itemStatusIconVisible !== "function")
            return false
        return Boolean(control.itemStatusIconVisible(control.textAt(index), index))
    }

    implicitHeight: 32

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    leftPadding: 10
    rightPadding: 30

    contentItem: Item {
        Text {
            anchors.left: parent.left
            anchors.right: selectedStatusIcon.visible
                           ? selectedStatusIcon.left : parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.rightMargin: selectedStatusIcon.visible ? 7 : 0
            text: control.displayText
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Image {
            id: selectedStatusIcon
            width: 15
            height: 15
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            source: control.itemStatusIconSource
            visible: control.hasItemStatusIcon(control.currentIndex)
            opacity: 0.92
        }
    }

    indicator: Image {
        width: 14
        height: 14
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        source: control.popup.visible ? "../../assets/icons/chevron-up.svg"
                                      : "../../assets/icons/chevron-down.svg"
        opacity: control.enabled ? 0.8 : 0.35
    }

    background: Rectangle {
        color: control.activeFocus ? Theme.hover : Theme.bgPrimary
        border.color: control.activeFocus ? Theme.accent : Theme.border
        radius: Theme.radiusSm
    }

    delegate: ItemDelegate {
        id: option
        required property int index
        width: control.popup.width - 2
        height: 38
        highlighted: control.highlightedIndex === index
        contentItem: Item {
            Text {
                anchors.left: parent.left
                anchors.right: optionStatusIcon.visible
                               ? optionStatusIcon.left : parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.rightMargin: optionStatusIcon.visible ? 10 : 0
                text: control.textAt(option.index)
                color: option.highlighted ? Theme.textPrimary : Theme.textSecondary
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Image {
                id: optionStatusIcon
                width: 16
                height: 16
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                source: control.itemStatusIconSource
                visible: control.hasItemStatusIcon(option.index)
                opacity: 0.95
            }
        }
        background: Rectangle {
            color: option.highlighted ? Theme.hover : Theme.bgPanel
        }
    }

    popup: Popup {
        y: control.height
        width: Math.max(control.width, control.popupWidth)
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 280)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: Theme.bgPanel
            border.color: Theme.border
            radius: Theme.radiusSm
        }
    }
}
