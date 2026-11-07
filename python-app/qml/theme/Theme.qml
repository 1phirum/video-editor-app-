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
    // 50 is neutral. 100 now maps to full brightness (delta 1.0); the lower end
    // keeps its -0.6 floor so 20% stays a readable dark, not pure black.
    readonly property real brightnessDelta:
        Math.max(-0.6, Math.min(1.0, (appearanceBrightness - 50) / 50))

    function surface(base) {
        // The lift is stronger going up than the dim going down: at full
        // brightness the greys should read as a genuinely light theme, so the
        // top end gets a wider multiplier than the -0.6 dark floor.
        return brightnessDelta >= 0
                ? Qt.lighter(base, 1 + brightnessDelta * 1.2)
                : Qt.darker(base, 1 + -brightnessDelta * 0.55)
    }

    // As the panels brighten the text goes the other way - toward white - so it
    // stays crisp on the lighter greys. 0 at neutral (keep the soft dark-theme
    // greys), 1 at full brightness (near white). White reads better here than
    // darkening the text to an ink did.
    readonly property real textLift: Math.max(0, brightnessDelta)

    // Linear blend between two colours. a and b must be colour values (the
    // readonly `color` endpoints below), not strings, so .r/.g/.b are defined.
    function mixColor(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t,
                       a.g + (b.g - a.g) * t,
                       a.b + (b.b - a.b) * t,
                       a.a + (b.a - a.a) * t)
    }

    // Text colour that brightens with the theme: the first endpoint is the
    // familiar dark-theme value, the second is where it lands at full brightness.
    // Qt.lighter(x, 1.0) coerces the hex strings to colour values so mixColor can
    // read their components.
    function ink(darkThemeColor, brightThemeColor) {
        return mixColor(Qt.lighter(darkThemeColor, 1.0),
                        Qt.lighter(brightThemeColor, 1.0), textLift)
    }

    // ---- Surfaces ------------------------------------------------------
    readonly property color bgPrimary: surface("#2b2b2b")
    readonly property color bgSidebar: surface("#323232")
    readonly property color bgPanel: surface("#323232")
    readonly property color bgTimeline: surface('#504f4f')
    readonly property color viewer: surface("#3a3a3a")  // monitor viewport (Premiere-style grey)

    // ---- Text ----------------------------------------------------------
    // Familiar soft greys at neutral brightness; they brighten toward white as
    // the panels lighten so nothing washes out. Muted lifts the most because its
    // old grey was the first to disappear into a lightened panel.
    readonly property color textPrimary: ink("#e4e4e4", "#ffffff")
    readonly property color textSecondary: ink("#b4b4b4", "#f2f2f2")
    readonly property color textMuted: ink("#7a7a7a", "#d2d2d2")
    readonly property color placeholderText: "#ffffff"

    // ---- Accents / lines ----------------------------------------------
    readonly property color accent: Backend.appSettings.accentColor || "#4b8ff5"
    readonly property color border: surface("#0d0d0d")
    readonly property color hover: surface("#333333")
    readonly property color danger: "#f87171"      // destructive actions (red-400)

    // ---- Right-click context menus -------------------------------------
    // The one place we deliberately break from the dark palette: right-click
    // menus are a light Premiere-style surface (fixed, so they do not ride the
    // appearanceBrightness dimmer like the panels do). Dark ink on a near-white
    // sheet reads faster for a dense command list than pale text on grey.
    readonly property color menuBg: "#ececec"
    readonly property color menuBorder: "#9a9a9a"
    readonly property color menuText: "#1c1c1c"
    readonly property color menuTextMuted: "#9b9b9b"      // disabled rows
    readonly property color menuHighlight: "#d2d2d2"      // hovered row pill
    readonly property color menuSeparator: "#c4c4c4"

    // ---- Cards and interior lines --------------------------------------
    // bgPanel and bgSidebar resolve to the same grey, so a header strip laid on
    // a panel is invisible and the only thing separating two surfaces is the
    // near-black `border` above. On a dense screen that reads as a mesh of
    // scratches rather than as structure. These give a content surface one step
    // of lift and its header strip one more, so tone does the separating and a
    // line is only ever a hairline. `border` stays the right colour where a
    // panel meets the window chrome.
    readonly property color bgCard: surface("#383838")        // content surface
    readonly property color bgCardHeader: surface("#3d3d3d")  // its title strip
    readonly property color divider: surface("#262626")       // inside one panel
    readonly property color outline: surface("#1c1c1c")        // around a panel or field

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
    // Effect-track bars. Purple so a bar reads as "not footage" at a glance,
    // the way the yellow subtitle row does.
    readonly property color clipEffect: "#7a5cc4"

    // Snap guide on the timeline. Yellow because the three lines it has to be
    // told apart from are already taken: blue is the playhead, red is a trim
    // handle, near-black is a butt joint between two clips.
    readonly property color snapGuide: "#f2c531"

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

    // ---- Timeline lanes -------------------------------------------------
    // A lane only gets its grey band once it actually holds clips, the way
    // CapCut does it: the occupied rows read as the sequence and the rest of
    // the canvas stays flat, so there is no phantom row to aim at.
    readonly property color laneOccupied: surface("#3b3b3b")
    readonly property color laneOccupiedLine: surface("#262626")

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
