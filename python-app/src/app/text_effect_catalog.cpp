#include "app/text_effect_catalog.h"

#include <QFont>
#include <QFontMetricsF>
#include <QStringList>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <cmath>

#include "app/caption_word_clock.h"

namespace {

// Not M_PI: that one is a POSIX extension and disappears under a strict-ANSI
// build of the same compiler.
constexpr double kPi = 3.14159265358979323846;

// Every width, offset and radius below is in em - a multiple of the rendered
// font's pixel size - because the preview draws at ~42 px and the export at
// whatever the sequence height gives, and a style has to look the same in both.
QVariantMap styleEntry(const QString &id, const QString &label,
                       const QVariantMap &recipe) {
  QVariantMap m = recipe;
  m["id"] = id;
  m["label"] = label;
  // Defaults for everything the entry left out, so a renderer can read every
  // key without checking whether it is there.
  const QVariantMap defaults{{"fill", QString()},      // empty: user's colour
                             {"fill2", QString()},     // empty: flat fill
                             {"outline", 0.0},
                             {"outlineColor", QStringLiteral("#000000")},
                             {"shadowDx", 0.0},
                             {"shadowDy", 0.0},
                             {"shadowBlur", 0.0},
                             {"shadowColor", QStringLiteral("#a6000000")},
                             {"glow", 0.0},
                             {"glowColor", QStringLiteral("#ffffff")},
                             {"extrude", 0.0},
                             {"extrudeColor", QStringLiteral("#141414")}};
  for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
    if (!m.contains(it.key()))
      m.insert(it.key(), it.value());
  return m;
}

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

double easeOutCubic(double p) { return 1.0 - std::pow(1.0 - p, 3.0); }

// Settles past 1 and comes back, by `over` (0.06 == 6% overshoot).
double easeOutBack(double p, double over) {
  const double c = 1.70158 * (over > 0.0 ? over / 0.10 : 1.0);
  const double q = p - 1.0;
  return 1.0 + (c + 1.0) * q * q * q + c * q * q;
}

double easeOutBounce(double p) {
  const double n = 7.5625, d = 2.75;
  if (p < 1.0 / d)
    return n * p * p;
  if (p < 2.0 / d) {
    p -= 1.5 / d;
    return n * p * p + 0.75;
  }
  if (p < 2.5 / d) {
    p -= 2.25 / d;
    return n * p * p + 0.9375;
  }
  p -= 2.625 / d;
  return n * p * p + 0.984375;
}

// Words, each remembering whether a space followed it. Wrapping is per word in
// both renderers, so a letter animation still cannot break a word in half.
struct Word {
  QString text;
  bool space = false;
};

QList<Word> splitWords(const QString &text) {
  QList<Word> out;
  const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString &line : lines)
    for (const QString &w : line.split(QLatin1Char(' '), Qt::SkipEmptyParts))
      out.append(Word{w, true});
  if (!out.isEmpty())
    out.last().space = false;
  return out;
}

// User-perceived characters. QChar is not one of those: a Khmer consonant plus
// its vowel signs is a single letter to a reader and must animate as one.
QStringList clusters(const QString &text) {
  QStringList out;
  QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
  int from = 0;
  while (finder.toNextBoundary() > 0) {
    const int to = finder.position();
    if (to > from)
      out.append(text.mid(from, to - from));
    from = to;
  }
  if (from < text.size())
    out.append(text.mid(from));
  return out;
}

QVariantMap animEntry(const QString &id, const QString &label,
                      const QVariantMap &recipe) {
  QVariantMap m = recipe;
  m["id"] = id;
  m["label"] = label;
  const QVariantMap defaults{{"unit", QStringLiteral("cue")},  // cue|word|letter
                             {"kind", QStringLiteral("none")},
                             {"durationMs", 300.0},  // per unit
                             {"staggerMs", 0.0},     // between units
                             {"scaleFrom", 1.0},
                             {"overshoot", 0.0},
                             {"dx", 0.0},  // em, start offset
                             {"dy", 0.0},
                             {"highlightColor", QStringLiteral("#ffd54a")}};
  for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
    if (!m.contains(it.key()))
      m.insert(it.key(), it.value());
  return m;
}

}  // namespace

