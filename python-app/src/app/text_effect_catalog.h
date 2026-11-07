#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Caption letter styles, caption animations, and the one solver that turns
// (text, animation, elapsed) into per-glyph transforms.
//
// Two renderers draw captions: QML Text items in the monitor overlay, and
// QPainter into the images the export overlays onto the video. Every number
// they need lives here and the timing is solved here, so what the monitor
// plays and what lands in the file cannot drift apart - the way three separate
// implementations of one curve did.
namespace TextEffectCatalog {

// Gallery order, "none" first. A style entry carries only numbers - see the
// implementation for the keys - so neither renderer has per-style code.
QVariantList styles();
QVariantList animations();

// Recipe by id, falling back to "none" for an id this build does not know, so
// a project written by a newer one still opens.
QVariantMap styleFor(const QString &id);
QVariantMap animationFor(const QString &id);

// Per-glyph state at `elapsedMs` into a cue lasting `cueDurationMs`.
//
//   layout  "block" (one wrapped run) or "words"
//   block   {opacity, scale, dx, dy} - only for layout == "block"
//   words   [{text, space, glyphs: [{text, opacity, scale, dx, dy, color}]}]
//
// dx and dy are in em, multiples of the font pixel size, so a 42 px preview
// and a 1080p export displace by the same proportion. `speed` divides every
// duration: 2.0 plays twice as fast.
//
// `wordTimes` is the cue's spoken word timing, packed by CaptionWordClock. Only
// the karaoke animations read it, and only to follow the speaker instead of
// splitting the cue evenly; an empty string still animates, just to the
// weighted estimate. `speed` is ignored by those animations - the voice sets
// the pace, and a speaker cannot be sped up.
QVariantMap solve(const QString &text, const QString &animationId, double speed,
                  double elapsedMs, double cueDurationMs,
                  const QString &wordTimes = QString());

// The half of solve() that does not move: which words, and which glyphs inside
// each of them, with no time in it at all.
//
//   layout      "block" or "words", matching what solve() will report
//   glyphCount  total glyphs across every word
//   words       [{text, space, glyphs: ["S", "a", ...], advance}]
//   spaceAdvance  the width of one space in the same font
//
// Separate from solve() for a reason that is not tidiness. A QML Repeater whose
// model is a freshly built list destroys and re-creates every delegate, and
// solve() returns a new list on every call by construction - it carries the
// numbers that changed. Feeding it to a Repeater on a 30 fps clock therefore
// rebuilt every offset copy of every letter of every caption on screen thirty
// times a second. The structure comes from here once, the numbers come from
// solve() per frame, and after the first frame nothing is created at all.
//
// The two advances are filled only when a pixel size is given, and they are what
// lets a renderer break a line and then align it. QML can measure a string, but
// only by writing into a TextMetrics and reading it back, and doing that inside
// the binding that lays the words out is a binding loop - which is why an
// animated caption was packed left by a Flow and ignored the alignment setting.
QVariantMap layoutFor(const QString &text, const QString &animationId,
                      const QString &fontFamily = QString(), int pixelSize = 0,
                      bool bold = false, bool italic = false);

// How long the animation needs before the cue is fully on screen, at `speed`.
// The export uses it to decide how many images a cue is worth.
double leadInMs(const QString &animationId, const QString &text, double speed,
                double cueDurationMs);

// Where the export has to write an image for this cue, in ms from its start,
// first entry always 0 and never more than `maxSteps` of them.
//
// Evenly across the lead-in for an animation that settles. On the word
// boundaries for a karaoke one: the highlight only changes when a word is
// spoken, so an evenly spaced image lands mid-word and moves the highlight at
// the wrong moment, while one image per word boundary is both correct and
// cheaper. When there are more words than the budget allows, boundaries are
// dropped evenly - some words then light up in pairs, which is what a step
// budget means.
QVector<double> stepTimes(const QString &animationId, const QString &text,
                          double speed, double cueDurationMs,
                          const QString &wordTimes, double stepMs,
                          int maxSteps);
}
