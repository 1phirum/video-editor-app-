#include "app/subtitles/speech_chain.h"

#include <QLatin1Char>
#include <QSet>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cutpro::speech {
namespace {

// The worker's own MIN_SPEED. Below this it does not re-encode at all, because the
// atempo artefacts cost more than the few milliseconds win. An estimate that
// ignored this would predict a line getting shorter when the worker will leave it
// alone.
constexpr double kMinFitSpeed = 1.02;
// The worker clamps maxSpeed to this, so there is no point searching past it.
constexpr double kMaxFitSpeed = 3.0;

struct Voice {
  int clipIndex = -1;
  qint64 cueStartMs = 0;
  qint64 durationMs = 0;
};

// One voice as the speed search sees it: what it takes to say at its natural pace,
// and what saying it inside its own slot would cost.
struct Measured {
  qint64 cueStartMs = 0;
  qint64 naturalMs = 0;
  double requiredSpeed = 1.0;
};

bool isGeneratedVoice(const QVariantMap &clip) {
  return clip.value(QStringLiteral("generatedBy")).toString() ==
         QLatin1String("text_to_speech");
}

// Where the picture ends: the last frame of video on the timeline. Voices live on A
// rows, subtitles on S and effect bars on F, so the V rows are the film itself.
// Zero means there is no video, and then nothing is overrunning anything.
qint64 pictureEndMs(const QVariantList &clips) {
  qint64 endMs = 0;
  for (const QVariant &entry : clips) {
    const QVariantMap clip = entry.toMap();
    if (!clip.value(QStringLiteral("enabled"), true).toBool())
      continue;
    if (!clip.value(QStringLiteral("track")).toString().startsWith(
            QLatin1Char('V')))
      continue;
    const qint64 durationMs =
        clip.value(QStringLiteral("durationMs")).toLongLong();
    if (durationMs <= 0)
      continue;
    endMs = qMax(endMs, clip.value(QStringLiteral("startMs")).toLongLong() +
                            durationMs);
  }
  return endMs;
}

// Every generated voice, in the order it is heard. Placement built an approximation
// of this while the cues were still arriving out of the worker; sorting is where
// that is made exact.
std::vector<Voice> collectVoices(const QVariantList &clips) {
  std::vector<Voice> voices;
  voices.reserve(static_cast<size_t>(clips.size()));
  for (int index = 0; index < clips.size(); ++index) {
    const QVariantMap clip = clips.at(index).toMap();
    if (!isGeneratedVoice(clip))
      continue;
    // The measured length of the voice audio, which is what the ear gets. Nothing
    // is added to it and nothing is taken off.
    const qint64 durationMs =
        clip.value(QStringLiteral("durationMs")).toLongLong();
    if (durationMs <= 0)
      continue;
    Voice voice;
    voice.clipIndex = index;
    voice.durationMs = durationMs;
    // Where the subtitle is, which is where this voice wants to be. A clip that has
    // never been moved is still sitting on it.
    voice.cueStartMs =
        clip.contains(QStringLiteral("ttsCueStartMs"))
            ? clip.value(QStringLiteral("ttsCueStartMs")).toLongLong()
            : clip.value(QStringLiteral("startMs")).toLongLong();
    voice.cueStartMs = qMax<qint64>(0, voice.cueStartMs);
    voices.push_back(voice);
  }
  std::sort(voices.begin(), voices.end(), [](const Voice &a, const Voice &b) {
    if (a.cueStartMs != b.cueStartMs)
      return a.cueStartMs < b.cueStartMs;
    return a.clipIndex < b.clipIndex;
  });
  return voices;
}

} // namespace

