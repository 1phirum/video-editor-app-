#pragma once

// Where a generated subtitle voice goes on the timeline.
//
// Split out of Backend for two reasons. The first is size: lanes and overlaps are
// the only part of a timed voiceover that needs this kind of bookkeeping, and
// Backend has five thousand lines of other work in it. The second is why this
// file exists at all - the rules changed. Cues used to arrive from a finished
// manifest, sorted by start, so a lane could be a sorted list plus a cursor that
// only ever moved forward. They now arrive from the still-running worker in
// completion order: a line whose text repeats later in the film is finished the
// moment its first occurrence is, so cue 19000 can land before cue 40. A cursor
// cannot express that. Each lane keeps an interval map instead, and a query is
// two comparisons against the neighbours of the requested span.

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

#include <functional>
#include <map>
#include <vector>

namespace cutpro::speech {

// A1..A64, the lanes a generated voice may be placed on.
constexpr int kMaxSpeechLanes = 64;

class SpeechLaneMap {
public:
  // Existing occupancy, read once per run. `lockedLane` is asked only about lanes
  // the sequence actually has (1..audioTrackCount); a locked one is left alone,
  // the way the per-cue search used to leave it.
  //
  // `excludeClip` leaves clips out of the occupancy. A caller that is about to
  // re-place a set of clips has to seed the room they are in as empty, or every one
  // of them collides with its own old position and moves down a lane forever.
  void seed(const QVariantList &clips,
            const std::function<bool(int laneNumber)> &lockedLane,
            int audioTrackCount,
            const std::function<bool(const QVariantMap &clip)> &excludeClip = {});

  // The lowest lane where [startMs, endMs) is free, taken by the time this
  // returns. Falls back to the last lane when all 64 are occupied, which is what
  // the old search did.
  QString reserve(qint64 startMs, qint64 endMs);

  // Takes the span on one named lane, or returns false and takes nothing when that
  // lane is blocked, out of range, or busy across any part of it. This is what
  // "put this voice on row 2 if row 2 is free" asks: a caller that wants the dub
  // spread over a fixed set of rows names its row first and falls back to reserve()
  // for the voices whose row happens to be under the user's own audio.
  bool reserveOn(int laneNumber, qint64 startMs, qint64 endMs);

  // The same question without taking anything: the lowest lane number that is free
  // for the span, or 0 when none is. Lets a caller decide between two candidate
  // spans before committing to one.
  int freeLane(qint64 startMs, qint64 endMs) const;

  // The first instant at or after `fromMs` where `laneNumber` is free for `lengthMs`,
  // or -1 for a lane that is blocked or out of range. This is what "slide the line
  // later until it stops colliding" asks.
  qint64 nextFreeStart(int laneNumber, qint64 fromMs, qint64 lengthMs) const;

  void clear();
  bool isEmpty() const { return m_lanes.empty(); }

private:
  struct Lane {
    // startMs -> endMs, non-overlapping by construction.
    std::map<qint64, qint64> intervals;
    bool blocked = false;
  };
  std::vector<Lane> m_lanes;
};

// What one entry of the worker's output becomes: at most one bin entry, exactly
// one clip. Both come back unfinished of the caller's own bookkeeping - the lane
// is assigned by the map above - so this stays free of Backend.
struct CuePlacement {
  bool valid = false;
  QString absolutePath;
  qint64 startMs = 0;
  // startMs + the length of the audio, which is the span that has to be free.
  // A line the voice needs longer to say than its cue allows keeps talking past
  // the cue, and must not be placed on top of the line after it.
  qint64 endMs = 0;
  bool needsMedia = false;
  QVariantMap media;  // complete, id included, when needsMedia
  QVariantMap clip;   // complete except for "track"
};

using IdFactory = std::function<QString(const char *prefix)>;

// `knownMediaId` is the bin entry an earlier cue already made for the same file -
// identical text is synthesized once, so most cues of a repetitive track share
// one - or empty to make a new entry. Returns an invalid placement for anything
// that cannot become a clip: no path, no length, or a file that is not there.
CuePlacement describeCue(const QVariantMap &output, const QString &knownMediaId,
                         const IdFactory &makeId);

} // namespace cutpro::speech
