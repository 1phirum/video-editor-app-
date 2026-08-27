#include "app/subtitles/transcription_plan.h"

#include <QVariantMap>
#include <QtGlobal>

#include <algorithm>

namespace {

// Below this the whole file goes to Whisper in one pass, exactly as before.
// A twenty minute source needs about 80 MB of samples and a 200 MB spectrogram,
// which is nothing; splitting it would only cost seams and lose context.
constexpr qint64 kUnchunkedLimitMs = 20 * 60 * 1000;

// Extra audio in front of each window so Whisper has something to condition on
// and does not clip the first word at a seam.
constexpr qint64 kLeadInMs = 3000;

// Whisper holds 16 kHz mono float32 samples, a complex64 STFT over 201 bins with
// one frame per 160 samples, its magnitudes, and the mel itself. Those all live
// at the same time, so one second of audio costs roughly:
//   16000 * 4                    samples
// + 16000 / 160 * 201 * 8        STFT
// + 16000 / 160 * 201 * 4        magnitudes
// + 16000 / 160 * 128 * 4        mel (128 bins is the large-v3 case)
// ...plus a torch temporary or two of the same order. 400 KB/s is that sum
// rounded up, and it is what makes the difference between a 300 s window costing
// ~120 MB and an eight hour file costing ten gigabytes.
constexpr double kBytesPerAudioSecond = 400.0 * 1024.0;

// Resident cost of the weights themselves, fp32 on CPU. Independent of window
// length - it is the floor a model cannot go below.
double modelWeightBytes(const QString &model) {
  const QString name = model.trimmed().toLower();
  if (name == QStringLiteral("tiny"))
    return 0.3 * 1024 * 1024 * 1024;
  if (name == QStringLiteral("base"))
    return 0.5 * 1024 * 1024 * 1024;
  if (name == QStringLiteral("small"))
    return 1.1 * 1024 * 1024 * 1024;
  if (name == QStringLiteral("medium"))
    return 3.1 * 1024 * 1024 * 1024;
  if (name == QStringLiteral("turbo"))
    return 3.2 * 1024 * 1024 * 1024;
  // large-v2 / large-v3 and anything unrecognised: assume the worst.
  return 6.2 * 1024 * 1024 * 1024;
}

// Larger models keep more activations alive per second of audio, so they get
// shorter windows. Ten minutes is long enough that seams are rare and the model
// load is amortised; five keeps the heavy models comfortable.
qint64 windowLengthForModel(const QString &model) {
  const QString name = model.trimmed().toLower();
  if (name == QStringLiteral("tiny") || name == QStringLiteral("base") ||
      name == QStringLiteral("small"))
    return 10 * 60 * 1000;
  return 5 * 60 * 1000;
}

QString formatBytes(double bytes) {
  const double gigabytes = bytes / (1024.0 * 1024.0 * 1024.0);
  if (gigabytes >= 1.0)
    return QStringLiteral("%1 GB").arg(gigabytes, 0, 'f', 1);
  return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 0);
}

} // namespace

