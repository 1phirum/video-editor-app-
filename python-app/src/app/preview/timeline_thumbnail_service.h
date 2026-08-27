#pragma once

#include "app/preview/decode_session.h"
#include "app/preview/keyframe_index.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QVariantMap>

#include <atomic>

// Produces the timeline's on-demand thumbnails and keeps the state that makes
// them cheap.
//
// Three things live here that a plain "decode a frame" call cannot have:
//
//  * a token per source. Image provider ids travel inside a URL, and a Windows
//    path with its colon, backslashes and spaces does not survive that intact.
//    QML asks the backend for a short token once and builds tile URLs from it.
//  * the keyframe index per source, so a position can be snapped to the bucket
//    it will decode to *before* any file is opened. That is what lets a cache
//    lookup answer on the calling thread.
//  * its own two-session decoder pool, kept apart from the monitor's. Filling a
//    screenful of thumbnails must never take the warm session the playhead is
//    scrubbing with.
class TimelineThumbnailService final {
public:
  struct Tile {
    QImage image;
    qint64 bucketMs = -1;
    bool fromCache = false;
    bool cancelled = false;
    QString error;

    bool valid() const { return !image.isNull(); }
  };

  static TimelineThumbnailService &instance();
  static bool available();

  TimelineThumbnailService();

  // Stable per path for the lifetime of the process, and reused when the same
  // path is asked for again.
  QString tokenFor(const QString &path);
  QString pathForToken(const QString &token) const;

  // Cache-only: memory, then disk, and only when the source's keyframe index is
  // already known. Never opens a file, so it is safe on the GUI thread.
  QImage cachedTile(const QString &path, qint64 positionMs);

  // Full path. Opens the source if needed, snaps the position to its keyframe
  // bucket, and decodes only when neither cache has that bucket.
  Tile tile(const QString &path, qint64 positionMs,
            const std::atomic_bool *cancel);

  void forget(const QString &path);
  QVariantMap statistics() const;

private:
  QString fingerprintFor(const QString &path);
  qint64 snapped(const QString &path, qint64 positionMs) const;
  void rememberKeyframes(const QString &path, const KeyframeIndex &index);

  mutable QMutex m_mutex;
  QHash<QString, QString> m_fingerprintByPath;
  QHash<QString, KeyframeIndex> m_keyframesByPath;

  // Two: one thread filling the strip while the other opens the next source.
  DecodeSessionCache m_sessions{2};

  std::atomic<quint64> m_serves{0};
  std::atomic<quint64> m_decodes{0};
  std::atomic<quint64> m_cancels{0};
  std::atomic<quint64> m_failures{0};
};
