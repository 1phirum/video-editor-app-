import QtQuick

// Only empty title-bar space uses this, so toolbar clicks never move the window.
MouseArea {
    id: dragArea
    required property var ownerWindow
    signal toggleMaximized()

    property point pressPosition
    property bool moving: false

    enabled: ownerWindow.visibility !== Window.FullScreen
    acceptedButtons: Qt.LeftButton
    cursorShape: Qt.ArrowCursor
    onPressed: function(mouse) {
        pressPosition = Qt.point(mouse.x, mouse.y)
        moving = false
    }
    onPositionChanged: function(mouse) {
        if (!pressed || moving)
            return
        const distance = Math.abs(mouse.x - pressPosition.x)
                         + Math.abs(mouse.y - pressPosition.y)
        if (distance >= Qt.styleHints.startDragDistance)
            moving = ownerWindow.startSystemMove()
    }
    onReleased: moving = false
    onCanceled: moving = false
    onDoubleClicked: toggleMaximized()
}
