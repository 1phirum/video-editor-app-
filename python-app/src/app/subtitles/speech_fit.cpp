#include "app/subtitles/speech_fit.h"

#include "app/subtitles/timed_speech_placer.h"

#include <QLatin1Char>
#include <QSet>
#include <QVariant>

#include <algorithm>
#include <vector>

namespace cutpro::speech {
namespace {

struct Voice {
  int clipIndex = -1;
  qint64 cueStartMs = 0;
  qint64 durationMs = 0;
};

bool isGeneratedVoice(const QVariantMap &clip) {
  return clip.value(QStringLiteral("generatedBy")).toString() ==
         QLatin1String("text_to_speech");
}

} // namespace

QVariantMap fitGeneratedSpeech(QVariantList &clips, const FitOptions &options) {
  const qint64 maxLateMs = qMax<qint64>(0, options.maxLateMs);

  std::vector<Voice> voices;
  voices.reserve(static_cast<size_t>(clips.size()));
  for (int index = 0; index < clips.size(); ++index) {
    const QVariantMap clip = clips.at(index).toMap();
    if (!isGeneratedVoice(clip))
      continue;
    const qint64 durationMs =
        clip.value(QStringLiteral("durationMs")).toLongLong();
    if (durationMs <= 0)
      continue;
    Voice voice;
    voice.clipIndex = index;
    voice.durationMs = durationMs;
    // A clip made before this existed has never been moved, so where it is now is
    // where its cue is.
    voice.cueStartMs = clip.contains(QStringLiteral("ttsCueStartMs"))
                           ? clip.value(QStringLiteral("ttsCueStartMs")).toLongLong()
                           : clip.value(QStringLiteral("startMs")).toLongLong();
    voice.cueStartMs = qMax<qint64>(0, voice.cueStartMs);
    voices.push_back(voice);
  }

  // Cue order, which is the order they are heard in. The whole pass depends on it:
  // a line may only be pushed behind lines that come before it.
  std::sort(voices.begin(), voices.end(), [](const Voice &a, const Voice &b) {
    if (a.cueStartMs != b.cueStartMs)
      return a.cueStartMs < b.cueStartMs;
    return a.clipIndex < b.clipIndex;
  });

  // Everything except the voices being re-placed: the user's music holds its room,
  // and a locked track is never written to. Seeding the voices in as well would make
  // each one collide with the position it is about to leave.
  SpeechLaneMap lanes;
  lanes.seed(clips, options.lockedLane, options.audioTrackCount,
             [](const QVariantMap &clip) { return isGeneratedVoice(clip); });

  int moved = 0;
  int onPrimary = 0;
  int spilled = 0;
  int resyncs = 0;
  qint64 worstLateMs = 0;
  qint64 totalLateMs = 0;
  QSet<int> usedLanes;

  for (const Voice &voice : voices) {
    // The first track that comes free soon enough after this line is due. Tried in
    // order, so A1 wins whenever the line before it has finished talking - which is
    // the whole point: consecutive voices chain end to end on one track instead of
    // being stacked on top of each other across four.
    qint64 startMs = -1;
    for (int lane = 1; lane <= kMaxSpeechLanes; ++lane) {
      const qint64 candidate =
          lanes.nextFreeStart(lane, voice.cueStartMs, voice.durationMs);
      if (candidate < 0)
        continue; // Locked.
      if (candidate - voice.cueStartMs <= maxLateMs) {
        startMs = candidate;
        break;
      }
    }
    if (startMs < 0) {
      // Every track is busy for longer than this line is allowed to wait. Sync wins
      // over tidiness here: the line goes back to its own cue, and the lane map puts
      // it under whatever is still talking. This is the only case that leaves an
      // overlap, and it is what the count reports.
      startMs = voice.cueStartMs;
      ++resyncs;
    }

    const QString track = lanes.reserve(startMs, startMs + voice.durationMs);
    QVariantMap clip = clips.at(voice.clipIndex).toMap();
    const bool changed =
        clip.value(QStringLiteral("startMs")).toLongLong() != startMs ||
        clip.value(QStringLiteral("track")).toString() != track;
    if (changed) {
      clip.insert(QStringLiteral("startMs"), startMs);
      clip.insert(QStringLiteral("track"), track);
      // Kept for a second pass, and for the report: without it the cue's own place
      // in the film is gone as soon as the line moves off it.
      clip.insert(QStringLiteral("ttsCueStartMs"), voice.cueStartMs);
      clips[voice.clipIndex] = clip;
      ++moved;
    } else if (!clip.contains(QStringLiteral("ttsCueStartMs"))) {
      clip.insert(QStringLiteral("ttsCueStartMs"), voice.cueStartMs);
      clips[voice.clipIndex] = clip;
    }

    const qint64 lateMs = qMax<qint64>(0, startMs - voice.cueStartMs);
    worstLateMs = qMax(worstLateMs, lateMs);
    totalLateMs += lateMs;
    bool ok = false;
    const int laneNumber = track.mid(1).toInt(&ok);
    if (ok && laneNumber > 0) {
      usedLanes.insert(laneNumber);
      if (laneNumber == 1)
        ++onPrimary;
      else
        ++spilled;
    }
  }

  std::vector<int> laneNumbers(usedLanes.begin(), usedLanes.end());
  std::sort(laneNumbers.begin(), laneNumbers.end());
  QVariantList trackNames;
  for (const int lane : laneNumbers)
    trackNames.append(QStringLiteral("A%1").arg(lane));

  return QVariantMap{{QStringLiteral("moved"), moved},
                     {QStringLiteral("voiceClips"), int(voices.size())},
                     {QStringLiteral("onPrimary"), onPrimary},
                     {QStringLiteral("spilled"), spilled},
                     {QStringLiteral("resyncs"), resyncs},
                     {QStringLiteral("maxLateMs"), worstLateMs},
                     {QStringLiteral("totalLateMs"), totalLateMs},
                     {QStringLiteral("tracks"), trackNames}};
}

} // namespace cutpro::speech
