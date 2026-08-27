#pragma once

#include "app/preview/keyframe_index.h"

#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

class SeekThumbnailExtractor;

// One open container, its decoder and its keyframe index, kept alive between
// requests.
//
// Opening a 6.3 GB MP4 is not cheap: libav parses the whole sample table before
// the first frame can be decoded. Doing that per scrub is what made the monitor
// stutter and what filled the console with repeated "Missing key frame while
// searching for timestamp: 0" - the same header work, over and over. A session
// pays it once and then answers each later position with a seek and a frame.
//
// Not thread-safe. Exclusive access is what DecodeSessionCache::Lease hands out.
class DecodeSession final {
public:
  struct Profile {
    QSize maximumFrameSize{960, 540};
    // Keyframe-only stills are near-instant and land up to one GOP early;
    // exact stills decode forward to the requested frame.
    bool exactSeek = false;
    int frameTimeBudgetMs = 1500;

    // Sessions are cached per profile: a filmstrip cell and a monitor frame want
    // different sizes out of the same file, and mixing them would keep
    // re-creating the scaler.
    QString key() const;
  };

  DecodeSession(QString path, Profile profile);
  ~DecodeSession();

  DecodeSession(const DecodeSession &) = delete;
  DecodeSession &operator=(const DecodeSession &) = delete;

  bool open();
  bool isOpen() const;
  const QString &path() const { return m_path; }
  const Profile &profile() const { return m_profile; }
  const QString &sourceKey() const { return m_sourceKey; }
  QString error() const;

  qint64 durationMs() const;
  QSize sourceSize() const;
  // Built on first use from the container index, so the cost lands on the first
  // scrub of a source rather than on its import.
  const KeyframeIndex &keyframes();

  // Aborts the current decode from another thread; cleared by the next request.
  void setCancelToken(const std::atomic_bool *cancel);

  QImage frameAt(qint64 positionMs);

  quint64 decodeCount() const { return m_decodes; }
  qint64 lastUsedMsSinceEpoch() const { return m_lastUsedMs; }
  void touch();

private:
  QString m_path;
  Profile m_profile;
  QString m_sourceKey;
  std::unique_ptr<SeekThumbnailExtractor> m_extractor;
  KeyframeIndex m_keyframes;
  bool m_keyframesBuilt = false;
  quint64 m_decodes = 0;
  qint64 m_lastUsedMs = 0;
};

// A small pool of live sessions, evicted least-recently-used.
//
// Three is deliberate: enough that alternating between two clips on the timeline
// and a third in the source monitor stays warm, few enough that the decoders and
// their reference frames cannot add up to hundreds of megabytes.
class DecodeSessionCache final {
public:
  // Exclusive checkout. The session leaves the pool for as long as the lease
  // lives and rejoins it on destruction, which is how two worker threads are
  // kept out of one non-reentrant decoder without holding the pool lock across a
  // decode.
  class Lease final {
  public:
    Lease() = default;
    ~Lease();
    Lease(Lease &&other) noexcept;
    Lease &operator=(Lease &&other) noexcept;
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;

    bool valid() const { return m_session != nullptr; }
    DecodeSession *get() const { return m_session.get(); }
    DecodeSession *operator->() const { return m_session.get(); }

  private:
    friend class DecodeSessionCache;
    Lease(DecodeSessionCache *owner, std::unique_ptr<DecodeSession> session);

    DecodeSessionCache *m_owner = nullptr;
    std::unique_ptr<DecodeSession> m_session;
  };

  static DecodeSessionCache &instance();

  explicit DecodeSessionCache(int maximumSessions = 3);
  ~DecodeSessionCache();

  // An open session for this source and profile, reused when one is idle in the
  // pool. Returns an invalid lease when the file cannot be opened.
  Lease checkout(const QString &path, const DecodeSession::Profile &profile);

  void drop(const QString &path);
  void clear();
  void setMaximumSessions(int count);

  int idleSessions() const;
  int maximumSessions() const;
  quint64 reuseHits() const;
  quint64 opens() const;

private:
  void giveBack(std::unique_ptr<DecodeSession> session);
  void trimLocked();

  mutable QMutex m_mutex;
  std::vector<std::unique_ptr<DecodeSession>> m_idle;
  int m_maximumSessions = 3;
  quint64 m_reuseHits = 0;
  quint64 m_opens = 0;
};
