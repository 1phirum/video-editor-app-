#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>

// Decoded still frames, kept in memory and evicted least-recently-used first.
//
// A scrub revisits the same few seconds constantly: dragging left and right over
// a cut, nudging the playhead a frame at a time, stepping back to a point just
// looked at. Every one of those was a fresh seek and decode before this cache
// existed. Now a position that decodes to a picture already held costs a hash
// lookup.
//
// Entries are bucketed by keyframe (see KeyframeIndex), so all the positions
// inside one GOP share a slot - which is correct, because in keyframe mode they
// all decode to the same picture.
//
// The budget is on pixel bytes, not on entry count: a 1920x1080 preview frame is
// 6 MB and a 320x180 filmstrip cell is 170 KB, so counting entries would either
// starve the small ones or blow past the memory ceiling on the large ones.
//
// The ceiling is sized for full-resolution stills. The monitor decodes at the
// source's own resolution (PreviewDecodePolicy), so one cached 1080p position is
// 6 MB and a 4K one 25 MB; a 96 MB budget held only a handful of positions and a
// scrub back over ground already covered went to the decoder again.
class FrameCache final {
public:
  struct Key {
    QString sourceKey; // path + mtime + size, from sourceKeyFor()
    qint64 bucketMs = 0;
    int width = 0;
    int height = 0;
    bool exact = false; // exact-seek frames are a different picture

    QString id() const;
    bool operator==(const Key &other) const { return id() == other.id(); }
  };

  static constexpr qint64 kDefaultBudgetBytes = 320LL * 1024 * 1024;

  // Shared by the scrub service and anything else decoding stills, so a frame
  // decoded for the monitor is also free for the next filmstrip refresh.
  static FrameCache &instance();

  explicit FrameCache(qint64 budgetBytes = kDefaultBudgetBytes);

  // Exact bucket hit. Marks the entry as recently used.
  QImage lookup(const Key &key);
  // Any frame of the same source and size whose bucket is within `toleranceMs`
  // of the requested one. During a fast drag showing a neighbouring picture
  // immediately reads as smooth motion, where waiting for the exact one reads as
  // a freeze. `matchedBucketMs` receives the bucket that was served, so the
  // caller can tell an approximate hit from an exact one.
  QImage nearest(const Key &key, qint64 toleranceMs,
                 qint64 *matchedBucketMs = nullptr);
  void insert(const Key &key, const QImage &image);

  void clear();
  void clearSource(const QString &sourceKey);

  qint64 bytes() const;
  qint64 budgetBytes() const;
  void setBudgetBytes(qint64 bytes);
  int count() const;
  quint64 hits() const;
  quint64 misses() const;

  // Identity of the file contents, so an edited or replaced file cannot serve a
  // stale frame.
  static QString sourceKeyFor(const QString &path);

private:
  struct Entry {
    Key key;
    QImage image;
    qint64 bytes = 0;
    quint64 tick = 0;
  };

  void evictLocked();

  mutable QMutex m_mutex;
  QHash<QString, Entry> m_entries;
  qint64 m_bytes = 0;
  qint64 m_budgetBytes = kDefaultBudgetBytes;
  quint64 m_tick = 0;
  quint64 m_hits = 0;
  quint64 m_misses = 0;
};
