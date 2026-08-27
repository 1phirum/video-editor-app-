#pragma once

#include <QString>
#include <QVariantList>
#include <QVector>

// Splits a long source into bounded transcription windows.
//
// Whisper's transcribe() is not a streaming API: it decodes the whole audio
// track into one float32 array and builds the mel spectrogram for all of it
// before emitting a single word. On an eight hour source that is ~1.9 GB of
// samples, a ~4.7 GB complex STFT and several GB of torch temporaries, all live
// at once - which is how a medium-model run reaches 20-25 GB and pages the
// machine to a halt. The model is not the problem; the amount of audio held in
// memory at one time is.
//
// So the source is transcribed a few minutes at a time. Peak memory then follows
// the window length instead of the file length, and an eight hour file costs the
// same as a five minute one.
struct TranscriptionWindow {
  // Nominal span this window owns, in source time.
  qint64 startMs = 0;
  qint64 lengthMs = 0;
  // Audio decoded before startMs purely so Whisper has context across the seam
  // and does not clip the first word. Nothing from here is kept unless the
  // midpoint rule in mergeWindow claims it.
  qint64 leadInMs = 0;

  qint64 endMs() const { return startMs + lengthMs; }
  // Where the extracted audio actually begins, so window-local timestamps can be
  // shifted back into source time.
  qint64 audioStartMs() const { return startMs - leadInMs; }
  qint64 audioLengthMs() const { return lengthMs + leadInMs; }
};

struct TranscriptionPlan {
  QVector<TranscriptionWindow> windows;
  // False means the source is short enough to hand to Whisper whole, which is
  // what every build before this did. Kept so short clips take an unchanged path.
  bool chunked = false;
  double estimatedPeakBytes = 0;
  // Human readable, shown in the transcription status line.
  QString summary;
};

namespace TranscriptionPlanner {

// `resumeFromMs` skips windows already covered by an earlier run, so a cancelled
// eight hour transcription can be continued instead of restarted.
TranscriptionPlan forSource(qint64 durationMs, const QString &model,
                            qint64 resumeFromMs = 0);

// Shifts one window's segments from window-local time into source time and
// appends them to `into`, resolving the lead-in overlap and keeping the list
// ordered and non-overlapping.
void mergeWindow(QVariantList *into, const QVariantList &windowSegments,
                 const TranscriptionWindow &window);

// Source time the merged list reaches. Used for the resume point and for the
// "kept N segments through hh:mm:ss" status after a cancel.
qint64 coveredMs(const QVariantList &segments);

QString formatDuration(qint64 milliseconds);

} // namespace TranscriptionPlanner
