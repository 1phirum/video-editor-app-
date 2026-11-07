#pragma once

#include <QVector>

// Pitch-tracked voicing, by the YIN method (de Cheveigné & Kawahara, 2002).
//
// The word clock's second tier used to read loudness alone, and loudness cannot
// tell a voice from a snare: over a music bed every column is loud, the voiced
// mask goes all-true, and the highlight degrades to an even split of the cue.
// A voice is *periodic*, and almost nothing else in a mix is periodic in the
// same band, so asking "does this 64 ms of audio repeat itself, and at what
// interval" separates a sung or spoken syllable from a cymbal, a room, or a
// synth pad that a level meter reads as identical.
//
// The five steps of the paper, of which the first four are here:
//
//   1. the squared difference function d(tau) over the analysis window;
//   2. d'(tau), the cumulative mean normalised difference - this is YIN's real
//      contribution, since it removes the "higher lags are always better" bias
//      that makes plain autocorrelation pick octaves below the true pitch;
//   3. the absolute threshold: the *first* dip below kThreshold rather than the
//      deepest one, which is what stops an octave error;
//   4. parabolic interpolation around that dip, so f0 is finer than one lag.
//
// Step five, the best local estimate, wants a search across neighbouring
// frames; a three-frame median over f0 afterwards buys the same stability for a
// fraction of the work and cannot pull a frame's own voicing decision around.
//
// d'(tau) at the chosen lag comes back as `aperiodicity`: 0 is a perfect
// repeat, 1 is noise. That number is the voicing test, and it is also what lets
// the caller distinguish a held note from a consonant.
//
// Samples in, frames out. No Qt objects, no audio decoding, no timeline - the
// same reason the word clock lives in cutpro_core: the monitor and the export
// both have to be able to reach it.
namespace CaptionVoiceYin {

// 16 kHz is the standard rate for voice pitch work: the band that carries a
// voice is under 4 kHz, and the cost of step 1 is proportional to the rate
// squared, so halving 32 to 16 is four times less work for no lost pitch.
constexpr int kSampleRate = 16000;
// 1024 samples. The window has to hold at least two periods of the lowest
// pitch: 70 Hz is 229 samples, so 1024 holds four and change.
constexpr int kWindowSamples = 1024;
constexpr double kHopMs = 10.0;
// A bass voice sits near 80 Hz, a sung soprano reaches 1 kHz. Outside this the
// answer is an instrument or an octave error, not a speaker.
constexpr double kMinimumHz = 70.0;
constexpr double kMaximumHz = 1000.0;
// The paper's own value. Lower finds fewer voiced frames and misses breathy
// speech; higher starts calling noise periodic.
constexpr double kThreshold = 0.15;
// A frame is a voice when its lag dip is at least this clean. Looser than
// kThreshold on purpose: a frame whose best dip is 0.3 is still a syllable
// under a loud mix, and the run rules downstream drop the strays.
constexpr double kVoicedAperiodicity = 0.42;

// One analysis frame.
struct Frame {
  double timeMs = 0.0;
  // 0 when the frame has no usable pitch.
  double f0Hz = 0.0;
  // d'(tau) at the chosen lag: 0 periodic, 1 noise.
  double aperiodicity = 1.0;
  double rms = 0.0;
  // Periodic, in band, and above the noise floor of this window.
  bool voiced = false;
};

// `samples` is mono; any rate is accepted and resampled to kSampleRate. The
// voice band is isolated first - a kick drum and a bass line are periodic too,
// and they are the two things most likely to be mistaken for a low voice.
QVector<Frame> analyse(const QVector<float> &samples, int sampleRate);

// Mono, kSampleRate, band-limited to the voice: what analyse() works on, split
// out so a caller that already holds prepared audio can skip the conversion.
QVector<float> prepare(const QVector<float> &samples, int sampleRate);

// Frames of prepared audio.
QVector<Frame> analysePrepared(const QVector<float> &samples);
}
