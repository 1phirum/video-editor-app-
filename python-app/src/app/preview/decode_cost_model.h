#pragma once

#include <QHash>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QVariantMap>

// What this machine can actually decode from this file, measured while it does.
//
// PreviewDecodePolicy now asks for the source's own resolution, bounded only by
// the monitor panel, because that is the picture the user wants to judge the
// file by. That is the right target for H.264 and it is affordable. It is not
// affordable for every source: software AV1 at 1080p costs several times what
// H.264 costs at the same size, and the 8 hour recording that exposed this is
// AV1. Asking for the full frame there does not produce a better preview - the
// decoder simply cannot keep up, frames are dropped, the pool churns and the
// interface competes with a decoder that is never finished.
//
// So the bound has two inputs:
//
//  * a static estimate from the codec, available before a single frame is
//    decoded (CodecDecodeTraits), so the first second of playback is already
//    sized sensibly rather than being fixed after the stutter;
//  * a measurement, an exponential average of what one frame really cost,
//    which corrects the estimate in both directions on the machine it is
//    running on.
//
// The advice is deliberately asymmetric. Lowering needs little evidence,
// because the symptom is visible immediately; raising needs a lot, because a
// resolution that oscillates is worse than one that is slightly conservative.
// Stills are never bound by this: one frame the user studies can take its time.
class DecodeCostModel final {
public:
  struct Advice {
    // 0 when the model has no opinion; otherwise the longest frame side worth
    // decoding for playback.
    int maximumSide = 0;
    double maximumFrameRate = 0.0;
    double costWeight = 1.0;
    bool expensive = false;
    bool measured = false;
  };

  static DecodeCostModel &instance();

  // Called when a file is probed, so the codec is known before playback starts.
  void noteSource(const QString &path, const QString &codecName, int width,
                  int height, double frameRate);
  // One decoded, converted and published playback frame.
  void notePlaybackFrame(const QString &path, double milliseconds,
                         qint64 pixels);
  // One still (scrub frame, filmstrip cell, timeline tile).
  void noteStillDecode(const QString &path, double milliseconds,
                       bool succeeded);

  Advice adviceFor(const QString &path, int sourceWidth,
                   int sourceHeight) const;
  // The codec name last seen for this path, empty when it was never probed.
  QString codecFor(const QString &path) const;

  void forget(const QString &path);
  void clear();
  QVariantMap statistics() const;

  // A playback frame may take this share of its own frame interval. The rest is
  // for the scene graph, the compositor and everything else on the machine.
  static constexpr double kFrameTimeShare = 0.55;
  // Never advise below this: a preview that small is not worth having.
  static constexpr int kMinimumAdvisedSide = 960;

private:
  struct Entry {
    QString codec;
    double costWeight = 1.0;
    bool expensive = false;
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    // Exponential average of one playback frame, and the pixel count it was
    // measured at - the ratio is what makes it a scale factor.
    double frameMs = 0.0;
    qint64 framePixels = 0;
    quint64 frames = 0;
    double stillMs = 0.0;
    quint64 stills = 0;
    quint64 stillFailures = 0;
    // Held between calls so the advice does not move every frame.
    int advisedSide = 0;
    double advisedFrameRate = 0.0;
    quint64 overBudgetFrames = 0;
    quint64 underBudgetFrames = 0;
  };

  DecodeCostModel() = default;

  Entry *entryFor(const QString &path);
  void trimLocked();

  static constexpr int kMaximumSources = 32;
  // Consecutive frames outside the budget before the advice moves.
  static constexpr quint64 kLowerAfterFrames = 8;
  static constexpr quint64 kRaiseAfterFrames = 240;

  mutable QMutex m_mutex;
  QHash<QString, Entry> m_sources;
  quint64 m_downgrades = 0;
  quint64 m_upgrades = 0;
};
