import QtQuick

// Let the OS resize the frameless window, including all four corners.
Item {
    id: handles
    required property var ownerWindow
    readonly property int grip: 5
    enabled: ownerWindow.visibility === Window.Windowed
    visible: enabled

    Repeater {
        model: [Qt.TopEdge, Qt.BottomEdge, Qt.LeftEdge, Qt.RightEdge,
                Qt.TopEdge | Qt.LeftEdge, Qt.TopEdge | Qt.RightEdge,
                Qt.BottomEdge | Qt.LeftEdge, Qt.BottomEdge | Qt.RightEdge]

        delegate: MouseArea {
            required property int modelData
            readonly property bool leftEdge: (modelData & Qt.LeftEdge) !== 0
            readonly property bool rightEdge: (modelData & Qt.RightEdge) !== 0
            readonly property bool topEdge: (modelData & Qt.TopEdge) !== 0
            readonly property bool bottomEdge: (modelData & Qt.BottomEdge) !== 0
            readonly property bool corner: (leftEdge || rightEdge) && (topEdge || bottomEdge)

            x: rightEdge ? handles.width - width : corner || leftEdge ? 0 : handles.grip
            y: bottomEdge ? handles.height - height : corner || topEdge ? 0 : handles.grip
            width: leftEdge || rightEdge ? handles.grip : handles.width - handles.grip * 2
            height: topEdge || bottomEdge ? handles.grip : handles.height - handles.grip * 2
            acceptedButtons: Qt.LeftButton
            cursorShape: corner
                ? (topEdge === leftEdge ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                : leftEdge || rightEdge ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: handles.ownerWindow.startSystemResize(modelData)
        }
    }
}
