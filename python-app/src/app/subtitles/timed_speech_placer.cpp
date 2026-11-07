#include "app/subtitles/timed_speech_placer.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLatin1Char>

#include <iterator>

namespace cutpro::speech {
namespace {

// Marks [startMs, endMs) taken, merging into whatever it touches. Two clips on one
// lane should never overlap, but a project file is free to say otherwise, and the
// neighbour test in reserve() only looks at the one interval before a span - an
// overlap left in the map could hide behind its neighbour and let a voice land on
// top of it.
void occupy(std::map<qint64, qint64> &intervals, qint64 startMs, qint64 endMs) {
  if (endMs <= startMs)
    return;
  auto it = intervals.lower_bound(startMs);
  if (it != intervals.begin()) {
    const auto previous = std::prev(it);
    if (previous->second >= startMs) {
      startMs = previous->first;
      endMs = qMax(endMs, previous->second);
      it = intervals.erase(previous);
    }
  }
  while (it != intervals.end() && it->first <= endMs) {
    endMs = qMax(endMs, it->second);
    it = intervals.erase(it);
  }
  intervals.emplace_hint(it, startMs, endMs);
}

} // namespace

void SpeechLaneMap::seed(const QVariantList &clips,
                         const std::function<bool(int)> &lockedLane,
                         int audioTrackCount,
                         const std::function<bool(const QVariantMap &)> &excludeClip) {
  m_lanes.assign(kMaxSpeechLanes, Lane{});
  if (lockedLane) {
    const int existing = qBound(0, audioTrackCount, kMaxSpeechLanes);
    for (int number = 1; number <= existing; ++number) {
      if (lockedLane(number))
        m_lanes[static_cast<size_t>(number - 1)].blocked = true;
    }
  }

  for (const QVariant &entry : clips) {
    const QVariantMap clip = entry.toMap();
    if (!clip.value(QStringLiteral("enabled"), true).toBool())
      continue;
    if (excludeClip && excludeClip(clip))
      continue;
    const QString track = clip.value(QStringLiteral("track")).toString();
    if (!track.startsWith(QLatin1Char('A')))
      continue;
    bool ok = false;
    const int number = track.mid(1).toInt(&ok);
    if (!ok || number < 1 || number > kMaxSpeechLanes)
      continue;
    const qint64 startMs = clip.value(QStringLiteral("startMs")).toLongLong();
    const qint64 durationMs =
        clip.value(QStringLiteral("durationMs")).toLongLong();
    if (durationMs <= 0)
      continue;
    occupy(m_lanes[static_cast<size_t>(number - 1)].intervals, startMs,
           startMs + durationMs);
  }
}

QString SpeechLaneMap::reserve(qint64 startMs, qint64 endMs) {
  if (m_lanes.empty())
    m_lanes.assign(kMaxSpeechLanes, Lane{});
  if (startMs < 0)
    startMs = 0;
  if (endMs <= startMs)
    endMs = startMs + 1;

  for (int index = 0; index < kMaxSpeechLanes; ++index) {
    Lane &lane = m_lanes[static_cast<size_t>(index)];
    if (lane.blocked)
      continue;
    // The two neighbours of the requested span are the only intervals that can
    // overlap it: the first one starting at or after startMs, and the one before
    // it. Everything else is either entirely ahead or entirely behind.
    const auto next = lane.intervals.lower_bound(startMs);
    if (next != lane.intervals.end() && next->first < endMs)
      continue;
    if (next != lane.intervals.begin() && std::prev(next)->second > startMs)
      continue;
    occupy(lane.intervals, startMs, endMs);
    return QStringLiteral("A%1").arg(index + 1);
  }
  return QStringLiteral("A%1").arg(kMaxSpeechLanes);
}

bool SpeechLaneMap::reserveOn(int laneNumber, qint64 startMs, qint64 endMs) {
  if (laneNumber < 1 || laneNumber > kMaxSpeechLanes)
    return false;
  if (m_lanes.empty())
    m_lanes.assign(kMaxSpeechLanes, Lane{});
  if (startMs < 0)
    startMs = 0;
  if (endMs <= startMs)
    endMs = startMs + 1;
  Lane &lane = m_lanes[static_cast<size_t>(laneNumber - 1)];
  if (lane.blocked)
    return false;
  // The same two-neighbour test reserve() runs, against one lane instead of the
  // first that answers.
  const auto next = lane.intervals.lower_bound(startMs);
  if (next != lane.intervals.end() && next->first < endMs)
    return false;
  if (next != lane.intervals.begin() && std::prev(next)->second > startMs)
    return false;
  occupy(lane.intervals, startMs, endMs);
  return true;
}

void SpeechLaneMap::clear() {
  m_lanes.clear();
  m_lanes.shrink_to_fit();
}

int SpeechLaneMap::freeLane(qint64 startMs, qint64 endMs) const {
  if (m_lanes.empty())
    return 1;
  if (startMs < 0)
    startMs = 0;
  if (endMs <= startMs)
    endMs = startMs + 1;
  for (int index = 0; index < kMaxSpeechLanes; ++index) {
    const Lane &lane = m_lanes[static_cast<size_t>(index)];
    if (lane.blocked)
      continue;
    const auto next = lane.intervals.lower_bound(startMs);
    if (next != lane.intervals.end() && next->first < endMs)
      continue;
    if (next != lane.intervals.begin() && std::prev(next)->second > startMs)
      continue;
    return index + 1;
  }
  return 0;
}

qint64 SpeechLaneMap::nextFreeStart(int laneNumber, qint64 fromMs,
                                    qint64 lengthMs) const {
  if (laneNumber < 1 || laneNumber > kMaxSpeechLanes || m_lanes.empty())
    return -1;
  const Lane &lane = m_lanes[static_cast<size_t>(laneNumber - 1)];
  if (lane.blocked)
    return -1;
  if (fromMs < 0)
    fromMs = 0;
  if (lengthMs <= 0)
    lengthMs = 1;
  qint64 candidate = fromMs;
  // Intervals are non-overlapping and ordered, so walking forward from the first
  // one that could touch the span settles in a step or two: each interval either
  // clears the candidate or pushes it to its own end, and the next interval starts
  // later than that.
  auto it = lane.intervals.lower_bound(candidate);
  if (it != lane.intervals.begin()) {
    const auto previous = std::prev(it);
    if (previous->second > candidate)
      candidate = previous->second;
  }
  while (it != lane.intervals.end() && it->first < candidate + lengthMs) {
    candidate = it->second;
    ++it;
  }
  return candidate;
}

CuePlacement describeCue(const QVariantMap &output, const QString &knownMediaId,
                         const IdFactory &makeId) {
  CuePlacement placement;

  const QString path = output.value(QStringLiteral("path")).toString();
  const qint64 startMs =
      qMax<qint64>(0, output.value(QStringLiteral("startMs")).toLongLong());
  const qint64 cueEndMs = output.value(QStringLiteral("endMs")).toLongLong();
  const qint64 cueMs = cueEndMs - startMs;
  // The length of the audio, not the length of the subtitle. The worker measures
  // every file it produces; what it reports here is what the voice actually needs.
  // Using the cue instead made the clip shorter than its own audio whenever a
  // translated line took longer to say than the original, so the sentence was cut
  // off in the monitor and again on export, where the chain is
  // atrim=...:duration=<clip durationMs>.
  const qint64 audioMs =
      qMax<qint64>(0, output.value(QStringLiteral("audioMs")).toLongLong());
  const qint64 sourceMs = audioMs > 0 ? audioMs : cueMs;
  // The room this voice had: from its own start to the next one's. Kept for the
  // report, and not used to shorten anything. Clamping the clip to it was quieter
  // on the timeline - one lane, no overlaps - but it did it by cutting the tail off
  // a line that ran long, and the tail of a line is words. A voice now gets the
  // whole length of its audio and the caller moves it down a track when that
  // collides with something, which is a thing the user can see and fix.
  //
  // Zero means unbounded: the last cue, or two cues that start together.
  const qint64 slotMs =
      qMax<qint64>(0, output.value(QStringLiteral("slotMs")).toLongLong());
  // What the worker would have had to do to make this line fit that room, whether
  // or not it was allowed to. 1.0 (or less) means it fits as spoken; above 1.0 is
  // the "speaks too fast to fit" list, and the amount over is how badly.
  const double requiredSpeed =
      output.value(QStringLiteral("requiredSpeed"), 1.0).toDouble();
  const qint64 durationMs = sourceMs;
  if (path.isEmpty() || durationMs <= 0 || cueMs <= 0)
    return placement;

  const QFileInfo source(path);
  placement.absolutePath = source.absoluteFilePath();
  QString mediaId = knownMediaId;
  if (mediaId.isEmpty()) {
    // Statted once per distinct file rather than once per cue. Cue tracks repeat
    // themselves, and a stat per cue is twenty thousand trips to the filesystem on
    // the GUI thread to re-answer a question the worker already audited.
    if (!source.isFile())
      return placement;
    mediaId = makeId("media");
    placement.needsMedia = true;
    placement.media = QVariantMap{
        {QStringLiteral("id"), mediaId},
        {QStringLiteral("path"), placement.absolutePath},
        {QStringLiteral("name"), source.fileName()},
        {QStringLiteral("kind"), QStringLiteral("audio")},
        {QStringLiteral("sizeBytes"), source.size()},
        // The file's own length, not the clip's. One voice file is shared by every
        // cue that repeats its text, and those cues do not all have the same room.
        {QStringLiteral("durationMs"), sourceMs},
        {QStringLiteral("width"), 0},
        {QStringLiteral("height"), 0},
        {QStringLiteral("frameRate"), 0.0},
        {QStringLiteral("sampleRate"), 24000},
        {QStringLiteral("channels"), 1},
        {QStringLiteral("thumbnailUrl"), QString()},
        {QStringLiteral("timelineThumbnailUrl"), QString()},
        {QStringLiteral("filmstripFrames"), 0},
        {QStringLiteral("filmstripFrameWidth"), 0},
        {QStringLiteral("filmstripFrameHeight"), 0},
        {QStringLiteral("waveformUrl"), QString()},
        {QStringLiteral("importedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("generatedBy"), QStringLiteral("text_to_speech")},
        {QStringLiteral("excludeFromTranscript"), true},
        {QStringLiteral("hiddenInProjectPanel"), true},
        {QStringLiteral("ttsSubtitleIndex"), output.value(QStringLiteral("index"))},
        {QStringLiteral("ttsStartMs"), startMs},
        {QStringLiteral("ttsEndMs"), cueEndMs},
        // Always 1.0 now: the voice is not re-timed. Kept because a project made
        // by an older build has sped-up files in it, and the report says so.
        {QStringLiteral("ttsSpeed"), output.value(QStringLiteral("speed"), 1.0)}};
  }

  placement.clip = QVariantMap{
      {QStringLiteral("id"), makeId("clip")},
      {QStringLiteral("mediaId"), mediaId},
      {QStringLiteral("name"), source.fileName()},
      {QStringLiteral("kind"), QStringLiteral("audio")},
      {QStringLiteral("startMs"), startMs},
      {QStringLiteral("sourceInMs"), 0},
      // The whole voice file. Clip and source length are the same thing here - the
      // clip is the line, from its first sample to its last - and the export chain
      // trims rather than resamples (atrim=...:duration=<durationMs>), so nothing
      // downstream reads a difference between these two as a rate change.
      {QStringLiteral("sourceDurationMs"), qMax<qint64>(1, sourceMs)},
      {QStringLiteral("durationMs"), durationMs},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("generatedBy"), QStringLiteral("text_to_speech")},
      {QStringLiteral("ttsSubtitleIndex"), output.value(QStringLiteral("index"))},
      {QStringLiteral("ttsCueDurationMs"), cueMs},
      // Where the subtitle is, as opposed to where the clip ended up. The fit pass
      // slides a line later to keep it off the one before it, and without this the
      // line's own place in the film would be lost the first time that happened -
      // a second pass would then slide it later again, from the moved position.
      {QStringLiteral("ttsCueStartMs"), startMs},
      // The three numbers the voice report is built from, on the clip itself so it
      // can be rebuilt for a project that was saved and reopened. speed is what was
      // applied (1.0 unless an old build made this), requiredSpeed is what fitting
      // the gap would have taken.
      {QStringLiteral("ttsSlotMs"), slotMs},
      {QStringLiteral("ttsAudioMs"), sourceMs},
      {QStringLiteral("ttsSpeed"), output.value(QStringLiteral("speed"), 1.0)},
      {QStringLiteral("ttsRequiredSpeed"), requiredSpeed},
      {QStringLiteral("subtitleText"), output.value(QStringLiteral("text"))}};

  placement.startMs = startMs;
  placement.endMs = startMs + durationMs;
  placement.valid = true;
  return placement;
}

} // namespace cutpro::speech
