#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

// One open container, many stills.
//
// This is the primitive every timeline preview is built from. The old filmstrip
// ran `ffmpeg -i file -vf fps=12/28800` and let the filter graph walk the entire
// source: for the 8 hour test clip that is a full decode of 6.3 GB to produce
// twelve 160x90 cells, which is why long media had its filmstrip disabled
// outright and drew a flat blue rectangle instead of thumbnails.
//
// Seeking inverts the cost. A backward seek lands on the keyframe at or before
// the requested timestamp, and with skip_frame set to AVDISCARD_NONKEY the
// decoder emits that keyframe and nothing else - so a still costs one seek plus
// one frame regardless of whether it sits at 3 seconds or 7 hours in. Holding
// the AVFormatContext and the decoder open across every extraction is the other
// half: re-opening a long MP4 re-parses a multi-megabyte index each time, which
// is what produced the repeated "Missing key frame while searching for
// timestamp: 0" bursts in the console.
//
// Not thread-safe: one extractor per worker. Cancellation is honoured inside
// libav's blocking I/O through an interrupt callback, so a cancelled build stops
// during a read on a slow disk instead of after it.
class SeekThumbnailExtractor final {
public:
  struct Options {
    // Upper bound on the returned still. The aspect ratio is preserved and the
    // source is never upscaled.
    QSize maximumFrameSize{320, 180};
    // Keyframe stills are near-free but land up to one GOP early. Exact stills
    // decode forward from the keyframe to the requested timestamp.
    bool exactSeek = false;
    // Frames decoded while walking forward to an exact timestamp before giving
    // up and keeping the closest one found.
    int maximumDecodeFrames = 240;
    // 0 disables the per-extraction ceiling.
    int frameTimeBudgetMs = 4000;
  };

  static bool available();

  explicit SeekThumbnailExtractor(const QString &path);
  ~SeekThumbnailExtractor();

  SeekThumbnailExtractor(const SeekThumbnailExtractor &) = delete;
  SeekThumbnailExtractor &operator=(const SeekThumbnailExtractor &) = delete;

  // Stops an in-progress extraction and makes every later call fail fast.
  void setCancelToken(const std::atomic_bool *cancel);

  bool open(const Options &options);
  bool isOpen() const;
  QString error() const;

  // Duration reported by the container, or 0 when it is unknown.
  qint64 durationMs() const;
  // Rotation-corrected source size.
  QSize sourceSize() const;
  // Clockwise display rotation baked into the returned stills.
  int rotationDegrees() const;
  // Keyframe positions from the container's own index, in milliseconds, sorted.
  // Empty when the container carries no index. This costs no extra I/O: for
  // MP4/MOV the sample table is already in memory once the header is parsed.
  QVector<qint64> keyframeTimestampsMs() const;

  // A still at or near `positionMs`, already rotated and scaled. Null on
  // failure; call error() for the reason.
  QImage frameAt(qint64 positionMs);

  // The container's own codec name once the file is open ("av1", "h264", ...).
  QString codecName() const;

private:
  // One seek-and-decode attempt. frameAt() runs it a second time with the
  // keyframe-only filter removed when a codec turns out not to tolerate it.
  QImage decodePass(qint64 positionMs);

  struct Private;
  std::unique_ptr<Private> d;
};
