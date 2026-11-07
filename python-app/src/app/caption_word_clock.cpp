#include "app/caption_word_clock.h"

#include <algorithm>
#include <cmath>

namespace CaptionWordClock {
namespace {

// Below this the window holds no speech at all, only the noise floor of the
// recording. Fitting words to hiss would make the highlight jitter.
constexpr double kSilentPeak = 0.02;
// Where the voice begins, between this window's own floor and its loudest
// column. Relative rather than absolute because a quiet interview and a loud
// vlog have to be read the same way.
constexpr double kVoiceFraction = 0.18;
// A dip shorter than this is inside a word - a stop consonant, a glottal catch -
// and must not stop the highlight.
constexpr double kBridgeMs = 90.0;
// Shorter than this is a click, a breath or a door, not a syllable.
constexpr double kMinRunMs = 55.0;
// A word is at most this many times the length of a one-letter word, so one very
// long word cannot eat the whole cue in the weighted split.
constexpr double kMaxWeight = 6.0;
// A pitch step this big is a new note or a new syllable rather than vibrato
// inside one: 150 cents is a tone and a half, which no speaker crosses without
// starting something new.
constexpr double kSyllableCents = 150.0;
// And a frame this much louder than the three before it is a fresh attack, which
// is how a syllable that repeats the same pitch is still found.
constexpr double kOnsetRise = 1.7;
// Two syllables cannot start closer together than this, spoken or sung. Without
// it the vibrato at the front of a held note reads as four syllables.
constexpr double kMinSyllableMs = 90.0;
// How far a word's estimated start may be moved to land on a syllable. Wider
// than this and a word gets pulled onto a syllable belonging to its neighbour,
// which is worse than the estimate it started from.
constexpr double kSnapMs = 130.0;


// A stretch of the cue the speaker is audible in.
struct Run {
  double startMs = 0.0;
  double endMs = 0.0;
};

// Where a run-relative amount of speaking time lands on the clock. Silences are
// skipped over, which is the whole point: a word whose share of the voice runs
// out after a pause ends after that pause.
double wallClock(const QVector<Run> &runs, double consumedMs) {
  double left = std::max(0.0, consumedMs);
  for (const Run &run : runs) {
    const double length = run.endMs - run.startMs;
    if (left <= length)
      return run.startMs + left;
    left -= length;
  }
  return runs.isEmpty() ? 0.0 : runs.last().endMs;
}

QVector<double> weightsFor(const QStringList &words, double *total) {
  QVector<double> weights;
  weights.reserve(words.size());
  double sum = 0.0;
  for (const QString &word : words) {
    const double weight = weightOf(word);
    weights.append(weight);
    sum += weight;
  }
  *total = sum > 0.0 ? sum : double(std::max(1, int(words.size())));
  return weights;
}

}  // namespace

double weightOf(const QString &word) {
  // Combining marks are not letters to a speaker: a Khmer consonant with two
  // vowel signs is one sound, so counting QChars would make it three times as
  // long as it is said.
  int letters = 0;
  for (const QChar ch : word)
    if (!ch.isMark() && !ch.isPunct() && !ch.isSpace())
      ++letters;
  double weight = 0.6 + 0.4 * letters;
  // Punctuation is time too. A comma is a breath and a full stop is a longer
  // one, and without this the highlight arrives at the next word before the
  // speaker does on every sentence boundary.
  if (!word.isEmpty()) {
    const QChar last = word.back();
    if (QStringLiteral(".?!…។៕。！？").contains(last))
      weight += 0.9;
    else if (QStringLiteral(",;:៖、，；：").contains(last))
      weight += 0.45;
  }
  return std::clamp(weight, 1.0, kMaxWeight);
}

QString pack(const QVector<Span> &spans) {
  QString out;
  out.reserve(spans.size() * 10);
  for (const Span &span : spans) {
    if (!out.isEmpty())
      out.append(QLatin1Char(' '));
    out.append(QString::number(qRound(std::max(0.0, span.startMs))));
    out.append(QLatin1Char(','));
    out.append(QString::number(qRound(std::max(span.startMs, span.endMs))));
  }
  return out;
}

QVector<Span> unpack(const QString &packed, int wordCount, double cueMs) {
  QVector<Span> spans;
  if (packed.isEmpty() || wordCount <= 0)
    return spans;
  const double limit = std::max(1.0, cueMs);
  const QStringView view(packed);
  spans.reserve(wordCount);
  int pos = 0;
  while (pos < view.size()) {
    while (pos < view.size() && view.at(pos) == QLatin1Char(' '))
      ++pos;
    if (pos >= view.size())
      break;
    const int comma = view.indexOf(QLatin1Char(','), pos);
    if (comma < 0)
      return {};
    int stop = view.indexOf(QLatin1Char(' '), comma);
    if (stop < 0)
      stop = view.size();
    bool startOk = false;
    bool endOk = false;
    const double startMs = view.sliced(pos, comma - pos).toDouble(&startOk);
    const double endMs =
        view.sliced(comma + 1, stop - comma - 1).toDouble(&endOk);
    if (!startOk || !endOk)
      return {};
    const double from = std::clamp(startMs, 0.0, limit);
    spans.append(Span{from, std::clamp(std::max(startMs, endMs), from, limit)});
    pos = stop;
    // One word more than the text has means the timings belong to something
    // else. Stop rather than walk a long malformed string.
    if (spans.size() > wordCount)
      return {};
  }
  if (spans.size() != wordCount)
    return {};
  return spans;
}

QVector<Span> weighted(const QStringList &words, double cueMs) {
  QVector<Span> spans;
  if (words.isEmpty())
    return spans;
  const double span = std::max(1.0, cueMs);
  double total = 0.0;
  const QVector<double> weights = weightsFor(words, &total);
  spans.reserve(words.size());
  double used = 0.0;
  for (int index = 0; index < words.size(); ++index) {
    const double startMs = span * used / total;
    used += weights.at(index);
    spans.append(Span{startMs, span * used / total});
  }
  return spans;
}

QVector<Span> fromEnvelope(const QVector<float> &peaks, const QStringList &words,
                           double cueMs) {
  QVector<Span> spans;
  if (peaks.isEmpty() || words.isEmpty())
    return spans;
  const double span = std::max(1.0, cueMs);
  const double columnMs = span / peaks.size();

  // The window's own floor and ceiling. A percentile rather than the minimum,
  // so one silent column in a loud window does not drag the threshold down.
  QVector<float> sorted = peaks;
  std::sort(sorted.begin(), sorted.end());
  const double loud = sorted.last();
  if (loud < kSilentPeak)
    return spans;
  const double quiet = sorted.at(int(sorted.size() * 0.2));
  const double threshold = quiet + (loud - quiet) * kVoiceFraction;

  QVector<bool> voiced(peaks.size());
  for (int index = 0; index < peaks.size(); ++index)
    voiced[index] = double(peaks.at(index)) >= threshold;

  // Close the gaps that are inside words before reading the runs off, or a
  // hard consonant would register as the speaker having stopped.
  const int bridge = std::max(1, int(std::lround(kBridgeMs / columnMs)));
  for (int index = 0; index < voiced.size(); ++index) {
    if (voiced.at(index))
      continue;
    int stop = index;
    while (stop < voiced.size() && !voiced.at(stop))
      ++stop;
    if (index > 0 && stop < voiced.size() && stop - index <= bridge)
      for (int fill = index; fill < stop; ++fill)
        voiced[fill] = true;
    index = stop;
  }

  QVector<Run> runs;
  for (int index = 0; index < voiced.size();) {
    if (!voiced.at(index)) {
      ++index;
      continue;
    }
    int stop = index;
    while (stop < voiced.size() && voiced.at(stop))
      ++stop;
    const Run run{index * columnMs, stop * columnMs};
    if (run.endMs - run.startMs >= kMinRunMs)
      runs.append(run);
    index = stop;
  }
  double voicedMs = 0.0;
  for (const Run &run : runs)
    voicedMs += run.endMs - run.startMs;
  // Nothing to follow. The caller falls back to the weighted split, which is
  // better than pinning every word to one short burst of sound.
  if (runs.isEmpty() || voicedMs < kMinRunMs)
    return spans;

  double total = 0.0;
  const QVector<double> weights = weightsFor(words, &total);
  spans.reserve(words.size());
  double used = 0.0;
  for (int index = 0; index < words.size(); ++index) {
    const double startMs = wallClock(runs, used);
    used += voicedMs * weights.at(index) / total;
    spans.append(Span{startMs, std::max(startMs, wallClock(runs, used))});
  }
  return spans;
}

QVector<Span> spansFor(const QStringList &words, const QString &packed,
                       double cueMs) {
  const QVector<Span> timed = unpack(packed, int(words.size()), cueMs);
  return timed.isEmpty() ? weighted(words, cueMs) : timed;
}

QVector<Span> fromVoice(const QVector<CaptionVoiceYin::Frame> &frames,
                        const QStringList &words, double cueMs) {
  QVector<Span> spans;
  if (frames.size() < 3 || words.isEmpty())
    return spans;
  const double span = std::max(1.0, cueMs);
  const double hop = std::max(1.0, frames.at(1).timeMs - frames.at(0).timeMs);

  QVector<bool> voiced(frames.size());
  for (int index = 0; index < frames.size(); ++index)
    voiced[index] = frames.at(index).voiced;

  // A stop consonant interrupts the periodicity as surely as it interrupts the
  // level, and it is inside a word either way.
  const int bridge = std::max(1, int(std::lround(kBridgeMs / hop)));
  for (int index = 0; index < voiced.size(); ++index) {
    if (voiced.at(index))
      continue;
    int stop = index;
    while (stop < voiced.size() && !voiced.at(stop))
      ++stop;
    if (index > 0 && stop < voiced.size() && stop - index <= bridge)
      for (int fill = index; fill < stop; ++fill)
        voiced[fill] = true;
    index = stop;
  }

  QVector<Run> runs;
  for (int index = 0; index < voiced.size();) {
    if (!voiced.at(index)) {
      ++index;
      continue;
    }
    int stop = index;
    while (stop < voiced.size() && voiced.at(stop))
      ++stop;
    const Run run{std::clamp(frames.at(index).timeMs, 0.0, span),
                  std::clamp(frames.at(stop - 1).timeMs + hop, 0.0, span)};
    if (run.endMs - run.startMs >= kMinRunMs)
      runs.append(run);
    index = stop;
  }
  double voicedMs = 0.0;
  for (const Run &run : runs)
    voicedMs += run.endMs - run.startMs;
  if (runs.isEmpty() || voicedMs < kMinRunMs)
    return spans;

  // Where a syllable starts: the opening of a voiced run, a jump in pitch, or a
  // fresh attack in level. These are the instants a word can actually begin on.
  QVector<double> onsets;
  double lastOnset = -kMinSyllableMs;
  for (int index = 0; index < voiced.size(); ++index) {
    if (!voiced.at(index))
      continue;
    const bool opens = index == 0 || !voiced.at(index - 1);
    bool attack = opens;
    if (!attack && index >= 3) {
      const CaptionVoiceYin::Frame &now = frames.at(index);
      double before = 0.0;
      for (int back = 1; back <= 3; ++back)
        before += frames.at(index - back).rms;
      before /= 3.0;
      const double previousF0 = frames.at(index - 1).f0Hz;
      if (before > 0.0 && now.rms >= before * kOnsetRise)
        attack = true;
      else if (now.f0Hz > 0.0 && previousF0 > 0.0 &&
               std::fabs(1200.0 * std::log2(now.f0Hz / previousF0)) >=
                   kSyllableCents)
        attack = true;
    }
    if (!attack)
      continue;
    const double at = std::clamp(frames.at(index).timeMs, 0.0, span);
    if (!opens && at - lastOnset < kMinSyllableMs)
      continue;
    onsets.append(at);
    lastOnset = at;
  }

  // The estimate first: each word's share of the voiced time, silences skipped.
  double total = 0.0;
  const QVector<double> weights = weightsFor(words, &total);
  QVector<double> starts;
  starts.reserve(words.size());
  double used = 0.0;
  for (int index = 0; index < words.size(); ++index) {
    starts.append(wallClock(runs, used));
    used += voicedMs * weights.at(index) / total;
  }

  // Then the correction: a word starts when a syllable starts. The estimate is
  // only ever moved onto a nearby onset, never past its neighbour's, and never
  // backwards - a highlight that goes back a word is worse than one that is a
  // few frames late.
  double previous = -1.0;
  for (int index = 0; index < starts.size(); ++index) {
    double best = starts.at(index);
    double nearest = kSnapMs;
    for (const double onset : onsets) {
      if (onset <= previous)
        continue;
      const double distance = std::fabs(onset - starts.at(index));
      if (distance < nearest) {
        nearest = distance;
        best = onset;
      }
    }
    starts[index] = std::max(best, previous + 1.0);
    previous = starts.at(index);
  }

  // A word stays lit until the next one is reached, so the ends are the next
  // start; the last word runs to the end of the speaking.
  spans.reserve(words.size());
  for (int index = 0; index < starts.size(); ++index) {
    const double startMs = std::clamp(starts.at(index), 0.0, span);
    const double endMs = index + 1 < starts.size()
                             ? starts.at(index + 1)
                             : std::min(span, runs.last().endMs);
    spans.append(Span{startMs, std::max(startMs, std::min(endMs, span))});
  }
  return spans;
}


}  // namespace CaptionWordClock
