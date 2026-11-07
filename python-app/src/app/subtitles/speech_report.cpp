#include "app/subtitles/speech_report.h"

#include <QLatin1Char>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cutpro::speech {
namespace {

// Above this a line counts as "speaks too fast to fit". 1.0 is exactly full - the
// voice ends the same millisecond the next line starts - and a couple of percent
// over that is not audible, while a transcript with one-second cues would put
// almost every line on the list. The list has to be short enough to act on.
constexpr double kFastSpeed = 1.05;

struct Voice {
  QString clipId;
  QString track;
  int trackNumber = 0;
  int index = -1;
  qint64 startMs = 0;
  qint64 endMs = 0;
  qint64 durationMs = 0;
  qint64 audioMs = 0;
  qint64 slotMs = 0;
  double speed = 1.0;
  double requiredSpeed = 1.0;
  QString text;
  // Filled in by the sweep below.
  qint64 worstOverlapMs = 0;
  int concurrent = 0;
  int partner = -1; // position in the voice vector, or -1
};

// 0 for anything that is not an audio track, which is also a safe sort key: a
// generated voice always lands on A<n>.
int laneNumber(const QString &track) {
  if (!track.startsWith(QLatin1Char('A')))
    return 0;
  bool ok = false;
  const int number = track.mid(1).toInt(&ok);
  return ok ? number : 0;
}

// The fields both sections share. A row is self-contained on purpose: the dialog
// gets one snapshot and never reaches back into the clip list, which is free to
// change under it while it is open.
QVariantMap rowOf(const Voice &voice) {
  return QVariantMap{
      {QStringLiteral("clipId"), voice.clipId},
      {QStringLiteral("index"), voice.index},
      {QStringLiteral("track"), voice.track},
      {QStringLiteral("startMs"), voice.startMs},
      {QStringLiteral("endMs"), voice.endMs},
      {QStringLiteral("durationMs"), voice.durationMs},
      {QStringLiteral("audioMs"), voice.audioMs},
      {QStringLiteral("slotMs"), voice.slotMs},
      {QStringLiteral("speed"), voice.speed},
      {QStringLiteral("requiredSpeed"), voice.requiredSpeed},
      {QStringLiteral("concurrent"), voice.concurrent},
      {QStringLiteral("text"), voice.text}};
}

} // namespace

