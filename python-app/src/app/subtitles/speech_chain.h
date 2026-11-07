#pragma once

// Sequential placement for a generated voiceover, spread across audio rows.
//
// The rule is the one a person follows laying the lines out by hand: no two voices are
// ever heard at once. Each voice waits at its subtitle until the line before it has
// stopped, then starts.
//
//     start_i = max(cue_i.startMs, end_(i-1) + gap)   (never on top of the previous)
//     end_i   = start_i + the measured length of its audio
//
// So a line that runs longer than the room before the next cue pushes every later line
// back, and the dub can end after the picture. That is accepted on purpose: the user
// pulls the overrun in themselves with the speed limit (see speechSpeedForPicture) or
// the Fit-to-video report. Trading a guaranteed fit for a guaranteed no-overlap is the
// whole point of this mode - two speakers over each other never happens.
//
// The row a voice lands on is read from where its *subtitle* sits, not from where the
// voice ends up after waiting: reserve() colours the cue intervals, so a line whose
// subtitle falls inside another's is dealt a fresh row (A2, A3, ...). The waiting then
// slides its start forward, but the row stays. This reproduces the hand layout - Today
// on A1, a line whose cue overlapped it on A2, the next on A1 again - and the number of
// rows is not chosen: it emerges as the deepest stack of overlapping cues, which the
// picker used to set by hand.
//
// Lanes still step over the user's own music and never write to a locked track -
// reserve() skips both because seed() marks them first.

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

#include <functional>

#include "app/subtitles/timed_speech_placer.h"

namespace cutpro::speech {

struct ChainOptions {
  // Silence left between one voice and the next before the later one may start. Zero
  // butts them end to end; a larger value leaves a breath between every line. It moves
  // voices in time - a bigger gap pushes the whole chain, and its end, later.
  qint64 gapMs = 0;
  std::function<bool(int laneNumber)> lockedLane;
  int audioTrackCount = 1;
};

// Re-lays every generated voice in `clips` in cue order, each one starting when the
// line before it has finished (plus the gap) so none is ever heard over another, and
// dealt onto the row its subtitle's overlap earns it. Only startMs and track are
// rewritten - nothing is re-encoded, nothing is trimmed - so it is instant and one undo
// step. Idempotent: the same clips and options move nothing the second time (the cue is
// remembered in ttsCueStartMs, so a second pass re-chains from the cues, not the moved
// positions).
//
// Returns:
//   moved          clips whose start or row changed
//   voiceClips     generated voices considered
//   onPrimary      voices that ended up on A1
//   spilled        voices on A2 and below (their cue overlapped another's)
//   rows           rows used, i.e. the deepest overlap of cues - 1 when none overlap
//   resyncs        always 0; kept so the report reads the same map as before
//   maxLateMs      the furthest a voice sits behind its cue from waiting its turn
//   totalLateMs    those waits summed over every voice
//   chainEndMs     where the last voice stops talking
//   pictureEndMs   where the last video clip ends, or 0 with no video
//   overrunMs      how far chainEndMs runs past the picture, 0 when it fits
//   tracks         every row used, "A1", "A2", ...
QVariantMap chainGeneratedSpeech(QVariantList &clips,
                                 const ChainOptions &options);

// The speed limit a regenerate would need for the whole dub to fit inside the
// picture, worked out from the numbers already on the clips: what each line
// naturally takes to say, and what fitting its own slot would have cost.
//
// This is the answer to "the voices run past the end of the video". They do that
// because a line that cannot be said in the room its subtitle has pushes every
// later line back, and the current limit was too low to stop enough of them. The
// worst single line's requiredSpeed would clear the whole track, but it is one
// pathological cue and using it would rush all 2581. This is the smallest limit
// that lets the *chain* land inside the picture, which is a much lower number,
// because a line that overruns can still be absorbed by the next pause.
//
// Returns:
//   fits            true when some limit at or under 3.00x does it
//   limit           that limit, rounded up to the nearest 0.05
//   currentEndMs    where the dub ends as it stands
//   pictureEndMs    where the picture ends
//   overrunMs       how far past it the dub runs now
//   projectedEndMs  where the dub would end at `limit`
//   voiceClips      voices measured
//   overrunning     voices that do not fit their own slot as spoken
QVariantMap speechSpeedForPicture(const QVariantList &clips,
                                  const ChainOptions &options);

} // namespace cutpro::speech
