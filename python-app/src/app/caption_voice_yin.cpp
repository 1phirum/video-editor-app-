#include "app/caption_voice_yin.h"

#include <algorithm>
#include <cmath>

namespace CaptionVoiceYin {
namespace {

// Not M_PI: that one is a POSIX extension and disappears under a strict-ANSI
// build, which is how this file would stop compiling on someone else's toolchain.
constexpr double kPi = 3.14159265358979323846;

// Below this a window holds no voice at any threshold, only the noise floor of
// the recording, and everything downstream should say so rather than fit words
// to hiss.
constexpr double kSilenceRms = 0.004;
// Where the noise floor of this particular window sits, between its own quiet
// fifth and its loudest frame. Relative because a whispered vlog and a mastered
// music bed have to be read the same way.
constexpr double kNoiseFraction = 0.12;
// The band a voice lives in. The low corner is the important one: a kick drum
// and a bass line are periodic, and without it they are the two things most
// often mistaken for a low voice.
constexpr double kVoiceLowHz = 150.0;
constexpr double kVoiceHighHz = 3400.0;

// Direct form 1, the RBJ cookbook coefficients. Two of these are cheaper than
// one FFT and there is nothing here that wants the spectrum.
struct Biquad {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
  double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

  float run(float in) {
    const double x0 = double(in);
    const double y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x0;
    y2 = y1;
    y1 = y0;
    return float(y0);
  }
};

// Butterworth Q: flat to the corner, no ringing to invent periodicity.
Biquad shape(double hz, int rate, bool high) {
  Biquad filter;
  const double w = 2.0 * kPi * std::clamp(hz, 10.0, rate * 0.45) / rate;
  const double alpha = std::sin(w) / (2.0 * 0.70710678);
  const double cosw = std::cos(w);
  const double a0 = 1.0 + alpha;
  if (high) {
    filter.b0 = (1.0 + cosw) / 2.0 / a0;
    filter.b1 = -(1.0 + cosw) / a0;
    filter.b2 = filter.b0;
  } else {
    filter.b0 = (1.0 - cosw) / 2.0 / a0;
    filter.b1 = (1.0 - cosw) / a0;
    filter.b2 = filter.b0;
  }
  filter.a1 = -2.0 * cosw / a0;
  filter.a2 = (1.0 - alpha) / a0;
  return filter;
}

} // namespace

QVector<float> prepare(const QVector<float> &samples, int sampleRate) {
  if (samples.isEmpty() || sampleRate <= 0)
    return {};

  // The anti-alias pass runs at the source rate, before anything is thrown
  // away: the cymbals and sibilance above 8 kHz would otherwise fold down into
  // the voice band, and folded noise is exactly what step 3 must not read as a
  // dip. Cheap here, impossible to undo afterwards.
  QVector<float> band(samples.size());
  Biquad guard = shape(std::min(kVoiceHighHz, sampleRate * 0.4), sampleRate,
                       false);
  for (int index = 0; index < samples.size(); ++index)
    band[index] = guard.run(samples.at(index));

  const double step = double(sampleRate) / double(kSampleRate);
  const int count = int(double(band.size()) / step);
  if (count < 2)
    return {};
  QVector<float> mono(count);
  for (int index = 0; index < count; ++index) {
    const double from = double(index) * step;
    const int start = int(from);
    const int stop = std::min<int>(band.size(), int(from + step) + 1);
    double sum = 0.0;
    int taken = 0;
    for (int source = start; source < stop; ++source) {
      sum += double(band.at(source));
      ++taken;
    }
    mono[index] = taken > 0 ? float(sum / taken) : 0.0f;
  }

  // And the low corner last, at the analysis rate, so the bass a level meter
  // cannot separate from a voice is gone before any lag is measured.
  Biquad low = shape(kVoiceLowHz, kSampleRate, true);
  for (int index = 0; index < mono.size(); ++index)
    mono[index] = low.run(mono.at(index));
  return mono;
}

QVector<Frame> analyse(const QVector<float> &samples, int sampleRate) {
  return analysePrepared(prepare(samples, sampleRate));
}

QVector<Frame> analysePrepared(const QVector<float> &samples) {
  QVector<Frame> frames;
  const int hop = std::max(1, int(std::lround(kHopMs * kSampleRate / 1000.0)));
  const int tauMin = std::max(2, int(kSampleRate / kMaximumHz));
  const int tauMax =
      std::min(kWindowSamples - 1, int(kSampleRate / kMinimumHz) + 1);
  // A lag of tau reads tau samples past the integration window, so a frame is
  // only measurable where both fit.
  const int need = kWindowSamples + tauMax;
  if (samples.size() < need || tauMax <= tauMin)
    return frames;

  const int count = 1 + (samples.size() - need) / hop;
  frames.reserve(count);
  QVector<double> diff(tauMax + 1, 0.0);
  QVector<double> norm(tauMax + 1, 1.0);

  for (int index = 0; index < count; ++index) {
    const float *x = samples.constData() + index * hop;
    Frame frame;
    frame.timeMs = (double(index * hop) + kWindowSamples / 2.0) * 1000.0 /
                   double(kSampleRate);
    double energy = 0.0;
    for (int j = 0; j < kWindowSamples; ++j)
      energy += double(x[j]) * double(x[j]);
    frame.rms = std::sqrt(energy / double(kWindowSamples));

    // Step 1: how badly the window disagrees with itself tau samples later.
    for (int tau = 1; tau <= tauMax; ++tau) {
      double sum = 0.0;
      for (int j = 0; j < kWindowSamples; ++j) {
        const double delta = double(x[j]) - double(x[j + tau]);
        sum += delta * delta;
      }
      diff[tau] = sum;
    }

    // Step 2: divided by the mean disagreement up to that lag. Without this
    // every longer lag looks better than the one before it and the answer is an
    // octave - or two - below the voice.
    double running = 0.0;
    norm[0] = 1.0;
    for (int tau = 1; tau <= tauMax; ++tau) {
      running += diff[tau];
      norm[tau] = running > 0.0 ? diff.at(tau) * double(tau) / running : 1.0;
    }

    // Step 3: the *first* lag that dips under the threshold, walked down to the
    // bottom of that dip. Taking the deepest dip in the whole range instead is
    // the classic octave error, because the lag at twice the period always
    // agrees with the signal at least as well as the period itself does.
    int chosen = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau) {
      if (norm.at(tau) >= kThreshold)
        continue;
      while (tau + 1 <= tauMax && norm.at(tau + 1) < norm.at(tau))
        ++tau;
      chosen = tau;
      break;
    }
    if (chosen < 0) {
      // Nothing that clean in this frame. The deepest dip still measures how
      // periodic it is, which is all the voicing test needs to reject it.
      chosen = tauMin;
      for (int tau = tauMin + 1; tau <= tauMax; ++tau)
        if (norm.at(tau) < norm.at(chosen))
          chosen = tau;
    }
    frame.aperiodicity = std::clamp(norm.at(chosen), 0.0, 1.0);

    // Step 4: the true minimum sits between two lags. Without this the pitch is
    // quantised to 16 kHz / tau, which at the top of the range is a step of
    // nearly a semitone - and a semitone is the size of the jump the caller
    // reads as a new syllable.
    double refined = double(chosen);
    if (chosen > tauMin && chosen < tauMax) {
      const double before = norm.at(chosen - 1);
      const double at = norm.at(chosen);
      const double after = norm.at(chosen + 1);
      const double curve = before + after - 2.0 * at;
      if (std::fabs(curve) > 1e-12)
        refined += 0.5 * (before - after) / curve;
    }
    const double hz = refined > 0.0 ? double(kSampleRate) / refined : 0.0;
    frame.f0Hz = hz >= kMinimumHz && hz <= kMaximumHz ? hz : 0.0;
    frames.append(frame);
  }

