#pragma once

// Moving generated voices so they stop playing over each other.
//
// The other half of the answer to an overrunning dub. Speeding a line up is done by
// the worker, before the audio exists; this is done afterwards, to audio that is
// already on the timeline, and it costs nothing but a pass over the clip list.
//
// The idea is the one a person uses by hand: if a line is still talking when the next
// one is due, start the next one a moment later instead of on top of it. Do that in
// order and the voices chain end to end on a single track. What it costs is sync - a
// line pushed later is later than its subtitle - so the slide is bounded. When the
// backlog grows past what the caller allows, the line goes back to its own cue time
// and the chain starts again from there; that one line lands on the track below,
// which is the only place an overlap is left.
//
// Own file rather than Backend or the placer: the placer answers "where does this
// one cue go" while cues are still arriving out of order, and cannot see the whole
// track. This runs once, over everything, and needs to.

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

#include <functional>

namespace cutpro::speech {

// How far behind its subtitle a voice may be dragged before the chain is abandoned
// and the line is put back where it belongs. Roughly one short sentence: far enough
// to absorb the usual overrun, close enough that the line still belongs to what is
// on screen.
constexpr qint64 kDefaultMaxLateMs = 2000;

struct FitOptions {
  qint64 maxLateMs = kDefaultMaxLateMs;
  // Lanes the caller will not write to (locked tracks), asked per lane number.
  std::function<bool(int laneNumber)> lockedLane;
  int audioTrackCount = 1;
};

// Rewrites startMs and track on every generated voice clip in `clips`, in place.
//
// Returns, for the dialog that asked:
//   moved        int   clips whose start or track changed
//   voiceClips   int   generated voice clips considered
//   onPrimary    int   clips that ended up on A1
//   spilled      int   clips left on A2 or below
//   resyncs      int   times the backlog was abandoned to keep a line in sync
//   maxLateMs    qint64 the worst lateness that survived, in ms
//   totalLateMs  qint64 sum of every clip's lateness, for an average
//   tracks       list  the track names in use afterwards, A-order
QVariantMap fitGeneratedSpeech(QVariantList &clips, const FitOptions &options);

} // namespace cutpro::speech
