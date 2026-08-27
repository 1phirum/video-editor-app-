//qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"
import "../common"
import "../effects"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

ComboBox {
    id: control

    implicitWidth: 210
    implicitHeight: 30
    leftPadding: 10
    rightPadding: 30
    hoverEnabled: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Text {
        leftPadding: 0
        rightPadding: 0
        text: control.displayText
        color: control.enabled ? Theme.textPrimary : Theme.textMuted
        font.pixelSize: Theme.fsSm
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Image {
        x: control.width - width - 9
        y: Math.round((control.height - height) / 2)
        width: 13
        height: 13
        source: "../../assets/icons/chevron-down.svg"
        opacity: control.enabled ? 0.72 : 0.3
    }

    background: Rectangle {
        color: control.down || control.visualFocus ? Theme.hover : Theme.bgPrimary
        border.width: 1
        border.color: control.visualFocus ? Theme.accent : Theme.border
        radius: Theme.radiusSm
    }

    delegate: ItemDelegate {
        required property int index
        required property var modelData
        width: control.width
        height: 30
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: control.textRole !== "" && modelData
                  ? modelData[control.textRole] : String(modelData)
            color: index === control.currentIndex ? Theme.accent : Theme.textPrimary
            font.pixelSize: Theme.fsSm
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: parent.highlighted ? Theme.hover : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 250)
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: Theme.bgSidebar
            border.width: 1
            border.color: Theme.border
            radius: Theme.radiusSm
        }
    }
}
