pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"

ListView {
    id: root
    required property var assistant
    property bool showToolNames: false
    objectName: "aiActionList"
    clip: true; spacing: 5
    model: assistant.actions
    ScrollBar.vertical: ScrollBar {}
    delegate: Rectangle {
        id: action
        required property var modelData
        width: ListView.view.width
        implicitHeight: body.implicitHeight + 20
        color: Theme.surface("#383838")
        radius: Theme.radiusSm
        RowLayout {
            id: body
            anchors.fill: parent; anchors.margins: 10; spacing: 10
            Text {
                text: action.modelData.status === "completed" ? "✓"
                      : action.modelData.status === "failed" ? "!"
                      : action.modelData.status === "running" ? "•" : "–"
                color: action.modelData.status === "completed" ? "#7dcea4"
                       : action.modelData.status === "failed" ? Theme.danger : Theme.accent
                font.pixelSize: 14
                Accessible.name: action.modelData.status
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: action.modelData.title
                    textFormat: Text.PlainText
                    color: Theme.textPrimary; font.pixelSize: Theme.fsMd
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: action.modelData.targets.length > 0
                    text: action.modelData.targets
                    textFormat: Text.PlainText
                    color: Theme.textSecondary; font.pixelSize: Theme.fsXs
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    text: root.showToolNames ? action.modelData.tool : action.modelData.status
                    textFormat: Text.PlainText
                    color: Theme.textMuted; font.pixelSize: Theme.fsXs
                    elide: Text.ElideRight
                }
            }
        }
    }
}
