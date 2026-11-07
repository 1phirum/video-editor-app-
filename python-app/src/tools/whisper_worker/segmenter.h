#pragma once

#include <QString>
#include <QVariantList>
#include <QVector>

// A faithful C++ port of tools/whisper_transcribe.py's line shaping
// (shape_segments / split_stream / should_cut / merge_fragments /
// pack_word_times). The native worker must cut subtitle lines exactly where the
// Python worker did, so a transcript looks the same whichever engine produced it.
//
// The input is the timed word stream (one entry per whisper word/token group),
// the output is one map per subtitle line with the keys the app's
// TranscriptionPlanner::mergeWindow and the single-pass reader consume:
//   { "start": seconds, "end": seconds, "text": str, "wordTimes"?: "b,e b,e" }
// Times are whatever timebase the words carry - the worker passes window-local
// seconds, and the app shifts them into source time.
namespace Segmenter {

struct Word {
  double start = 0.0; // seconds
  double end = 0.0;   // seconds
  QString text;       // whisper's own spacing: a leading space stays in the token
};

struct RawSegment {
  QVector<Word> words; // empty when whisper gave no timings for this segment
  QString text;        // used only for the wordless fallback
  double start = 0.0;
  double end = 0.0;
};

// Mirrors DEFAULT_LIMITS. Every value is milliseconds except the character
// counts.
struct Limits {
  qint64 pauseMs = 600;
  qint64 hardPauseMs = 1100;
  qint64 maxMs = 6200;
  int maxChars = 84;
  qint64 minMs = 900;
  int minChars = 12;
};

// Mirrors segmentation_limits(): DEFAULT_LIMITS, then the CUTPRO_SEGMENT_MAX_MS /
// CUTPRO_SEGMENT_PAUSE_MS environment overrides, then the job overrides (which
// win). Pass -1 for a job override that is absent.
Limits limitsFromEnvironment(qint64 jobMaxMs = -1, qint64 jobPauseMs = -1);

QVariantList shapeSegments(const QVector<RawSegment> &rawSegments,
                           const Limits &limits);

} // namespace Segmenter