QVariantMap chainGeneratedSpeech(QVariantList &clips,
                                 const ChainOptions &options) {
  const qint64 gapMs = qMax<qint64>(0, options.gapMs);
  const std::vector<Voice> voices = collectVoices(clips);

  // Everything except the voices being re-placed: the user's music holds its room
  // and a locked track is never written to. Seeding the voices in as well would make
  // each one collide with the position it is about to leave.
  SpeechLaneMap lanes;
  lanes.seed(clips, options.lockedLane, options.audioTrackCount,
             [](const QVariantMap &clip) { return isGeneratedVoice(clip); });

  int moved = 0;
  int onPrimary = 0;
  int spilled = 0;
  qint64 lastEndMs = 0;
  qint64 maxLateMs = 0;
  qint64 totalLateMs = 0;
  int maxRow = 0;
  QSet<int> usedLanes;

  // Two voices are never heard at once: each waits at its cue until the one before it
  // has stopped, exactly as the lines would be laid out by hand. chainEndMs is where
  // the last placed voice ends, and nothing may start before it.
  qint64 chainEndMs = 0;

  // Cue order. The row a line takes is read from where its subtitle sits, not from
  // where the voice ends up after waiting: reserve() the span the cue itself covers,
  // so a line whose subtitle lands inside another's is dealt a fresh row. That is what
  // spreads the dub across A1, A2, ... and decides how many rows it needs - the picker
  // is gone, the count follows the transcript.
  for (const Voice &voice : voices) {
    const qint64 cueEndMs = voice.cueStartMs + voice.durationMs;
    // The cue span plus the gap, so two lines closer than the gap are dealt to
    // different rows too, not just ones that truly overlap.
    const QString track = lanes.reserve(voice.cueStartMs, cueEndMs + gapMs);

    // Placed on the chain, never on top of the previous line: at the cue, or when the
    // one before it has fallen silent, whichever is later. This is why a long line
    // pushes every later line back and the dub can end after the picture - the user
    // pulls that overrun in themselves with the speed limit.
    const qint64 startMs =
        qMax(voice.cueStartMs, chainEndMs > 0 ? chainEndMs + gapMs : chainEndMs);
    const qint64 endMs = startMs + voice.durationMs;
    chainEndMs = endMs;

    QVariantMap clip = clips.at(voice.clipIndex).toMap();
    const bool changed =
        clip.value(QStringLiteral("startMs")).toLongLong() != startMs ||
        clip.value(QStringLiteral("track")).toString() != track;
    if (changed) {
      clip.insert(QStringLiteral("startMs"), startMs);
      clip.insert(QStringLiteral("track"), track);
      // Without this the cue's own place in the film is gone the first time a line
      // is dragged off it, and a second pass would re-place from the moved position.
      clip.insert(QStringLiteral("ttsCueStartMs"), voice.cueStartMs);
      clips[voice.clipIndex] = clip;
      ++moved;
    } else if (!clip.contains(QStringLiteral("ttsCueStartMs"))) {
      clip.insert(QStringLiteral("ttsCueStartMs"), voice.cueStartMs);
      clips[voice.clipIndex] = clip;
    }

    // The chain only moves forward, so this is simply the last voice's end - the last
    // sound the dub makes, the one that has to fit inside the picture.
    lastEndMs = qMax(lastEndMs, endMs);
    // How far the line had to wait past its cue for the chain to clear.
    const qint64 lateMs = qMax<qint64>(0, startMs - voice.cueStartMs);
    maxLateMs = qMax(maxLateMs, lateMs);
    totalLateMs += lateMs;

    bool ok = false;
    const int laneNumber = track.mid(1).toInt(&ok);
    if (ok && laneNumber > 0) {
      usedLanes.insert(laneNumber);
      maxRow = qMax(maxRow, laneNumber);
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

  const qint64 pictureMs = pictureEndMs(clips);
  return QVariantMap{
      {QStringLiteral("moved"), moved},
      {QStringLiteral("voiceClips"), int(voices.size())},
      {QStringLiteral("onPrimary"), onPrimary},
      {QStringLiteral("spilled"), spilled},
      // How many rows the dub is spread over: the deepest stack of subtitles whose
      // spoken length overlaps. 1 when the lines never crowd each other, growing only
      // as far as the transcript pushes it.
      {QStringLiteral("rows"), maxRow},
      // Unused now the chain never drifts off and back onto the cue; kept so the report
      // reads the same map.
      {QStringLiteral("resyncs"), 0},
      // How far the voices sit behind their cues from waiting their turn.
      {QStringLiteral("maxLateMs"), maxLateMs},
      {QStringLiteral("totalLateMs"), totalLateMs},
      // The end of the last voice, which is the last sound the dub makes.
      {QStringLiteral("chainEndMs"), lastEndMs},
      {QStringLiteral("pictureEndMs"), pictureMs},
      {QStringLiteral("overrunMs"),
       pictureMs > 0 ? qMax<qint64>(0, lastEndMs - pictureMs) : 0},
      {QStringLiteral("tracks"), trackNames}};
}

QVariantMap speechSpeedForPicture(const QVariantList &clips,
                                  const ChainOptions &options) {
  const qint64 gapMs = qMax<qint64>(0, options.gapMs);
  const std::vector<Voice> voices = collectVoices(clips);
  const qint64 pictureMs = pictureEndMs(clips);

  std::vector<Measured> measured;
  measured.reserve(voices.size());
  int overrunning = 0;
  for (const Voice &voice : voices) {
    const QVariantMap clip = clips.at(voice.clipIndex).toMap();
    // durationMs is the audio as it stands, which is already sped up by whatever
    // the last run's limit allowed. Undoing that recovers the natural length, which
    // is the only fixed quantity here - every candidate limit is measured from it.
    const double appliedSpeed =
        qMax(1.0, clip.value(QStringLiteral("ttsSpeed"), 1.0).toDouble());
    Measured item;
    item.cueStartMs = voice.cueStartMs;
    item.naturalMs = qMax<qint64>(
        1, static_cast<qint64>(std::llround(voice.durationMs * appliedSpeed)));
    item.requiredSpeed =
        qMax(1.0, clip.value(QStringLiteral("ttsRequiredSpeed"), 1.0).toDouble());
    if (item.requiredSpeed > 1.0)
      ++overrunning;
    measured.push_back(item);
  }

  // Walk the chain with every line sped up by min(what it needs, the limit) and
  // report where the last voice stops talking.
  const auto endAtLimit = [&measured, gapMs](double limit) {
    qint64 cursorMs = 0;
    qint64 lastEndMs = 0;
    for (const Measured &item : measured) {
      double speed = qMin(item.requiredSpeed, limit);
      // Mirrors the worker: under MIN_SPEED it does not re-encode, so the line
      // keeps its natural length.
      if (speed < kMinFitSpeed)
        speed = 1.0;
      const qint64 lengthMs = qMax<qint64>(
          1, static_cast<qint64>(std::llround(item.naturalMs / speed)));
      const qint64 startMs = qMax(item.cueStartMs, cursorMs);
      lastEndMs = startMs + lengthMs;
      cursorMs = lastEndMs + gapMs;
    }
    return lastEndMs;
  };

  const qint64 currentEndMs = endAtLimit(1.0);
  QVariantMap result{
      {QStringLiteral("voiceClips"), int(measured.size())},
      {QStringLiteral("overrunning"), overrunning},
      {QStringLiteral("pictureEndMs"), pictureMs},
      {QStringLiteral("currentEndMs"), currentEndMs},
      {QStringLiteral("overrunMs"),
       pictureMs > 0 ? qMax<qint64>(0, currentEndMs - pictureMs) : 0}};

  if (measured.empty() || pictureMs <= 0 || currentEndMs <= pictureMs) {
    // Nothing to solve: no voices, no picture, or it already fits at natural pace.
    result.insert(QStringLiteral("fits"), true);
    result.insert(QStringLiteral("limit"), 1.0);
    result.insert(QStringLiteral("projectedEndMs"), currentEndMs);
    return result;
  }

  if (endAtLimit(kMaxFitSpeed) > pictureMs) {
    // Even the worker's ceiling is not enough. There is more speech in the
    // transcript than there is film to put it under.
    result.insert(QStringLiteral("fits"), false);
    result.insert(QStringLiteral("limit"), kMaxFitSpeed);
    result.insert(QStringLiteral("projectedEndMs"), endAtLimit(kMaxFitSpeed));
    return result;
  }

  // The end is monotonic in the limit - raising it can only shorten a line - so a
  // bisection settles on the smallest limit that fits. Thirty passes over a few
  // thousand entries is microseconds, and the alternative (the worst single line's
  // requiredSpeed) rushes the whole dub to rescue one cue.
  double low = 1.0;
  double high = kMaxFitSpeed;
  for (int pass = 0; pass < 30 && high - low > 0.001; ++pass) {
    const double middle = 0.5 * (low + high);
    if (endAtLimit(middle) <= pictureMs)
      high = middle;
    else
      low = middle;
  }
  // Rounded up to a limit a person would choose, and re-checked: rounding down
  // would hand back a number that does not actually fit.
  double limit = std::ceil(high * 20.0) / 20.0;
  if (limit > kMaxFitSpeed)
    limit = kMaxFitSpeed;
  if (endAtLimit(limit) > pictureMs)
    limit = qMin(kMaxFitSpeed, limit + 0.05);

  result.insert(QStringLiteral("fits"), endAtLimit(limit) <= pictureMs);
  result.insert(QStringLiteral("limit"), limit);
  result.insert(QStringLiteral("projectedEndMs"), endAtLimit(limit));
  return result;
}

} // namespace cutpro::speech