namespace TextEffectCatalog {

QVariantList styles() {
  return {
      styleEntry(QStringLiteral("none"), QStringLiteral("None"), {}),
      styleEntry(QStringLiteral("outline"), QStringLiteral("Outline"),
                 {{"outline", 0.09}}),
      styleEntry(QStringLiteral("shadow"), QStringLiteral("Drop Shadow"),
                 {{"shadowDx", 0.05}, {"shadowDy", 0.05}, {"shadowBlur", 0.04}}),
      styleEntry(QStringLiteral("extrude3d"), QStringLiteral("3D Extrude"),
                 {{"extrude", 0.10},
                  {"extrudeColor", QStringLiteral("#171717")},
                  {"outline", 0.035}}),
      styleEntry(QStringLiteral("neon"), QStringLiteral("Neon"),
                 {{"fill", QStringLiteral("#f2ffff")},
                  {"glow", 0.30},
                  {"glowColor", QStringLiteral("#24d7ff")},
                  {"outline", 0.045},
                  {"outlineColor", QStringLiteral("#0b5f74")}}),
      styleEntry(QStringLiteral("gold"), QStringLiteral("Gold"),
                 {{"fill", QStringLiteral("#fff6c2")},
                  {"fill2", QStringLiteral("#c07a06")},
                  {"outline", 0.065},
                  {"outlineColor", QStringLiteral("#2e1c00")},
                  {"shadowDx", 0.03},
                  {"shadowDy", 0.04}}),
      styleEntry(QStringLiteral("fire"), QStringLiteral("Fire"),
                 {{"fill", QStringLiteral("#ffe58a")},
                  {"fill2", QStringLiteral("#ff2f00")},
                  {"glow", 0.22},
                  {"glowColor", QStringLiteral("#ff6a00")},
                  {"outline", 0.05},
                  {"outlineColor", QStringLiteral("#3a0900")}}),
      styleEntry(QStringLiteral("chrome"), QStringLiteral("Chrome"),
                 {{"fill", QStringLiteral("#ffffff")},
                  {"fill2", QStringLiteral("#77869c")},
                  {"outline", 0.06},
                  {"outlineColor", QStringLiteral("#141c26")},
                  {"shadowDx", 0.02},
                  {"shadowDy", 0.04}}),
      styleEntry(QStringLiteral("sticker"), QStringLiteral("Sticker"),
                 {{"outline", 0.16},
                  {"outlineColor", QStringLiteral("#ffffff")},
                  {"shadowDx", 0.04},
                  {"shadowDy", 0.05},
                  {"shadowBlur", 0.05}}),
      styleEntry(QStringLiteral("retro"), QStringLiteral("Retro Pop"),
                 {{"fill", QStringLiteral("#fff1d6")},
                  {"extrude", 0.11},
                  {"extrudeColor", QStringLiteral("#ff4d79")},
                  {"outline", 0.03},
                  {"outlineColor", QStringLiteral("#2b0d18")}}),
      styleEntry(QStringLiteral("boldOutline"), QStringLiteral("Bold Outline"),
                 {{"outline", 0.14}}),
      styleEntry(QStringLiteral("cartoon"), QStringLiteral("Cartoon"),
                 {{"outline", 0.11},
                  {"outlineColor", QStringLiteral("#16110f")},
                  {"extrude", 0.055},
                  {"extrudeColor", QStringLiteral("#16110f")}}),
      styleEntry(QStringLiteral("softGlow"), QStringLiteral("Soft Glow"),
                 {{"glow", 0.24}, {"glowColor", QStringLiteral("#ffffff")}}),
      styleEntry(QStringLiteral("electric"), QStringLiteral("Electric"),
                 {{"fill", QStringLiteral("#f4feff")},
                  {"glow", 0.28},
                  {"glowColor", QStringLiteral("#2bb0ff")},
                  {"outline", 0.05},
                  {"outlineColor", QStringLiteral("#062b45")}}),
      styleEntry(QStringLiteral("ice"), QStringLiteral("Ice"),
                 {{"fill", QStringLiteral("#ffffff")},
                  {"fill2", QStringLiteral("#68c8ff")},
                  {"glow", 0.16},
                  {"glowColor", QStringLiteral("#9be8ff")},
                  {"outline", 0.04},
                  {"outlineColor", QStringLiteral("#10465f")}}),
      styleEntry(QStringLiteral("emerald"), QStringLiteral("Emerald"),
                 {{"fill", QStringLiteral("#dcffe9")},
                  {"fill2", QStringLiteral("#06a15c")},
                  {"outline", 0.05},
                  {"outlineColor", QStringLiteral("#04351f")},
                  {"shadowDx", 0.02},
                  {"shadowDy", 0.03}}),
      styleEntry(QStringLiteral("candy"), QStringLiteral("Candy"),
                 {{"fill", QStringLiteral("#fff0f6")},
                  {"fill2", QStringLiteral("#ff4f9a")},
                  {"outline", 0.06},
                  {"outlineColor", QStringLiteral("#5c0e2e")},
                  {"shadowDx", 0.025},
                  {"shadowDy", 0.035}}),
      styleEntry(QStringLiteral("grape"), QStringLiteral("Grape"),
                 {{"fill", QStringLiteral("#f3e6ff")},
                  {"fill2", QStringLiteral("#7b2ff7")},
                  {"glow", 0.18},
                  {"glowColor", QStringLiteral("#b07cff")},
                  {"outline", 0.04},
                  {"outlineColor", QStringLiteral("#1e0740")}}),
      styleEntry(QStringLiteral("royal"), QStringLiteral("Royal"),
                 {{"fill", QStringLiteral("#ece5ff")},
                  {"fill2", QStringLiteral("#3a1e8f")},
                  {"outline", 0.055},
                  {"outlineColor", QStringLiteral("#e8b64c")}}),
      styleEntry(QStringLiteral("steel"), QStringLiteral("Steel"),
                 {{"fill", QStringLiteral("#eef3f9")},
                  {"fill2", QStringLiteral("#4a5a6b")},
                  {"extrude", 0.06},
                  {"extrudeColor", QStringLiteral("#1a2129")},
                  {"outline", 0.03},
                  {"outlineColor", QStringLiteral("#0d1216")}}),
      styleEntry(QStringLiteral("honey"), QStringLiteral("Honey"),
                 {{"fill", QStringLiteral("#fff3c4")},
                  {"fill2", QStringLiteral("#e78b0a")},
                  {"glow", 0.12},
                  {"glowColor", QStringLiteral("#ffb62e")},
                  {"outline", 0.04},
                  {"outlineColor", QStringLiteral("#3d2400")}}),
      styleEntry(QStringLiteral("matrix"), QStringLiteral("Matrix"),
                 {{"fill", QStringLiteral("#b6ffb6")},
                  {"fill2", QStringLiteral("#00b52a")},
                  {"glow", 0.22},
                  {"glowColor", QStringLiteral("#35ff6a")},
                  {"outline", 0.04},
                  {"outlineColor", QStringLiteral("#052b0a")}}),
      styleEntry(QStringLiteral("longShadow"), QStringLiteral("Long Shadow"),
                 {{"extrude", 0.22},
                  {"extrudeColor", QStringLiteral("#2f2f2f")},
                  {"outline", 0.03},
                  {"outlineColor", QStringLiteral("#101010")}}),
      styleEntry(QStringLiteral("sunset"), QStringLiteral("Sunset"),
                 {{"fill", QStringLiteral("#ffd76e")},
                  {"fill2", QStringLiteral("#ff3d7f")},
                  {"outline", 0.045},
                  {"outlineColor", QStringLiteral("#3d0b23")},
                  {"shadowDx", 0.02},
                  {"shadowDy", 0.035}}),
      styleEntry(QStringLiteral("magma"), QStringLiteral("Magma"),
                 {{"fill", QStringLiteral("#fff1c9")},
                  {"fill2", QStringLiteral("#b3200a")},
                  {"glow", 0.18},
                  {"glowColor", QStringLiteral("#ff5a1f")},
                  {"outline", 0.035},
                  {"outlineColor", QStringLiteral("#2b0700")}}),
      styleEntry(QStringLiteral("mint"), QStringLiteral("Mint"),
                 {{"fill", QStringLiteral("#f2fffb")},
                  {"fill2", QStringLiteral("#12b886")},
                  {"outline", 0.05},
                  {"outlineColor", QStringLiteral("#06301f")}}),
      styleEntry(QStringLiteral("bubblegum"), QStringLiteral("Bubblegum"),
                 {{"fill", QStringLiteral("#fff0f6")},
                  {"fill2", QStringLiteral("#ff6ba8")},
                  {"outline", 0.09},
                  {"outlineColor", QStringLiteral("#7a1140")},
                  {"extrude", 0.05},
                  {"extrudeColor", QStringLiteral("#7a1140")}}),
      styleEntry(QStringLiteral("midnight"), QStringLiteral("Midnight"),
                 {{"fill", QStringLiteral("#cfd8ff")},
                  {"fill2", QStringLiteral("#1b1f4b")},
                  {"glow", 0.16},
                  {"glowColor", QStringLiteral("#4a63ff")}}),
      styleEntry(QStringLiteral("copper"), QStringLiteral("Copper"),
                 {{"fill", QStringLiteral("#ffe3c2")},
                  {"fill2", QStringLiteral("#a35a1e")},
                  {"outline", 0.045},
                  {"outlineColor", QStringLiteral("#3a1c07")},
                  {"extrude", 0.045},
                  {"extrudeColor", QStringLiteral("#3a1c07")}}),
      styleEntry(QStringLiteral("lime"), QStringLiteral("Lime"),
                 {{"fill", QStringLiteral("#f6ffd6")},
                  {"fill2", QStringLiteral("#7cb800")},
                  {"outline", 0.06},
                  {"outlineColor", QStringLiteral("#26330a")}}),
      // Dark letters in a white ring, for footage that is bright everywhere -
      // the one case a white caption cannot be rescued by a heavier outline.
      styleEntry(QStringLiteral("inverse"), QStringLiteral("Inverse"),
                 {{"fill", QStringLiteral("#14110f")},
                  {"outline", 0.12},
                  {"outlineColor", QStringLiteral("#ffffff")}}),
      styleEntry(QStringLiteral("graffiti"), QStringLiteral("Graffiti"),
                 {{"fill", QStringLiteral("#ffe94a")},
                  {"fill2", QStringLiteral("#ff7a00")},
                  {"outline", 0.1},
                  {"outlineColor", QStringLiteral("#1a1208")},
                  {"extrude", 0.07},
                  {"extrudeColor", QStringLiteral("#1a1208")}}),
  };
}

QVariantList animations() {
  return {
      animEntry(QStringLiteral("none"), QStringLiteral("None"),
                {{"durationMs", 0.0}}),
      animEntry(QStringLiteral("fadeIn"), QStringLiteral("Fade In"),
                {{"kind", QStringLiteral("fade")}, {"durationMs", 320.0}}),
      animEntry(QStringLiteral("popIn"), QStringLiteral("Pop In"),
                {{"kind", QStringLiteral("scale")},
                 {"durationMs", 360.0},
                 {"scaleFrom", 0.72},
                 {"overshoot", 0.07}}),
      animEntry(QStringLiteral("slideUp"), QStringLiteral("Slide Up"),
                {{"kind", QStringLiteral("slide")},
                 {"durationMs", 320.0},
                 {"dy", 0.85}}),
      animEntry(QStringLiteral("typewriter"), QStringLiteral("Typewriter"),
                {{"unit", QStringLiteral("letter")},
                 {"kind", QStringLiteral("reveal")},
                 {"durationMs", 1.0},
                 {"staggerMs", 42.0}}),
      animEntry(QStringLiteral("letterPop"), QStringLiteral("Letter Pop"),
                {{"unit", QStringLiteral("letter")},
                 {"kind", QStringLiteral("scale")},
                 {"durationMs", 240.0},
                 {"staggerMs", 34.0},
                 {"scaleFrom", 0.3},
                 {"overshoot", 0.10}}),
      animEntry(QStringLiteral("letterBounce"), QStringLiteral("Letter Bounce"),
                {{"unit", QStringLiteral("letter")},
                 {"kind", QStringLiteral("bounce")},
                 {"durationMs", 320.0},
                 {"staggerMs", 34.0},
                 {"dy", -0.55}}),
      animEntry(QStringLiteral("wordFade"), QStringLiteral("Word Fade"),
                {{"unit", QStringLiteral("word")},
                 {"kind", QStringLiteral("fade")},
                 {"durationMs", 220.0},
                 {"staggerMs", 90.0}}),
      animEntry(QStringLiteral("wordRise"), QStringLiteral("Word Rise"),
                {{"unit", QStringLiteral("word")},
                 {"kind", QStringLiteral("slide")},
                 {"durationMs", 260.0},
                 {"staggerMs", 80.0},
                 {"dy", 0.5}}),
      animEntry(QStringLiteral("dropIn"), QStringLiteral("Drop In"),
                {{"kind", QStringLiteral("slide")},
                 {"durationMs", 340.0},
                 {"dy", -0.9}}),
      animEntry(QStringLiteral("punchIn"), QStringLiteral("Punch In"),
                {{"kind", QStringLiteral("scale")},
                 {"durationMs", 300.0},
                 {"scaleFrom", 1.4}}),
      animEntry(QStringLiteral("slideLeft"), QStringLiteral("Slide Left"),
                {{"kind", QStringLiteral("slide")},
                 {"durationMs", 320.0},
                 {"dx", 1.3}}),
      animEntry(QStringLiteral("slideRight"), QStringLiteral("Slide Right"),
                {{"kind", QStringLiteral("slide")},
                 {"durationMs", 320.0},
                 {"dx", -1.3}}),
      animEntry(QStringLiteral("wordPop"), QStringLiteral("Word Pop"),
                {{"unit", QStringLiteral("word")},
                 {"kind", QStringLiteral("scale")},
                 {"durationMs", 260.0},
                 {"staggerMs", 85.0},
                 {"scaleFrom", 0.55},
                 {"overshoot", 0.09}}),
      animEntry(QStringLiteral("wordBounce"), QStringLiteral("Word Bounce"),
                {{"unit", QStringLiteral("word")},
                 {"kind", QStringLiteral("bounce")},
                 {"durationMs", 340.0},
                 {"staggerMs", 90.0},
                 {"dy", -0.5}}),
      animEntry(QStringLiteral("letterFade"), QStringLiteral("Letter Fade"),
                {{"unit", QStringLiteral("letter")},
                 {"kind", QStringLiteral("fade")},
                 {"durationMs", 200.0},
                 {"staggerMs", 30.0}}),
      animEntry(QStringLiteral("letterDrop"), QStringLiteral("Letter Drop"),
                {{"unit", QStringLiteral("letter")},
                 {"kind", QStringLiteral("slide")},
                 {"durationMs", 240.0},
                 {"staggerMs", 30.0},
                 {"dy", -0.7}}),
      // The karaoke family. Every one of these follows the speaker rather than
      // the clock - see the highlight branch of solve() - so they hold still
      // while the speaker holds still and run when the speaker runs.
      animEntry(QStringLiteral("karaoke"), QStringLiteral("Karaoke"),
                {{"unit", QStringLiteral("word")},
                 {"kind", QStringLiteral("highlight")}}),
      animEntry(QStringLiteral("karaokePop"), QStringLiteral("Karaoke Pop"),
                {{"unit", QStringLiteral("word")},
                 {"kind", QStringLiteral("highlightPop")}}),
      animEntry(QStringLiteral("karaokeRise"), QStringLiteral("Karaoke Rise"),
                {{"unit", QStringLiteral("word")},
                 {"kind", QStringLiteral("highlightRise")}}),
      animEntry(QStringLiteral("karaokeFocus"), QStringLiteral("Karaoke Focus"),
                {{"unit", QStringLiteral("word")},
                 {"kind", QStringLiteral("highlightOne")}}),
  };
}

QVariantMap styleFor(const QString &id) {
  const QVariantList all = styles();
  for (const QVariant &v : all) {
    const QVariantMap m = v.toMap();
    if (m.value(QStringLiteral("id")).toString() == id)
      return m;
  }
  return all.first().toMap();
}

QVariantMap animationFor(const QString &id) {
  const QVariantList all = animations();
  for (const QVariant &v : all) {
    const QVariantMap m = v.toMap();
    if (m.value(QStringLiteral("id")).toString() == id)
      return m;
  }
  return all.first().toMap();
}

// One unit's state at normalised progress `p`. Both renderers get this already
// solved; neither knows what an easing curve is.
static QVariantMap unitState(const QVariantMap &a, const QString &kind,
                             double p) {
  double opacity = 1.0, scale = 1.0, dx = 0.0, dy = 0.0;
  const double from = a.value(QStringLiteral("scaleFrom")).toDouble();
  const double over = a.value(QStringLiteral("overshoot")).toDouble();
  const double ax = a.value(QStringLiteral("dx")).toDouble();
  const double ay = a.value(QStringLiteral("dy")).toDouble();

  if (kind == QLatin1String("fade")) {
    opacity = easeOutCubic(p);
  } else if (kind == QLatin1String("scale")) {
    const double e = over > 0.0 ? easeOutBack(p, over) : easeOutCubic(p);
    scale = from + (1.0 - from) * e;
    // Ramps in over the first part of the move: a glyph that faded for the whole
    // duration reads as blurred rather than as arriving.
    opacity = clamp01(p * 2.5);
  } else if (kind == QLatin1String("slide")) {
    const double e = easeOutCubic(p);
    dx = ax * (1.0 - e);
    dy = ay * (1.0 - e);
    opacity = clamp01(p * 2.0);
  } else if (kind == QLatin1String("reveal")) {
    // Typewriter: a letter is either typed or it is not.
    opacity = p >= 1.0 ? 1.0 : 0.0;
  } else if (kind == QLatin1String("bounce")) {
    dy = ay * (1.0 - easeOutBounce(p));
    opacity = clamp01(p * 3.0);
  }
  return QVariantMap{{QStringLiteral("opacity"), opacity},
                     {QStringLiteral("scale"), scale},
                     {QStringLiteral("dx"), dx},
                     {QStringLiteral("dy"), dy}};
}

QVariantMap solve(const QString &text, const QString &animationId, double speed,
                  double elapsedMs, double cueDurationMs,
                  const QString &wordTimes) {
  const QVariantMap a = animationFor(animationId);
  const QString kind = a.value(QStringLiteral("kind")).toString();
  const QString unit = a.value(QStringLiteral("unit")).toString();
  const double sp = speed > 0.05 ? speed : 1.0;
  const double dur =
      std::max(1.0, a.value(QStringLiteral("durationMs")).toDouble() / sp);
  const double stagger =
      std::max(0.0, a.value(QStringLiteral("staggerMs")).toDouble() / sp);

  // A cue animation moves the wrapped block as one thing; anything finer moves
  // each unit. Both forms fill words[], so a renderer always has a layout to
  // walk and only has to decide where to hang the transform.
  const bool whole = kind == QLatin1String("none") ||
                     unit == QLatin1String("cue") || text.trimmed().isEmpty();
  const QVariantMap block =
      whole ? unitState(a, kind, clamp01(elapsedMs / dur))
            : unitState(a, QStringLiteral("static"), 1.0);

  const QList<Word> words = splitWords(text);
  // Karaoke runs on speaker time. The spans say when each word is spoken -
  // Whisper's own word timestamps when the cue was transcribed, the shape of
  // the audio when it was imported, an estimate from word lengths when there is
  // neither - so a pause in the delivery is a pause in the highlight and fast
  // speech is a fast highlight. Nothing here divides the cue evenly any more,
  // and nothing here looks at `speed`: the voice already set it.
  const bool karaoke = kind.startsWith(QLatin1String("highlight"));
  QVector<CaptionWordClock::Span> spans;
  if (karaoke) {
    QStringList plain;
    plain.reserve(int(words.size()));
    for (const Word &w : words)
      plain.append(w.text);
    spans = CaptionWordClock::spansFor(plain, wordTimes, cueDurationMs);
  }
  const QString highlightColor =
      a.value(QStringLiteral("highlightColor")).toString();

  QVariantList wordList;
  int ordinal = 0;  // clusters for a letter animation, words otherwise
  for (int wi = 0; wi < words.size(); ++wi) {
    const Word &w = words.at(wi);
    QVariantList glyphs;
    if (whole) {
      QVariantMap g = block;
      g[QStringLiteral("text")] = w.text;
      g[QStringLiteral("color")] = QString();
      glyphs.append(g);
    } else if (karaoke) {
      const CaptionWordClock::Span span = spans.value(wi);
      const double heard = std::max(1.0, span.endMs - span.startMs);
      // How far into being said this word is: below 0 the speaker has not
      // reached it, above 1 they have left it behind.
      const double said = (elapsedMs - span.startMs) / heard;
      const double p = clamp01(said);
      // Swells as the word is said and settles back, so the movement marks the
      // word being spoken instead of accumulating down the line.
      const double emphasis = p > 0.0 && said < 1.0 ? std::sin(kPi * p) : 0.0;
      QVariantMap g = unitState(a, QStringLiteral("static"), 1.0);
      // Past the end of the cue every word has been said. The export asks for
      // that state to lay a cue out and burns it in on its own when the
      // transcript is too long to animate, so it has to read as a finished
      // line rather than as one nobody has reached yet.
      const bool past = elapsedMs >= cueDurationMs;
      const bool current = (said >= 0.0 && said < 1.0) || past;
      const bool spoken = said >= 0.0;
      if (kind == QLatin1String("highlightPop")) {
        g[QStringLiteral("scale")] = 1.0 + 0.16 * emphasis;
      } else if (kind == QLatin1String("highlightRise")) {
        g[QStringLiteral("dy")] = -0.18 * emphasis;
      } else if (kind == QLatin1String("highlightOne")) {
        // Only the word being said is lit, and the rest of the line steps back
        // rather than disappearing - the reader still needs to see what is
        // coming.
        g[QStringLiteral("opacity")] = current ? 1.0 : 0.55;
        g[QStringLiteral("scale")] = 1.0 + 0.1 * emphasis;
      }
      g[QStringLiteral("text")] = w.text;
      const bool lit =
          kind == QLatin1String("highlightOne") ? current : spoken;
      g[QStringLiteral("color")] = lit ? highlightColor : QString();
      glyphs.append(g);
    } else if (unit == QLatin1String("letter")) {
      // Grapheme clusters, not QChars: a Khmer or Devanagari letter is several
      // code points and splitting inside one would animate its marks away from
      // the consonant they belong to.
      for (const QString &cluster : clusters(w.text)) {
        QVariantMap g =
            unitState(a, kind, clamp01((elapsedMs - ordinal * stagger) / dur));
        g[QStringLiteral("text")] = cluster;
        g[QStringLiteral("color")] = QString();
        glyphs.append(g);
        ++ordinal;
      }
      if (w.space)
        ++ordinal;  // the space is typed too, or the rhythm goes wrong
    } else {
      QVariantMap g =
          unitState(a, kind, clamp01((elapsedMs - wi * stagger) / dur));
      g[QStringLiteral("text")] = w.text;
      g[QStringLiteral("color")] = QString();
      glyphs.append(g);
    }
    wordList.append(QVariantMap{{QStringLiteral("text"), w.text},
                                {QStringLiteral("space"), w.space},
                                {QStringLiteral("glyphs"), glyphs}});
  }

  QVariantMap out;
  out[QStringLiteral("layout")] =
      whole ? QStringLiteral("block") : QStringLiteral("words");
  out[QStringLiteral("block")] = block;
  out[QStringLiteral("words")] = wordList;
  return out;
}

QVariantMap layoutFor(const QString &text, const QString &animationId,
                      const QString &fontFamily, int pixelSize, bool bold,
                      bool italic) {
  const QVariantMap a = animationFor(animationId);
  const QString kind = a.value(QStringLiteral("kind")).toString();
  const QString unit = a.value(QStringLiteral("unit")).toString();
  // The same three decisions solve() makes, in the same order, so an index into
  // one is an index into the other. Anything else here and a glyph would read
  // another glyph's state.
  const bool whole = kind == QLatin1String("none") ||
                     unit == QLatin1String("cue") || text.trimmed().isEmpty();
  const bool karaoke = kind.startsWith(QLatin1String("highlight"));
  const bool letters = !whole && !karaoke && unit == QLatin1String("letter");

  // Only when the caller means to lay the words out itself. Building the metrics
  // means opening the font, which is not worth doing for a caller that only
  // wants to know how many glyphs there are.
  const bool measure = pixelSize > 0;
  QFont font(fontFamily);
  if (measure) {
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    font.setItalic(italic);
  }
  const QFontMetricsF metrics(font);

  QVariantList wordList;
  int total = 0;
  for (const Word &w : splitWords(text)) {
    QVariantList glyphs;
    if (letters) {
      for (const QString &cluster : clusters(w.text))
        glyphs.append(cluster);
    } else {
      glyphs.append(w.text);
    }
    total += int(glyphs.size());
    wordList.append(QVariantMap{
        {QStringLiteral("text"), w.text},
        {QStringLiteral("space"), w.space},
        {QStringLiteral("glyphs"), glyphs},
        {QStringLiteral("advance"),
         measure ? metrics.horizontalAdvance(w.text) : 0.0}});
  }

  QVariantMap out;
  out[QStringLiteral("layout")] =
      whole ? QStringLiteral("block") : QStringLiteral("words");
  out[QStringLiteral("glyphCount")] = total;
  out[QStringLiteral("spaceAdvance")] =
      measure ? metrics.horizontalAdvance(QStringLiteral(" ")) : 0.0;
  out[QStringLiteral("words")] = wordList;
  return out;
}

double leadInMs(const QString &animationId, const QString &text, double speed,
                double cueDurationMs) {
  const QVariantMap a = animationFor(animationId);
  const QString kind = a.value(QStringLiteral("kind")).toString();
  if (kind == QLatin1String("none"))
    return 0.0;
  if (kind.startsWith(QLatin1String("highlight")))
    return cueDurationMs;  // never settles: it moves for the whole cue

  const double sp = speed > 0.05 ? speed : 1.0;
  const double dur = a.value(QStringLiteral("durationMs")).toDouble() / sp;
  const double stagger = a.value(QStringLiteral("staggerMs")).toDouble() / sp;
  const QString unit = a.value(QStringLiteral("unit")).toString();

  int units = 1;
  if (unit == QLatin1String("word")) {
    units = std::max(1, int(splitWords(text).size()));
  } else if (unit == QLatin1String("letter")) {
    units = 0;
    for (const Word &w : splitWords(text))
      units += int(clusters(w.text).size()) + (w.space ? 1 : 0);
    units = std::max(1, units);
  }
  return dur + stagger * (units - 1);
}

QVector<double> stepTimes(const QString &animationId, const QString &text,
                          double speed, double cueDurationMs,
                          const QString &wordTimes, double stepMs,
                          int maxSteps) {
  const int budget = std::max(1, maxSteps);
  const double cueMs = std::max(1.0, cueDurationMs);
  const double lead = std::min(leadInMs(animationId, text, speed, cueMs), cueMs);
  if (lead <= 1.0)
    return {0.0};

  const QVariantMap a = animationFor(animationId);
  if (a.value(QStringLiteral("kind")).toString().startsWith(
          QLatin1String("highlight"))) {
    const QList<Word> words = splitWords(text);
    QStringList plain;
    plain.reserve(int(words.size()));
    for (const Word &w : words)
      plain.append(w.text);
    const QVector<CaptionWordClock::Span> spans =
        CaptionWordClock::spansFor(plain, wordTimes, cueMs);
    QVector<double> starts;
    starts.append(0.0);
    for (const CaptionWordClock::Span &span : spans) {
      // Within a frame of the previous boundary is the same boundary: two words
      // said together are worth one image, not two identical ones.
      if (span.startMs > starts.last() + 20.0 && span.startMs < cueMs - 20.0)
        starts.append(span.startMs);
    }
    if (starts.size() <= budget)
      return starts;
    // Over budget. Keep the first and then thin the rest evenly, so the images
    // that survive are still spread across the line.
    QVector<double> kept;
    kept.reserve(budget);
    for (int index = 0; index < budget; ++index)
      kept.append(starts.at(int(qint64(index) * starts.size() / budget)));
    return kept;
  }

  const int steps =
      std::clamp(int(std::ceil(lead / std::max(1.0, stepMs))), 2, budget);
  QVector<double> starts;
  starts.reserve(steps);
  for (int step = 0; step < steps; ++step)
    starts.append(lead * step / steps);
  return starts;
}

}  // namespace TextEffectCatalog