namespace TranscriptionPlanner {

TranscriptionPlan forSource(qint64 durationMs, const QString &model,
                            qint64 resumeFromMs) {
  TranscriptionPlan plan;
  const qint64 duration = qMax<qint64>(0, durationMs);
  const qint64 resumeFrom = qBound<qint64>(0, resumeFromMs, duration);

  if (duration > 0 && duration <= kUnchunkedLimitMs && resumeFrom <= 0) {
    plan.chunked = false;
    plan.estimatedPeakBytes =
        modelWeightBytes(model) + duration / 1000.0 * kBytesPerAudioSecond;
    plan.summary = QStringLiteral("one pass, ~%1 peak")
                       .arg(formatBytes(plan.estimatedPeakBytes));
    return plan;
  }
  // A source with no known duration is treated as long: guessing "short" would
  // put the whole file back into memory, which is the failure being fixed.
  if (duration <= 0) {
    plan.chunked = false;
    plan.estimatedPeakBytes = modelWeightBytes(model);
    plan.summary = QStringLiteral("one pass");
    return plan;
  }

  const qint64 windowMs = windowLengthForModel(model);
  plan.chunked = true;
  for (qint64 start = resumeFrom; start < duration; start += windowMs) {
    TranscriptionWindow window;
    window.startMs = start;
    window.lengthMs = qMin(windowMs, duration - start);
    window.leadInMs = start > 0 ? qMin(kLeadInMs, start) : 0;
    // A sliver at the end is not worth its own model pass or its own seam.
    if (window.lengthMs < 2000 && !plan.windows.isEmpty()) {
      plan.windows.last().lengthMs += window.lengthMs;
      break;
    }
    plan.windows.append(window);
  }

  plan.estimatedPeakBytes =
      modelWeightBytes(model) +
      (windowMs + kLeadInMs) / 1000.0 * kBytesPerAudioSecond;
  plan.summary = QStringLiteral("%1 windows x %2 min, ~%3 peak")
                     .arg(plan.windows.size())
                     .arg(windowMs / 60000)
                     .arg(formatBytes(plan.estimatedPeakBytes));
  if (resumeFrom > 0)
    plan.summary = QStringLiteral("resuming at %1 - %2")
                       .arg(formatDuration(resumeFrom), plan.summary);
  return plan;
}

void mergeWindow(QVariantList *into, const QVariantList &windowSegments,
                 const TranscriptionWindow &window) {
  if (!into)
    return;
  // Window-local seconds are relative to where the extracted audio began, which
  // is leadInMs before the span this window owns.
  const double offsetSeconds = window.audioStartMs() / 1000.0;
  const double nominalStartSeconds = window.startMs / 1000.0;

  qint64 previousEndMs = into->isEmpty() ? -1 : coveredMs(*into);
  for (const QVariant &value : windowSegments) {
    const QVariantMap source = value.toMap();
    const double start = source.value(QStringLiteral("start")).toDouble() +
                         offsetSeconds;
    const double end =
        source.value(QStringLiteral("end")).toDouble() + offsetSeconds;
    if (end <= start)
      continue;
    // The lead-in exists for context, not for output. A phrase straddling the
    // seam belongs to whichever window holds more of it, so the midpoint decides
    // - which means it is emitted exactly once, by exactly one window.
    if ((start + end) / 2.0 < nominalStartSeconds)
      continue;

    QVariantMap segment = source;
    qint64 startMs = qRound64(start * 1000.0);
    qint64 endMs = qRound64(end * 1000.0);
    if (previousEndMs >= 0)
      startMs = qMax(startMs, previousEndMs);
    endMs = qMax(endMs, startMs + 1);
    segment[QStringLiteral("start")] = startMs / 1000.0;
    segment[QStringLiteral("end")] = endMs / 1000.0;
    // Word timings travel with the segment for the TTS and translation paths, so
    // they need the same shift.
    const QVariantList words = source.value(QStringLiteral("words")).toList();
    if (!words.isEmpty()) {
      QVariantList shifted;
      shifted.reserve(words.size());
      for (const QVariant &wordValue : words) {
        QVariantMap word = wordValue.toMap();
        if (word.contains(QStringLiteral("start")))
          word[QStringLiteral("start")] =
              word.value(QStringLiteral("start")).toDouble() + offsetSeconds;
        if (word.contains(QStringLiteral("end")))
          word[QStringLiteral("end")] =
              word.value(QStringLiteral("end")).toDouble() + offsetSeconds;
        shifted.append(word);
      }
      segment[QStringLiteral("words")] = shifted;
    }
    into->append(segment);
    previousEndMs = endMs;
  }
}

qint64 coveredMs(const QVariantList &segments) {
  qint64 covered = 0;
  for (const QVariant &value : segments) {
    const qint64 endMs =
        qRound64(value.toMap().value(QStringLiteral("end")).toDouble() * 1000.0);
    covered = qMax(covered, endMs);
  }
  return covered;
}

QString formatDuration(qint64 milliseconds) {
  const qint64 totalSeconds = qMax<qint64>(0, milliseconds) / 1000;
  const qint64 hours = totalSeconds / 3600;
  const qint64 minutes = (totalSeconds % 3600) / 60;
  const qint64 seconds = totalSeconds % 60;
  if (hours > 0)
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
  return QStringLiteral("%1:%2")
      .arg(minutes)
      .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace TranscriptionPlanner
