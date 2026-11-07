// qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"

// A checkbox whose box sits at the left edge and whose label starts exactly one
// gap past the box. The default control lets the style decide where the
// indicator lands, which on this app's style drifts under the label; pinning the
// indicator at x:0 and offsetting the text by the box width removes any overlap.
CheckBox {
    id: control
    padding: 0
    spacing: 8
    hoverEnabled: true
    implicitHeight: Math.max(18, contentItem.implicitHeight)

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    indicator: Rectangle {
        x: 0
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 16
        height: 16
        radius: 3
        color: control.checked ? Theme.accent : "transparent"
        border.width: 1
        border.color: control.checked
                      ? Theme.accent
                      : (control.hovered ? Theme.textSecondary : Theme.textMuted)

        Image {
            anchors.centerIn: parent
            source: "../../assets/icons/check.svg"
            sourceSize.width: 12
            sourceSize.height: 12
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        leftPadding: control.indicator.width + control.spacing
        color: control.enabled ? Theme.textSecondary : Theme.textMuted
        font.pixelSize: Theme.fsSm
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
