#pragma once

#include <QString>
#include <QVector>

// Where the keyframes of one source are, in milliseconds.
//
// Every seek in a long H.264/HEVC source lands on a keyframe, never on the
// requested millisecond: asking for 01:23:45.100 and asking for 01:23:45.900
// decodes the same picture. Knowing that up front turns most of a scrub into
// cache hits, because consecutive positions inside one GOP share a cache slot
// instead of each paying for a seek and a decode.
//
// Two ways to get one:
//  - exact, from the container's own index (MP4/MOV carry a sample table, so the
//    index is already in memory once the header is parsed - reading it costs no
//    extra I/O);
//  - estimated, from an assumed GOP length, for containers that have no index.
//    The buckets are then uniform, which is still enough for caching.
class KeyframeIndex final {
public:
  // Assumed distance between keyframes when the container has no index. Two
  // seconds is what most cameras and encoders emit.
  static constexpr qint64 kDefaultGopMs = 2000;

  KeyframeIndex() = default;

  static KeyframeIndex fromTimestamps(QVector<qint64> timestampsMs,
                                      qint64 durationMs);
  static KeyframeIndex estimated(qint64 gopMs, qint64 durationMs);

  bool isEmpty() const { return m_timestamps.isEmpty() && m_gopMs <= 0; }
  // True when the positions came from the container rather than from an
  // assumption, so callers can decide whether a snapped position is reliable.
  bool exact() const { return m_exact; }
  int count() const { return int(m_timestamps.size()); }
  qint64 durationMs() const { return m_durationMs; }
  // Typical distance between keyframes: the median gap for an exact index, the
  // assumed length for an estimated one.
  qint64 gopSpanMs() const { return m_gopMs; }

  // Largest keyframe position at or before `positionMs`, which is where a
  // backward seek to that position will actually land.
  qint64 keyframeAtOrBefore(qint64 positionMs) const;
  // First keyframe strictly after `positionMs`, or -1 past the last one.
  qint64 keyframeAfter(qint64 positionMs) const;
  // Identifier shared by every position that decodes to the same picture. Used
  // as the frame cache bucket.
  qint64 bucketFor(qint64 positionMs) const {
    return keyframeAtOrBefore(positionMs);
  }
  // Half a GOP: how far a cached frame may be from a requested position before
  // it stops being a fair stand-in during a fast drag.
  qint64 toleranceMs() const;

  const QVector<qint64> &timestampsMs() const { return m_timestamps; }
  QString describe() const;

private:
  QVector<qint64> m_timestamps; // sorted, unique, milliseconds
  qint64 m_durationMs = 0;
  qint64 m_gopMs = 0;
  bool m_exact = false;
};
