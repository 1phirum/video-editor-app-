// qmllint disable

import QtQuick
import QtQuick.Effects
import CutPro 1.0

// A caption drawn with a letter style and an animation, from numbers solved in
// C++. Backend.solveCaptionAnimation returns every glyph's opacity, scale and
// offset for one instant; this only draws them. The export paints the same
// solved values with QPainter, which is why the monitor and the file agree.
//
// Time comes in as `elapsedMs` rather than being run by QML Animations on
// purpose: the playhead is the clock, so scrubbing lands on the same frame the
// export would write, and a paused monitor shows a still of the animation
// instead of the finished caption.
Item {
    id: root

    property string text: ""
    property string styleId: Backend.captionTextEffect
    property string animationId: Backend.captionAnimation
    property real speed: Backend.captionAnimationSpeed
    property real elapsedMs: 0
    property real cueDurationMs: 2000
    property string fontFamily: Backend.captionFontFamily
    property real emSize: 42
    property bool bold: Backend.captionBold
    property bool italic: Backend.captionItalic
    property color baseColor: Backend.captionTextColor
    // Gallery previews are tiny and there are ~20 on screen at once. lowDetail
    // draws the block layout with the same cut-down layer stack the per-glyph
    // path already uses (fewer outline/glow copies, flat face instead of a
    // masked gradient) so opening the Text Effects tab doesn't build twenty
    // MultiEffect render targets in one go and stall the window. The monitor and
    // the export leave it false and keep full fidelity.
    property bool lowDetail: false
    property int horizontalAlignment: Text.AlignHCenter
    property int maximumLineCount: 4
    // Width the text is allowed to wrap at. Comes from the viewer rather than
    // from the caption box: the box hugs this item's implicit size, so reading
    // its width back here would be a loop.
    property real maxWidth: 0
    // The overlay's old behaviour: a caption with no background box got a thin
    // black outline so it stayed readable. Only used when the chosen style
    // brings no decoration of its own.
    property bool fallbackOutline: false
    // When each word of this cue was spoken, as "start,end start,end" in ms from
    // the cue start. Only the karaoke animations read it, and only to follow the
    // speaker instead of splitting the cue evenly. Left empty by a gallery card,
    // which has no cue behind it.
    property string wordTimes: ""

    readonly property var recipe: Backend.textEffectStyleFor(styleId)
    // The size the glyphs are actually drawn at. Whole pixels, because the word
    // widths below are measured in C++ at an integer size and the two have to be
    // measuring the same font.
    readonly property int emPx: Math.max(1, Math.round(emSize))
    // What is drawn: the words, the glyphs inside them, and how wide each word is
    // in this font. No time in it, so it is rebuilt when the text, the animation
    // or the font changes and not otherwise.
    readonly property var glyphLayout: Backend.captionGlyphLayout(
                                           text, animationId, fontFamily, emPx,
                                           bold, italic)
    // How it moves right now. A fresh object every frame by nature - it is the
    // numbers that changed - so nothing may use it as a Repeater model. Feeding a
    // Repeater a new list is what makes it destroy and re-create every delegate,
    // and on a 30 fps clock that rebuilt every offset copy of every letter of
    // every caption on screen thirty times a second.
    readonly property var solved: Backend.solveCaptionAnimation(
                                      text, elapsedMs, cueDurationMs,
                                      animationId, speed, wordTimes)

    readonly property var effectiveRecipe: {
        const r = recipe || ({})
        const bare = (r.outline || 0) <= 0 && (r.glow || 0) <= 0
                   && (r.extrude || 0) <= 0 && (r.shadowDx || 0) === 0
                   && (r.shadowDy || 0) === 0
        if (!bare || !fallbackOutline)
            return r
        const copy = JSON.parse(JSON.stringify(r))
        copy.outline = 0.045
        copy.outlineColor = "#000000"
        return copy
    }

    // Per-glyph drawing costs a stack of copies per letter, so a long caption
    // falls back to moving the block as one thing. A letter animation on sixty
    // letters is unreadable anyway - CapCut uses them on short punchy lines.
    readonly property int glyphCount: (glyphLayout && glyphLayout.glyphCount) || 0
    readonly property bool perGlyph: glyphLayout && glyphLayout.layout === "words"
                                     && glyphCount > 0 && glyphCount <= 72

    // Where a glyph sits when the solver has nothing for it - one frame while the
    // text is being edited, before the layout and the numbers agree again. A
    // shared constant, so reading it allocates nothing per frame.
    readonly property var restState: ({ opacity: 1, scale: 1, dx: 0, dy: 0,
                                        color: "" })
    // Same idea for the structure side: a word index that is briefly out of range
    // draws nothing instead of throwing out of a binding.
    readonly property var restWord: ({ text: "", space: false, glyphs: [],
                                       advance: 0 })

    // The gap between two words, from the same font metrics the word widths came
    // from, so what is packed below and what is drawn agree. TextMetrics only as
    // a fallback for a layout that was asked for without a font.
    readonly property real wordSpacing: (glyphLayout && glyphLayout.spaceAdvance > 0)
                                        ? glyphLayout.spaceAdvance
                                        : metrics.advanceWidth

    // Which words go on which line, packed against `maxWidth`. Rows as
    // {first, count} into the word list - the same object from frame to frame, so
    // this can be a Repeater model while `solved` cannot.
    //
    // Done here rather than by letting a Flow wrap the words because a Flow packs
    // every row against its left edge and has no notion of alignment at all,
    // which is why a centred caption's second line used to sit flush left.
    readonly property var glyphRows: {
        const words = (glyphLayout && glyphLayout.words) || []
        const gap = root.wordSpacing
        const limit = root.maxWidth > 0 ? root.maxWidth : Infinity
        const rows = []
        let first = 0
        let count = 0
        let width = 0
        for (let i = 0; i < words.length; ++i) {
            const advance = words[i].advance || 0
            const grown = count === 0 ? advance : width + gap + advance
            // A word that does not fit starts the next line - unless it is the
            // only one on this one, because moving it down would not help.
            if (count > 0 && grown > limit) {
                rows.push({ first: first, count: count })
                first = i
                count = 1
                width = advance
            } else {
                count += 1
                width = grown
            }
        }
        if (count > 0)
            rows.push({ first: first, count: count })
        return rows
    }

    // 0 packs a short line left, 1 right, 0.5 centres it. Kept as a number so the
    // row offset is one multiplication instead of a branch per row.
    readonly property real alignFactor: horizontalAlignment === Text.AlignRight ? 1
                                      : horizontalAlignment === Text.AlignLeft ? 0
                                                                               : 0.5

    // One glyph's numbers, found by its place in the layout. Both sides split the
    // text by the same rule in the same order, so the index is the whole
    // correspondence between them.
    function glyphMotion(wordIndex, glyphIndex) {
        const words = root.solved && root.solved.words
        if (!words || wordIndex < 0 || wordIndex >= words.length)
            return root.restState
        const glyphs = words[wordIndex].glyphs
        if (!glyphs || glyphIndex < 0 || glyphIndex >= glyphs.length)
            return root.restState
        return glyphs[glyphIndex]
    }

    // One word's structure, by the same index.
    function glyphWord(wordIndex) {
        const words = root.glyphLayout && root.glyphLayout.words
        if (!words || wordIndex < 0 || wordIndex >= words.length)
            return root.restWord
        return words[wordIndex]
    }

    // The whole-caption transform, used by the block layout. Present even when
    // the solver worked per glyph, so a binding never reads undefined.
    readonly property var blockState: (solved && solved.block)
                                      || ({ opacity: 1, scale: 1, dx: 0, dy: 0 })

    implicitWidth: perGlyph ? wordsLoader.implicitWidth : blockLoader.implicitWidth
    implicitHeight: perGlyph ? wordsLoader.implicitHeight : blockLoader.implicitHeight

    TextMetrics {
        id: metrics
        font.family: root.fontFamily
        font.pixelSize: root.emPx
        font.bold: root.bold
        font.italic: root.italic
        text: " "
    }

    // One run of text with the letter style painted around it. Offset copies
    // rather than shader passes: the export strokes the same layers with
    // QPainter, and a blur here would have no counterpart there.
    component StyledRun: Item {
        id: run
        property string content: ""
        property var recipe: ({})
        property real em: 42
        property color faceColor: "white"
        property color colorOverride: "transparent"
        property bool wrapped: false
        property real wrapWidth: 0
        property int maxLines: 0
        // Set for per-glyph drawing: fewer copies per layer, and a flat face
        // instead of a masked gradient, because there are up to 72 of these.
        property bool cheap: false

        // Wrapped, the painted extent is what the caption box should hug; on one
        // line the natural width is the same thing and cheaper to read.
        implicitWidth: run.wrapped && run.wrapWidth > 0 ? face.contentWidth
                                                        : face.implicitWidth
        implicitHeight: run.wrapped && run.wrapWidth > 0 ? face.contentHeight
                                                         : face.implicitHeight

        // The face is as wide as the wrap limit so Text can align its lines
        // against each other, but this item is only as wide as the text actually
        // drawn - so text aligned inside the face lands off to one side of the box
        // that hugs it. Shift it back by the slack, in the direction the alignment
        // pushed it, and a right-aligned caption sits at the box's right edge
        // instead of somewhere out past it.
        readonly property real faceShift: {
            if (!(run.wrapped && run.wrapWidth > 0))
                return 0
            const slack = face.width - face.contentWidth
            if (slack <= 0)
                return 0
            return root.horizontalAlignment === Text.AlignLeft ? 0
                 : root.horizontalAlignment === Text.AlignRight ? -slack
                                                                : -slack / 2
        }

        // Bottom to top: shadow, the extruded body, the glow, the outline. The
        // face is drawn after these, so it always lands on top.
        readonly property var layers: {
            const r = run.recipe || ({})
            const em = run.em
            const out = []
            const sdx = (r.shadowDx || 0) * em
            const sdy = (r.shadowDy || 0) * em
            const sblur = (r.shadowBlur || 0) * em
            const shadowColor = r.shadowColor || "#a6000000"
            if (sdx !== 0 || sdy !== 0 || sblur > 0) {
                out.push({ dx: sdx, dy: sdy, color: shadowColor, alpha: 1 })
                if (sblur > 0) {
                    out.push({ dx: sdx + sblur * 0.7, dy: sdy + sblur * 0.7,
                               color: shadowColor, alpha: 0.35 })
                    out.push({ dx: sdx - sblur * 0.5, dy: sdy + sblur * 0.6,
                               color: shadowColor, alpha: 0.25 })
                }
            }
            const ext = (r.extrude || 0) * em
            if (ext > 0) {
                const steps = Math.max(2, Math.min(run.cheap ? 4 : 8,
                                                   Math.round(ext / 1.5)))
                for (let i = steps; i >= 1; --i)
                    out.push({ dx: ext * i / steps * 0.75, dy: ext * i / steps,
                               color: r.extrudeColor || "#141414", alpha: 1 })
            }
            const glow = (r.glow || 0) * em
            if (glow > 0) {
                const rings = run.cheap ? 1 : 3
                for (let ring = rings; ring >= 1; --ring) {
                    const radius = glow * ring / rings
                    for (let k = 0; k < 8; ++k)
                        out.push({ dx: radius * Math.cos(k * Math.PI / 4),
                                   dy: radius * Math.sin(k * Math.PI / 4),
                                   color: r.glowColor || "#ffffff",
                                   alpha: run.cheap ? 0.22 : 0.14 })
                }
            }
            const ow = (r.outline || 0) * em
            if (ow > 0) {
                const copies = run.cheap ? 8 : 12
                for (let k = 0; k < copies; ++k)
                    out.push({ dx: ow * Math.cos(k * 2 * Math.PI / copies),
                               dy: ow * Math.sin(k * 2 * Math.PI / copies),
                               color: r.outlineColor || "#000000", alpha: 1 })
            }
            return out
        }

        Repeater {
            model: run.layers
            delegate: Text {
                required property var modelData
                x: run.faceShift + modelData.dx
                y: modelData.dy
                width: face.width
                text: run.content
                color: modelData.color
                opacity: modelData.alpha
                font: face.font
                wrapMode: face.wrapMode
                maximumLineCount: face.maximumLineCount
                horizontalAlignment: face.horizontalAlignment
                verticalAlignment: face.verticalAlignment
                elide: face.elide
            }
        }

        Text {
            id: face
            x: run.faceShift
            // implicitWidth is the unwrapped width, so this shrinks to the text
            // when it fits and only clamps when there is really wrapping to do.
            width: run.wrapped && run.wrapWidth > 0
                   ? Math.min(run.wrapWidth, implicitWidth) : implicitWidth
            text: run.content
            color: run.colorOverride.a > 0 ? run.colorOverride
                   : (run.recipe && run.recipe.fill) ? run.recipe.fill
                                                     : run.faceColor
            font.family: root.fontFamily
            font.pixelSize: run.em
            font.bold: root.bold
            font.italic: root.italic
            // WrapAtWordBoundaryOrAnywhere, not plain WordWrap: Chinese, Khmer and
            // other scripts write long runs with no spaces to break on, so plain
            // word-wrap left them one unbreakable line that spilled past the box.
            wrapMode: run.wrapped ? Text.Wrap : Text.NoWrap
            maximumLineCount: run.maxLines > 0 ? run.maxLines : 32
            horizontalAlignment: root.horizontalAlignment
            verticalAlignment: Text.AlignVCenter
            elide: run.maxLines > 0 ? Text.ElideRight : Text.ElideNone
            // The mask below covers this with the real ramp; drawing the first
            // stop here means a flat but right-coloured face if it ever fails.
            layer.enabled: gradientFace.visible
        }

        // A two-stop face. QML Text takes one colour, so the ramp is a gradient
        // rectangle masked by the glyphs. Only for the block layout: per glyph
        // this would be one offscreen surface per letter, so those get the flat
        // first stop and the export still writes the ramp.
        Rectangle {
            id: gradientFace
            anchors.fill: face
            visible: !run.cheap && run.recipe && run.recipe.fill2
                     && String(run.recipe.fill2).length > 0
                     && !(run.colorOverride.a > 0)
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: (run.recipe && run.recipe.fill) ? run.recipe.fill
                                                           : run.faceColor
                }
                GradientStop {
                    position: 1.0
                    color: (run.recipe && run.recipe.fill2) ? run.recipe.fill2
                                                            : run.faceColor
                }
            }
            layer.enabled: gradientFace.visible
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: face
            }
        }
    }

    // Animation "none", or one that moves the cue as a whole: a single wrapped
    // run, which is exactly what the overlay drew before any of this existed.
    Loader {
        id: blockLoader
        anchors.centerIn: parent
        active: !root.perGlyph
        visible: active
        sourceComponent: blockRun
    }

    Component {
        id: blockRun
        StyledRun {
            content: root.text
            recipe: root.effectiveRecipe
            em: root.emPx
            faceColor: root.baseColor
            wrapped: true
            wrapWidth: root.maxWidth
            maxLines: root.maximumLineCount
            cheap: root.lowDetail
            opacity: root.blockState.opacity
            scale: root.blockState.scale
            transform: Translate {
                x: root.blockState.dx * root.emSize
                y: root.blockState.dy * root.emSize
            }
        }
    }

    // Per word or per letter. Lines are packed in `glyphRows` and every glyph
    // carries its own transform, so a word is never cut in half and a short line
    // still follows the alignment setting.
    Loader {
        id: wordsLoader
        anchors.centerIn: parent
        active: root.perGlyph
        visible: active
        sourceComponent: wordsRun
    }

    Component {
        id: wordsRun
        Item {
            implicitWidth: lines.implicitWidth
            implicitHeight: lines.implicitHeight

            Column {
                id: lines
                // Left edge rather than centred: this item hugs the widest row, so
                // the two coincide, and a row is offset inside the caption's own
                // extent rather than inside the wrap limit.
                anchors.left: parent.left
                anchors.top: parent.top

                Repeater {
                    // The rows, never `solved`: this list is the same object from
                    // frame to frame, so these delegates are built once and every
                    // tick after that only moves them.
                    model: root.glyphRows
                    delegate: Row {
                        id: lineRow
                        required property var modelData
                        // Alignment from the widths that were really rendered, not
                        // from the metrics the packing used, so whatever the two
                        // disagree about the rows still line up with each other.
                        x: (lines.width - lineRow.width) * root.alignFactor
                        spacing: root.wordSpacing

                        Repeater {
                            model: lineRow.modelData.count
                            delegate: Row {
                                id: wordRow
                                required property int index
                                readonly property int wordIndex:
                                    lineRow.modelData.first + index
                                readonly property var word:
                                    root.glyphWord(wordRow.wordIndex)

                                Repeater {
                                    model: wordRow.word.glyphs
                                    delegate: StyledRun {
                                        required property var modelData
                                        required property int index
                                        // Not named `state`: Item already has one.
                                        readonly property var motion:
                                            root.glyphMotion(wordRow.wordIndex,
                                                             index)
                                        content: modelData
                                        recipe: root.effectiveRecipe
                                        em: root.emPx
                                        faceColor: root.baseColor
                                        // Karaoke hands back a colour per word;
                                        // the rest leave it empty and keep the
                                        // style's own fill.
                                        colorOverride: motion.color
                                                       && String(motion.color).length > 0
                                                       ? motion.color : "transparent"
                                        cheap: true
                                        opacity: motion.opacity
                                        scale: motion.scale
                                        transform: Translate {
                                            x: motion.dx * root.emSize
                                            y: motion.dy * root.emSize
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
