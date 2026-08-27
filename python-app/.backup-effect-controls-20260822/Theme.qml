pragma Singleton
// qmllint disable

import QtQuick
import CutPro 1.0

// Central design tokens for the Cut Pro UI.
// Values transcribed from the reference React app (agent-app/src/index.css),
// the "Adobe Premiere Pro 2025" dark palette. Consumed via a relative
// directory import, e.g. `import "../theme"` then `Theme.accent`.
QtObject {
    id: theme

    readonly property real appearanceBrightness:
        Number(Backend.appSettings.appearanceBrightness || 50)
    readonly property real brightnessDelta:
        Math.max(-0.6, Math.min(0.6, (appearanceBrightness - 50) / 50))

    function surface(base) {
        return brightnessDelta >= 0
                ? Qt.lighter(base, 1 + brightnessDelta * 0.55)
                : Qt.darker(base, 1 + -brightnessDelta * 0.55)
    }

    // ---- Surfaces ------------------------------------------------------
    readonly property color bgPrimary: surface("#2b2b2b")
    readonly property color bgSidebar: surface("#323232")
    readonly property color bgPanel: surface("#323232")
    readonly property color bgTimeline: surface("#131313")
    readonly property color viewer: "#000000"      // monitor viewport

    // ---- Text ----------------------------------------------------------
    readonly property color textPrimary: "#e4e4e4"
    readonly property color textSecondary: "#b4b4b4"
    readonly property color textMuted: "#7a7a7a"

    // ---- Accents / lines ----------------------------------------------
    readonly property color accent: Backend.appSettings.accentColor || "#4b8ff5"
    readonly property color border: surface("#0d0d0d")
    readonly property color hover: surface("#333333")
    readonly property color danger: "#f87171"      // destructive actions (red-400)

    // ---- Media / clip colors ------------------------------------------
    readonly property color clipVideo: "#3f6a9e"
    readonly property color clipAudio: "#3f8a52"
    readonly property color clipSubtitle: "#e8c546"

    // ---- Audio meter gradient stops -----------------------------------
    readonly property color meterGreen: "#35c15a"
    readonly property color meterYellow: "#d6c437"
    readonly property color meterRed: "#d64444"

    // ---- Typography ----------------------------------------------------
    readonly property int fsXs: 10
    readonly property int fsSm: 11
    readonly property int fsMd: 12
    readonly property int fsLg: 13
    readonly property int fsXl: 18
    readonly property string monoFont: "Consolas"

    // ---- Radii ---------------------------------------------------------
    readonly property int radiusSm: 4
    readonly property int radiusMd: 6

    // ---- Bar heights ---------------------------------------------------
    readonly property int menuBarHeight: 44
    readonly property int panelTabHeight: 32
    readonly property int transportHeight: 40
    readonly property int rulerHeight: 24
    readonly property int trackHeadWidth: 164
    readonly property int toolRailWidth: 56
    readonly property int audioMetersWidth: 90
}