  // The paper's fifth step searches neighbouring frames for a better estimate.
  // A median of three does the same job for the thing that matters here - a
  // single frame that latched onto an octave or onto an instrument between two
  // good frames - and it cannot drag a frame's own voicing decision with it.
  if (frames.size() >= 3) {
    const QVector<Frame> raw = frames;
    for (int index = 1; index < frames.size() - 1; ++index) {
      double window[3] = {raw.at(index - 1).f0Hz, raw.at(index).f0Hz,
                          raw.at(index + 1).f0Hz};
      std::sort(window, window + 3);
      // Only where the frame has a pitch of its own: median-filling a silence
      // from its neighbours would invent a voice in the gap between words.
      if (raw.at(index).f0Hz > 0.0 && window[1] > 0.0)
        frames[index].f0Hz = window[1];
    }
  }

  QVector<double> levels;
  levels.reserve(frames.size());
  for (const Frame &frame : frames)
    levels.append(frame.rms);
  std::sort(levels.begin(), levels.end());
  const double loud = levels.isEmpty() ? 0.0 : levels.last();
  const double quiet =
      levels.isEmpty() ? 0.0 : levels.at(int(levels.size() * 0.2));
  const double floor =
      std::max(kSilenceRms, quiet + (loud - quiet) * kNoiseFraction);

  for (Frame &frame : frames)
    frame.voiced = frame.f0Hz > 0.0 &&
                   frame.aperiodicity <= kVoicedAperiodicity &&
                   frame.rms >= floor;
  return frames;
}

} // namespace CaptionVoiceYin
