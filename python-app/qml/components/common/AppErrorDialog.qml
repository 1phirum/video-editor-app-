// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"

// App-wide failure popup. Something that used to fail silently (a bad status
// line the user had to notice) now calls open(title, reason) and this modal
// shows the actual reason, with a Copy button so it can be pasted into a bug
// report or support chat. Wired to Backend.operationFailed in main.qml.
Popup {
    id: root

    property string errorTitle: qsTr("Something went wrong")
    property string errorReason: ""

    function show(title, reason) {
        root.errorTitle = title && title.length > 0 ? title : qsTr("Something went wrong")
        root.errorReason = reason && reason.length > 0
                ? reason
                : qsTr("No further detail was reported.")
        root.open()
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    width: Math.min(520, (parent ? parent.width : 520) - 80)
    padding: 0

    background: Rectangle {
        color: Theme.bgPanel
        border.color: Theme.border
        radius: Theme.radiusSm
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header: alert icon + title
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.bottomMargin: 10
            spacing: 10
            Image {
                source: "../../assets/icons/alert-circle.svg"
                sourceSize.width: 20
                sourceSize.height: 20
                Layout.alignment: Qt.AlignTop
            }
            Text {
                Layout.fillWidth: true
                text: root.errorTitle
                color: Theme.textPrimary
                font.pixelSize: Theme.fsXl
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // Reason: scrollable so a long provider error (stack trace, JSON) is
        // fully readable rather than clipped.
        Flickable {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.preferredHeight: Math.min(reasonText.implicitHeight, 240)
            contentHeight: reasonText.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Text {
                id: reasonText
                width: parent.width
                text: root.errorReason
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // Footer: Copy detail + Close
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8
            Button {
                text: qsTr("Copy detail")
                implicitHeight: 32
                implicitWidth: 110
                onClicked: {
                    copyHelper.text = root.errorTitle + "\n\n" + root.errorReason
                    copyHelper.selectAll()
                    copyHelper.copy()
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                contentItem: Text {
                    text: parent.text
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.down ? Theme.hover : Theme.bgPrimary
                    border.color: Theme.border
                    radius: Theme.radiusSm
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Close")
                implicitHeight: 32
                implicitWidth: 96
                onClicked: root.close()
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pixelSize: Theme.fsSm
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.down ? Theme.hover : Theme.accent
                    border.color: Theme.accent
                    radius: Theme.radiusSm
                }
            }
        }
    }

    // Off-screen helper that owns the clipboard copy (TextEdit has copy()).
    TextEdit {
        id: copyHelper
        visible: false
        width: 0
        height: 0
    }
}
