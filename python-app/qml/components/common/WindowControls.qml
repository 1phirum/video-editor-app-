import QtQuick
import QtQuick.Controls
import "../../theme"

Row {
    id: controls
    required property var ownerWindow
    signal toggleMaximized()

    readonly property bool maximized: ownerWindow.visibility === Window.Maximized
    width: 138
    spacing: 0

    Repeater {
        model: ["minimize", "maximize", "close"]

        delegate: Button {
            id: control
            required property string modelData
            readonly property bool closeButton: modelData === "close"
            readonly property string label: modelData === "minimize" ? qsTr("Minimize")
                : closeButton ? qsTr("Close")
                : controls.maximized ? qsTr("Restore") : qsTr("Maximize")

            objectName: "window-" + modelData
            width: 46
            height: controls.height
            padding: 0
            flat: true
            hoverEnabled: true
            display: AbstractButton.IconOnly
            icon.source: "qrc:/assets/icons/window-"
                         + (modelData === "maximize" && controls.maximized
                            ? "restore" : modelData) + ".svg"
            icon.width: 12
            icon.height: 12
            icon.color: closeButton && (hovered || down) ? "#ffffff"
                : controls.ownerWindow.active || hovered ? Theme.textPrimary : Theme.textMuted

            Accessible.name: label
            ToolTip.visible: hovered
            ToolTip.delay: 700
            ToolTip.text: label

            background: Rectangle {
                color: control.closeButton
                    ? (control.down ? "#c42b1c" : control.hovered ? "#e81123" : "transparent")
                    : control.down ? Qt.rgba(1, 1, 1, 0.16)
                    : control.hovered ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
            }

            onClicked: {
                if (modelData === "minimize")
                    controls.ownerWindow.showMinimized()
                else if (modelData === "maximize")
                    controls.toggleMaximized()
                else
                    controls.ownerWindow.close()
            }
        }
    }
}
