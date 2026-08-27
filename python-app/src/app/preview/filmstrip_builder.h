#pragma once

#include <QString>

#include <atomic>

// Timeline filmstrip generation that does not depend on the length of the source.
//
// The strip keeps the layout the timeline already draws - one wide JPEG holding
// `cells` tiles of `cellWidth` x `cellHeight` - but it is produced by seeking to
// each sample point instead of decoding the file end to end. That is the whole
// difference between a filmstrip an 8 hour clip can have and the flat blue
// rectangle it used to get: cost scales with the number of cells, not with the
// duration or the file size.
//
// Sample points are taken at cell centres. Sampling at cell starts puts the
// source's first frame - very often black or a fade-in - in cell zero and lands
// the final cell exactly on EOF where there is nothing to decode.
class FilmstripBuilder final {
public:
  struct Options {
    int cellWidth = 160;
    int cellHeight = 90;
    // Cell count is derived from the duration and then clamped, so a 4 second
    // clip does not get 48 near-identical tiles and an 8 hour one is not
    // summarised by 12.
    int minimumCells = 8;
    int maximumCells = 48;
    qint64 msPerCell = 20000;
    int jpegQuality = 82;
    // Whole-strip ceiling. A cell that runs past it is filled from the last
    // successful still rather than left black.
    int timeBudgetMs = 25000;
    // Keyframe stills are the right trade-off for a filmstrip; exact stills cost
    // a partial GOP decode each and are visually indistinguishable at 160x90.
    bool exactSeek = false;
  };

  struct Result {
    QString url;
    int cells = 0;
    int cellWidth = 0;
    int cellHeight = 0;
    bool fromCache = false;
    bool cancelled = false;
    QString error;

    bool valid() const { return cells > 0 && !url.isEmpty(); }
  };

  static int cellCountFor(qint64 durationMs, const Options &options);

  static Result build(const QString &path, qint64 durationMs,
                      const Options &options,
                      const std::atomic_bool *cancel);
  // GCC rejects a defaulted reference to a nested aggregate with member
  // initialisers, so the common cases get overloads instead of default
  // arguments.
  static Result build(const QString &path, qint64 durationMs) {
    return build(path, durationMs, Options{}, nullptr);
  }
  static Result build(const QString &path, qint64 durationMs,
                      const std::atomic_bool *cancel) {
    return build(path, durationMs, Options{}, cancel);
  }

  // Cache-only lookups. Import uses these so a source that was already
  // filmstripped in an earlier session shows its thumbnails immediately, while a
  // cache miss stays the deferred job's problem and never blocks the bin.
  static Result cached(const QString &path, qint64 durationMs,
                       const Options &options);
  static Result cached(const QString &path, qint64 durationMs) {
    return cached(path, durationMs, Options{});
  }
  static QString cachedPoster(const QString &path);

  // A single poster frame, cached under its own variant. Used for the project
  // bin tile, where one still is all that is shown.
  static QString poster(const QString &path, qint64 durationMs,
                        const std::atomic_bool *cancel);
  static QString poster(const QString &path, qint64 durationMs) {
    return poster(path, durationMs, nullptr);
  }
};