QVariantMap buildSpeechReport(const QVariantList &clips) {
  std::vector<Voice> voices;
  voices.reserve(static_cast<size_t>(clips.size()));

  for (const QVariant &entry : clips) {
    const QVariantMap clip = entry.toMap();
    if (clip.value(QStringLiteral("generatedBy")).toString() !=
        QLatin1String("text_to_speech"))
      continue;
    // A voice the user muted is not heard, so it neither overlaps anything nor
    // needs to be reported for running long - switching it off is one of the ways
    // to answer this report.
    if (!clip.value(QStringLiteral("enabled"), true).toBool())
      continue;
    const qint64 durationMs =
        clip.value(QStringLiteral("durationMs")).toLongLong();
    if (durationMs <= 0)
      continue;

    Voice voice;
    voice.clipId = clip.value(QStringLiteral("id")).toString();
    voice.track = clip.value(QStringLiteral("track")).toString();
    voice.trackNumber = laneNumber(voice.track);
    voice.index = clip.value(QStringLiteral("ttsSubtitleIndex"), -1).toInt();
    voice.startMs = clip.value(QStringLiteral("startMs")).toLongLong();
    voice.durationMs = durationMs;
    voice.endMs = voice.startMs + durationMs;
    voice.slotMs = clip.value(QStringLiteral("ttsSlotMs")).toLongLong();
    voice.audioMs = clip.value(QStringLiteral("ttsAudioMs")).toLongLong();
    if (voice.audioMs <= 0)
      voice.audioMs = durationMs;
    voice.speed = clip.value(QStringLiteral("ttsSpeed"), 1.0).toDouble();
    voice.text = clip.value(QStringLiteral("subtitleText")).toString();
    // A project made before the worker started reporting this has neither number,
    // so it is measured here from what the clip does carry. Same arithmetic, one
    // build later.
    voice.requiredSpeed =
        clip.value(QStringLiteral("ttsRequiredSpeed"), 0.0).toDouble();
    if (voice.requiredSpeed <= 0.0)
      voice.requiredSpeed = voice.slotMs > 0
                                ? double(voice.audioMs) / double(voice.slotMs)
                                : 1.0;
    voices.push_back(voice);
  }
  std::sort(voices.begin(), voices.end(), [](const Voice &a, const Voice &b) {
    if (a.startMs != b.startMs)
      return a.startMs < b.startMs;
    return a.trackNumber < b.trackNumber;
  });

  // One pass in start order, holding the voices that are still talking. Two voices
  // overlap across tracks as much as on one - the complaint is hearing both at
  // once, and which lane they sit in does not change that - so the active list is
  // not per-track.
  std::vector<int> active;
  int maxConcurrent = 0;
  for (int i = 0; i < int(voices.size()); ++i) {
    Voice &current = voices[static_cast<size_t>(i)];
    active.erase(std::remove_if(active.begin(), active.end(),
                                [&](int held) {
                                  return voices[static_cast<size_t>(held)]
                                             .endMs <= current.startMs;
                                }),
                 active.end());
    // Everything still in the list ends after this one starts, so all of it is
    // audible together with this one; the count is exact rather than an estimate.
    const int concurrent = int(active.size()) + 1;
    maxConcurrent = qMax(maxConcurrent, concurrent);
    current.concurrent = qMax(current.concurrent, concurrent);
    for (const int held : active) {
      Voice &other = voices[static_cast<size_t>(held)];
      other.concurrent = qMax(other.concurrent, concurrent);
      // In start order, so the overlap always begins at current.startMs.
      const qint64 overlapMs = qMin(other.endMs, current.endMs) - current.startMs;
      if (overlapMs <= 0)
        continue;
      if (overlapMs > current.worstOverlapMs) {
        current.worstOverlapMs = overlapMs;
        current.partner = held;
      }
      if (overlapMs > other.worstOverlapMs) {
        other.worstOverlapMs = overlapMs;
        other.partner = i;
      }
    }
    active.push_back(i);
  }

  std::vector<int> overlapping;
  std::vector<int> fast;
  std::vector<int> lanes;
  std::vector<int> speedBuckets(static_cast<size_t>(kSpeedBucketCount), 0);
  double maxRequiredSpeed = 0.0;
  double appliedSpeed = 1.0;
  int retimedCount = 0;
  int rushedCount = 0;
  for (int i = 0; i < int(voices.size()); ++i) {
    const Voice &voice = voices[static_cast<size_t>(i)];
    if (voice.trackNumber > 0)
      lanes.push_back(voice.trackNumber);
    maxRequiredSpeed = qMax(maxRequiredSpeed, voice.requiredSpeed);
    appliedSpeed = qMax(appliedSpeed, voice.speed);
    // Sped up by the run that made it, to fit the room it had. Nothing is re-timed
    // unless the request asked for it, so on a natural-pace dub this stays zero.
    if (voice.speed > 1.001 || voice.speed < 0.999)
      ++retimedCount;
    if (voice.speed > kRushedSpeed)
      ++rushedCount;
    if (voice.worstOverlapMs > 0)
      overlapping.push_back(i);
    if (voice.requiredSpeed > kFastSpeed)
      fast.push_back(i);
    // See the bucket comment in the header for why a line that already fits is
    // counted nowhere: it makes "still will not fit at cap X" a plain tail sum.
    if (voice.requiredSpeed > kSpeedBucketBase) {
      // The 1e-9 keeps a requiredSpeed that lands exactly on a bucket edge out of
      // the next bucket up, which double arithmetic on 0.05 otherwise does about
      // half the time.
      const double steps =
          std::ceil((voice.requiredSpeed - kSpeedBucketBase) / kSpeedBucketStep
                    - 1e-9);
      const int bucket =
          std::clamp(int(steps) - 1, 0, kSpeedBucketCount - 1);
      ++speedBuckets[static_cast<size_t>(bucket)];
    }
  }

  // Worst first, so a capped list is the part worth looking at.
  std::sort(overlapping.begin(), overlapping.end(), [&voices](int a, int b) {
    const Voice &x = voices[static_cast<size_t>(a)];
    const Voice &y = voices[static_cast<size_t>(b)];
    if (x.worstOverlapMs != y.worstOverlapMs)
      return x.worstOverlapMs > y.worstOverlapMs;
    return x.startMs < y.startMs;
  });
  std::sort(fast.begin(), fast.end(), [&voices](int a, int b) {
    const Voice &x = voices[static_cast<size_t>(a)];
    const Voice &y = voices[static_cast<size_t>(b)];
    if (x.requiredSpeed != y.requiredSpeed)
      return x.requiredSpeed > y.requiredSpeed;
    return x.startMs < y.startMs;
  });

  std::sort(lanes.begin(), lanes.end());
  lanes.erase(std::unique(lanes.begin(), lanes.end()), lanes.end());
  QVariantList trackNames;
  for (const int lane : lanes)
    trackNames.append(QStringLiteral("A%1").arg(lane));

  QVariantList overlapRows;
  const int overlapRowCount =
      qMin(int(overlapping.size()), kSpeechReportRowLimit);
  for (int n = 0; n < overlapRowCount; ++n) {
    const Voice &voice = voices[static_cast<size_t>(overlapping[n])];
    QVariantMap row = rowOf(voice);
    row.insert(QStringLiteral("overlapMs"), voice.worstOverlapMs);
    if (voice.partner >= 0) {
      // The one it collides with worst, of however many it collides with. The
      // count is on the row as `concurrent`; this is the one to look at first.
      const Voice &other = voices[static_cast<size_t>(voice.partner)];
      row.insert(QStringLiteral("otherClipId"), other.clipId);
      row.insert(QStringLiteral("otherIndex"), other.index);
      row.insert(QStringLiteral("otherTrack"), other.track);
      row.insert(QStringLiteral("otherStartMs"), other.startMs);
      row.insert(QStringLiteral("otherEndMs"), other.endMs);
      row.insert(QStringLiteral("otherText"), other.text);
    }
    overlapRows.append(row);
  }

  QVariantList fastRows;
  const int fastRowCount = qMin(int(fast.size()), kSpeechReportRowLimit);
  for (int n = 0; n < fastRowCount; ++n) {
    const Voice &voice = voices[static_cast<size_t>(fast[n])];
    QVariantMap row = rowOf(voice);
    // How much longer this line is than the room before the next one. That is the
    // number to shorten the text by, or the gap to widen.
    row.insert(QStringLiteral("overrunMs"),
               voice.slotMs > 0 ? qMax<qint64>(0, voice.audioMs - voice.slotMs)
                                : qint64(0));
    fastRows.append(row);
  }

  QVariantList bucketCounts;
  for (const int count : speedBuckets)
    bucketCounts.append(count);

  return QVariantMap{
      {QStringLiteral("voiceClips"), int(voices.size())},
      {QStringLiteral("trackCount"), int(lanes.size())},
      {QStringLiteral("tracks"), trackNames},
      {QStringLiteral("overlapCount"), int(overlapping.size())},
      {QStringLiteral("maxConcurrent"), maxConcurrent},
      {QStringLiteral("fastCount"), int(fast.size())},
      {QStringLiteral("fastThreshold"), kFastSpeed},
      {QStringLiteral("maxRequiredSpeed"), maxRequiredSpeed},
      {QStringLiteral("retimedCount"), retimedCount},
      {QStringLiteral("rushedCount"), rushedCount},
      {QStringLiteral("rushedSpeed"), kRushedSpeed},
      {QStringLiteral("appliedSpeed"), appliedSpeed},
      {QStringLiteral("speedBuckets"), bucketCounts},
      {QStringLiteral("speedBucketBase"), kSpeedBucketBase},
      {QStringLiteral("speedBucketStep"), kSpeedBucketStep},
      {QStringLiteral("rowLimit"), kSpeechReportRowLimit},
      {QStringLiteral("overlaps"), overlapRows},
      {QStringLiteral("fast"), fastRows}};
}

} // namespace cutpro::speech
