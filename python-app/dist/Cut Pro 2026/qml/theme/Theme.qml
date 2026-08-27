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
    readonly property color bgTimeline: surface('#504f4f')
    readonly property color viewer: surface("#3a3a3a")  // monitor viewport (Premiere-style grey)

    // ---- Text ----------------------------------------------------------
    readonly property color textPrimary: "#e4e4e4"
    readonly property color textSecondary: "#b4b4b4"
    readonly property color textMuted: "#7a7a7a"
    readonly property color placeholderText: "#ffffff"

    // ---- Accents / lines ----------------------------------------------
    readonly property color accent: Backend.appSettings.accentColor || "#4b8ff5"
    readonly property color border: surface("#0d0d0d")
    readonly property color hover: surface("#333333")
    readonly property color danger: "#f87171"      // destructive actions (red-400)

    // ---- Translation UI -----------------------------------------------
    // Shared Premiere-style surfaces for provider rows and the translation
    // chooser. Keeping these here avoids slightly different greys in each
    // translation component.
    readonly property color translationRow: surface("#3d3d3d")
    readonly property color translationRowHover: surface("#464646")
    readonly property color translationRowBorder: surface("#171717")
    readonly property color translationDialogSurface: surface("#353535")
    readonly property int translationDialogRadius: 10
    readonly property int translationRowRadius: 6
    readonly property int translationDialogWidth: 560

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

    // ---- Effect Controls tree ------------------------------------------
    // Premiere lays the panel out as a fixed row grid: every row is the same
    // height so the keyframe lanes on the right line up with the parameter
    // rows on the left, and each column starts at the same x on every row.
    readonly property int ecRowHeight: 26
    readonly property int ecHeaderHeight: 26
    readonly property color ecBand: surface("#464646")     // "Video" / "Audio"
    readonly property color ecRowLine: surface('#2b2b2b')  // row separators
    readonly property color ecRowHover: surface("#3b3b3b")
    readonly property color ecTrack: surface("#1f1f1f")    // slider groove
    readonly property color ecClipBand: "#2f6f7d"          // clip bar in lanes
    readonly property color ecValueText: Qt.lighter(accent, 1.25)
    readonly property int ecIndent: 18       // one hierarchy step
    readonly property int ecGutter: 14       // disclosure triangle cell
    readonly property int ecBadge: 16        // fx badge / stopwatch cell
    readonly property int ecValueWidth: 84   // value column
    readonly property int ecNavWidth: 56     // keyframe navigator column
    readonly property int ecResetWidth: 22   // per-row reset column

    // ---- Timeline clip density -----------------------------------------
    // A clip only earns its decorations once it is wide enough to read them.
    // Below these widths the extra layers stop being information and turn into
    // noise, which is what makes a zoomed-out timeline look smeared.
    readonly property int clipMinThumbWidth: 26     // narrower: colored bar only
    readonly property int clipMinLabelWidth: 46     // narrower: no name label
    readonly property int clipMinWaveformSpace: 30  // clip height needed to fit one
    readonly property int clipWaveformHeight: 20    // waveform strip on video clips
    readonly property int clipFilmstripCellPad: 1   // slot padding, one cell edge

    // ---- Bar heights ---------------------------------------------------
    readonly property int menuBarHeight: 44
    readonly property int panelTabHeight: 32
    readonly property int transportHeight: 40
    readonly property int rulerHeight: 24
    readonly property int trackHeadWidth: 164
    readonly property int toolRailWidth: 56
    readonly property int audioMetersWidth: 90
}
