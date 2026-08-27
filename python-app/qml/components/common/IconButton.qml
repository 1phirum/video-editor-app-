//qmllint disable
import QtQuick
import QtQuick.Controls
import "../../theme"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

// A flat, square icon button with Premiere-style press/active states.
// Pass `iconName` (a lucide file stem in assets/icons); the SVG is recolored
// via `icon.color`, so the same asset serves muted / active tints.
Button {
    id: control

    property string iconName: ""
    property int boxSize: 28
    property int glyphSize: 15
    property bool active: false
    property color activeColor: Theme.accent
    // Base (non-hover, non-active) icon tint. Changed to primary
    // for a bright resting state, matching Premiere Pro exactly.
    property color restColor: Theme.textPrimary
    property color hoverColor: "#ffffff"
    // Adobe's compact tool rail uses a flat cell and colored glyph instead
    // of the rounded active button used elsewhere in the application.
    property bool adobeStyle: false

    implicitWidth: boxSize
    implicitHeight: boxSize
    padding: 0
    flat: true
    hoverEnabled: false
    display: AbstractButton.IconOnly

    HoverHandler {
        id: iconHover
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    icon.source: iconName === "" ? "" : "../../assets/icons/" + iconName + ".svg"
    icon.width: glyphSize
    icon.height: glyphSize
    icon.color: adobeStyle
                 ? (active ? Theme.accent : iconHover.hovered ? hoverColor : restColor)
                 : (active ? "white" : iconHover.hovered ? hoverColor : restColor)

    background: Rectangle {
        radius: control.adobeStyle ? 0 : Theme.radiusMd
        color: control.adobeStyle
               ? (control.active ? "#303030"
                  : control.down ? Qt.rgba(1, 1, 1, 0.08)
                  : "transparent")
               : (control.active ? control.activeColor
                  : control.down ? Qt.darker(Theme.hover, 1.3)
                  : "transparent")
    }
}
