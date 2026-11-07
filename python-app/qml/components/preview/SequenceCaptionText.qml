import QtQuick
import CutPro 1.0
import "../subtitles"

AnimatedCaption {
    id: root
    property var cue: null
    text: cue ? cue.text : ""
    baseColor: Backend.captionTextColor
    fontFamily: Backend.captionFontFamily
    bold: Backend.captionBold
    italic: Backend.captionItalic
    maximumLineCount: 4
    horizontalAlignment: Backend.captionAlignment === "left" ? Text.AlignLeft
                         : Backend.captionAlignment === "right" ? Text.AlignRight : Text.AlignHCenter
    fallbackOutline: !Backend.captionBackgroundVisible
    elapsedMs: cue ? Math.max(0, Backend.playheadMs - cue.startMs) : 0
    cueDurationMs: cue ? Math.max(1, cue.durationMs) : 2000
    wordTimes: cue && cue.wordTimes ? cue.wordTimes : Backend.activeCaptionWordTimes
}
