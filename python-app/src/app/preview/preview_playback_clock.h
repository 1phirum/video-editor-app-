#pragma once

#include <QtGlobal>

// GUI-thread timing decisions, driven by a monotonic time supplied by Backend.
// Explicit timestamps also let tests exercise startup and drained audio without
// sleeping or opening a media device.
class PreviewPlaybackClock {
public:
  struct Sample {
    qint64 clipStartMs = 0;
    qint64 sourceInMs = 0;
    qint64 durationMs = 0;
    double speed = 1.0;
    qint64 shownMs = -1;
    qint64 audioMs = -1;
    qint64 frameAgeMs = 0;
    bool video = false;
    bool timedSource = false;
    bool decoding = false;
    bool failed = false;
  };

  void start(qint64 now, qint64 position) {
    m_started = m_anchorWall = m_lastTick = m_audioWall = now;
    m_anchorPosition = position;
    m_shown = m_audio = -1;
    m_displayWall = now;
    m_displayPosition = position;
    m_displayAdvancing = false;
  }
  static qint64 timelinePosition(const Sample &sample, qint64 sourceMs) {
    const double speed = qIsFinite(sample.speed) && sample.speed > 0
                             ? sample.speed : 1.0;
    return sample.clipStartMs + qRound64(qMax<qint64>(0, sourceMs - sample.sourceInMs) / speed);
  }
  qint64 next(qint64 now, qint64 position, const Sample &sample) {
    const qint64 elapsed = qMax<qint64>(0, now - m_lastTick);
    m_lastTick = now;
    if (!sample.timedSource)
      return recordDisplaySample(now, position, position + elapsed);
    if (sample.video && sample.shownMs < 0 && sample.decoding &&
        !sample.failed && now - m_started < 5000)
      return position;

    qint64 nextPosition;
    if (sample.audioMs >= 0 && (!sample.video || sample.shownMs >= 0)) {
      nextPosition = timelinePosition(sample, sample.audioMs);
      if (sample.audioMs != m_audio) {
        m_audio = sample.audioMs;
        m_audioWall = now;
      } else if (nextPosition < sample.clipStartMs + sample.durationMs &&
                 now - m_audioWall > 250) {
        nextPosition += now - m_audioWall - 250;
      }
    } else {
      if (sample.shownMs >= 0 && sample.shownMs != m_shown) {
        m_shown = sample.shownMs;
        m_anchorPosition = timelinePosition(sample, sample.shownMs);
        m_anchorWall = now - qBound(qint64(0), sample.frameAgeMs, qint64(120));
      }
      nextPosition = m_anchorPosition + qMax<qint64>(0, now - m_anchorWall);
    }
    return recordDisplaySample(now, position, qMax(position, nextPosition));
  }
  // Display-only interpolation between UI ticks. It never drives transport or
  // decoding, cannot run ahead by more than one tick, and holds on a stalled
  // source instead of accumulating wall-clock drift. All calls are GUI-thread.
  double displayPosition(double now, qint64 position, qint64 maximumLeadMs) {
    if (!m_displayAdvancing)
      return position;
    const double elapsed = qBound(0.0, now - m_displayWall,
                                 double(qMax<qint64>(0, maximumLeadMs)));
    m_displayPosition = qMax(m_displayPosition, position + elapsed);
    return m_displayPosition;
  }
private:
  qint64 recordDisplaySample(qint64 now, qint64 position, qint64 nextPosition) {
    if (nextPosition > position) {
      m_displayWall = now;
      m_displayAdvancing = true;
    }
    return nextPosition;
  }
  qint64 m_started = 0;
  qint64 m_anchorWall = 0;
  qint64 m_anchorPosition = 0;
  qint64 m_lastTick = 0;
  qint64 m_shown = -1;
  qint64 m_audio = -1;
  qint64 m_audioWall = 0;
  qint64 m_displayWall = 0;
  double m_displayPosition = 0;
  bool m_displayAdvancing = false;
};
