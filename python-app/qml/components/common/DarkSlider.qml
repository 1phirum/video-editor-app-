//qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"

// Slider matching the app's colour-grading sliders: a thin dark track with an
// accent-filled portion up to the handle, and a small round handle. Replaces the
// default light Qt track/handle that otherwise clashes with the dark dialogs.
Slider {
    id: control
    implicitHeight: 24
    live: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 3
        radius: 1.5
        color: "#4a4a4a"
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 14
        height: 14
        radius: width / 2
        color: control.pressed ? "white" : Theme.textPrimary
        border.width: 1
        border.color: Theme.border
    }
}
