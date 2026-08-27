#pragma once

#include <QString>

#include <atomic>

// Waveform generation for sources of any length.
//
// The previous waveform ran showwavespic over the whole audio track. That filter
// has to decode every sample before it can draw, so an 8 hour source meant
// decoding 8 hours of AAC to produce a 1600x160 PNG - which is why long media had
// its waveform suppressed and the timeline clip showed nothing under the video.
//
// Two strategies, chosen by duration:
//  - short sources are decoded straight through, which is exact and cheap;
//  - long sources are sampled: the envelope is built from one short decode window
//    per output column. A 1600 column strip is 1600 windows regardless of whether
//    the file is ten minutes or ten hours, so the cost is bounded by the width of
//    the image rather than by the length of the audio.
//
// The sampled envelope is an approximation, and it is the right one: at one
// column per several seconds of audio, a peak taken from a 120 ms window inside
// that span and a peak taken over the whole span are visually the same waveform.
class AudioPeakBuilder final {
public:
  struct Options {
    int imageWidth = 1600;
    int imageHeight = 160;
    // Columns of envelope. Fewer columns than pixels are stretched, which keeps
    // the sampled path affordable on very long sources.
    int columns = 1600;
    // Above this duration the sampled strategy is used instead of a full decode.
    qint64 fullDecodeLimitMs = 8LL * 60 * 1000;
    // Audio decoded per column in sampled mode.
    int windowMs = 120;
    int timeBudgetMs = 30000;
    // 0xRRGGBB, matching the previous showwavespic colour so the timeline looks
    // unchanged.
    quint32 colorRgb = 0x63a4ff;
  };

  struct Result {
    QString url;
    int columns = 0;
    bool fromCache = false;
    bool cancelled = false;
    bool sampled = false;
    QString error;

    bool valid() const { return !url.isEmpty(); }
  };

  static bool available();

  static Result build(const QString &path, qint64 durationMs,
                      const Options &options, const std::atomic_bool *cancel);
  // GCC rejects a defaulted reference to a nested aggregate with member
  // initialisers, so the common cases get overloads instead.
  static Result build(const QString &path, qint64 durationMs) {
    return build(path, durationMs, Options{}, nullptr);
  }
  static Result build(const QString &path, qint64 durationMs,
                      const std::atomic_bool *cancel) {
    return build(path, durationMs, Options{}, cancel);
  }

  // Cache-only lookup, for import: an already-built waveform is free, a missing
  // one is left to the deferred job rather than blocking the bin.
  static Result cached(const QString &path, qint64 durationMs,
                       const Options &options);
  static Result cached(const QString &path, qint64 durationMs) {
    return cached(path, durationMs, Options{});
  }
};
