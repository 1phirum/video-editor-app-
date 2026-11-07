import QtQuick
import QtQuick.Templates as T

// Shared by all attached ToolTip labels. Preserve the existing light popup,
// text, timing and placement; only the background gains rounded corners.
T.ToolTip {
    id: control

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - 3
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    margins: 6
    padding: 6
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
                 | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font: control.font
        wrapMode: Text.Wrap
        color: control.palette.toolTipText
    }

    background: Rectangle {
        color: control.palette.toolTipBase
        border.color: control.palette.dark
        radius: 6
    }
}
