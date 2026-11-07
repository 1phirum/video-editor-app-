#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "app/caption_voice_yin.h"

// When each word of a caption is actually spoken.
//
// Karaoke has to follow the voice: the highlight holds still while the speaker
// holds still and runs when they run. Three sources answer that, in this order,
// and all three end up as the same thing - one spoken span per word - so the
// monitor and the export ask one question and get one answer.
//
//   1. Whisper's word timestamps, carried on the cue as `wordTimes` and packed
//      by pack() below. Exact, and free: the transcriber computes them already
//      and until now threw them away.
//   2. The audio itself, for a cue that arrived without them - an imported SRT,
//      a hand-typed caption, a translated line. fromVoice() pitch-tracks the
//      window and lays the words onto the parts a voice is actually sounding
//      in, snapping each word to the syllable it starts on; fromEnvelope() is
//      the same idea read off loudness alone, for a build that cannot decode
//      samples.
//   3. Failing both, the cue split by how long each word takes to say.

//
// Nothing here knows about audio decoding or about the timeline: peaks and word
// lists come in, spans go out, which is what keeps it in cutpro_core where both
// renderers can reach it.
namespace CaptionWordClock {

// One word's spoken span, in ms from the start of the cue.
struct Span {
  double startMs = 0.0;
  double endMs = 0.0;
};

// "0,240 260,520 540,900" - ms from the cue start, one pair per word. A string
// rather than a list of maps because a transcript runs to tens of thousands of
// cues: this is a few bytes a word in the project file instead of a QVariantMap
// with two entries, which is what made the transcriber drop the timings.
QString pack(const QVector<Span> &spans);

// The inverse. Empty unless the text really has `wordCount` words: an edited or
// translated line no longer describes what was said, and lighting up the wrong
// words is worse than not following the voice at all. Spans are clamped into the
// cue and forced to run forwards.
QVector<Span> unpack(const QString &packed, int wordCount, double cueMs);

// The words laid onto the voiced parts of `peaks` - amplitudes spanning the cue
// in order, the array AudioPeakWindowService already caches for the timeline.
// Empty when there is no speech in the window to follow, so the caller can fall
// back rather than fit words to hiss.
QVector<Span> fromEnvelope(const QVector<float> &peaks, const QStringList &words,
                           double cueMs);

// The words laid onto pitch-tracked frames of the cue's audio, which is the
// accurate tier: loudness cannot tell a syllable from a snare, so over a music
// bed or a noisy room fromEnvelope() sees one continuous "voice" and divides the
// cue evenly - the very thing it exists to avoid. Periodicity can, and the
// pitch contour additionally marks where one syllable ends and the next begins,
// so each word's start is snapped to the syllable the speaker starts it on
// rather than interpolated between two of them.
//
// Frames come from CaptionVoiceYin::analyse() over exactly the cue's window, so
// frame times are already ms from the cue start. Empty when the window holds no
// voice, same contract as above.
QVector<Span> fromVoice(const QVector<CaptionVoiceYin::Frame> &frames,
                        const QStringList &words, double cueMs);


// The cue divided by how long each word takes to say. Always answers, and is
// what plays while a background analysis of the audio is still running.
QVector<Span> weighted(const QStringList &words, double cueMs);

// What the solver calls: the packed timings when they fit the text, the weighted
// split otherwise.
QVector<Span> spansFor(const QStringList &words, const QString &packed,
                       double cueMs);

// Roughly how long this word takes to say, relative to the others in its cue.
double weightOf(const QString &word);
}
