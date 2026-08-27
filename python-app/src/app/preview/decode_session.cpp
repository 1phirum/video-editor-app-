#include "app/preview/decode_session.h"

#include "app/preview/frame_cache.h"
#include "app/preview/seek_thumbnail_extractor.h"

#include <QDateTime>
#include <QMutexLocker>

#include <algorithm>
#include <utility>

QString DecodeSession::Profile::key() const {
  return QStringLiteral("%1x%2%3")
      .arg(maximumFrameSize.width())
      .arg(maximumFrameSize.height())
      .arg(exactSeek ? QStringLiteral("|e") : QString());
}

DecodeSession::DecodeSession(QString path, Profile profile)
    : m_path(std::move(path)), m_profile(profile),
      m_sourceKey(FrameCache::sourceKeyFor(m_path)) {}

DecodeSession::~DecodeSession() = default;

bool DecodeSession::open() {
  if (isOpen())
    return true;
  m_extractor = std::make_unique<SeekThumbnailExtractor>(m_path);
  SeekThumbnailExtractor::Options options;
  options.maximumFrameSize = m_profile.maximumFrameSize;
  options.exactSeek = m_profile.exactSeek;
  options.frameTimeBudgetMs = m_profile.frameTimeBudgetMs;
  if (!m_extractor->open(options))
    return false;
  touch();
  return true;
}

bool DecodeSession::isOpen() const {
  return m_extractor && m_extractor->isOpen();
}

QString DecodeSession::error() const {
  return m_extractor ? m_extractor->error() : QString();
}

qint64 DecodeSession::durationMs() const {
  return m_extractor ? m_extractor->durationMs() : 0;
}

QSize DecodeSession::sourceSize() const {
  return m_extractor ? m_extractor->sourceSize() : QSize();
}

const KeyframeIndex &DecodeSession::keyframes() {
  if (m_keyframesBuilt)
    return m_keyframes;
  m_keyframesBuilt = true;
  const qint64 duration = durationMs();
  QVector<qint64> timestamps =
      m_extractor ? m_extractor->keyframeTimestampsMs() : QVector<qint64>{};
  m_keyframes = timestamps.isEmpty()
                    ? KeyframeIndex::estimated(KeyframeIndex::kDefaultGopMs,
                                               duration)
                    : KeyframeIndex::fromTimestamps(std::move(timestamps),
                                                    duration);
  return m_keyframes;
}

void DecodeSession::setCancelToken(const std::atomic_bool *cancel) {
  if (m_extractor)
    m_extractor->setCancelToken(cancel);
}

QImage DecodeSession::frameAt(qint64 positionMs) {
  if (!isOpen())
    return {};
  touch();
  QImage frame = m_extractor->frameAt(positionMs);
  if (!frame.isNull())
    ++m_decodes;
  return frame;
}

void DecodeSession::touch() {
  m_lastUsedMs = QDateTime::currentMSecsSinceEpoch();
}

DecodeSessionCache::Lease::Lease(DecodeSessionCache *owner,
                                 std::unique_ptr<DecodeSession> session)
    : m_owner(owner), m_session(std::move(session)) {}

DecodeSessionCache::Lease::~Lease() {
  if (m_owner && m_session)
    m_owner->giveBack(std::move(m_session));
}

DecodeSessionCache::Lease::Lease(Lease &&other) noexcept
    : m_owner(other.m_owner), m_session(std::move(other.m_session)) {
  other.m_owner = nullptr;
}

DecodeSessionCache::Lease &
DecodeSessionCache::Lease::operator=(Lease &&other) noexcept {
  if (this == &other)
    return *this;
  if (m_owner && m_session)
    m_owner->giveBack(std::move(m_session));
  m_owner = other.m_owner;
  m_session = std::move(other.m_session);
  other.m_owner = nullptr;
  return *this;
}

DecodeSessionCache &DecodeSessionCache::instance() {
  static DecodeSessionCache cache;
  return cache;
}

DecodeSessionCache::DecodeSessionCache(int maximumSessions)
    : m_maximumSessions(qMax(1, maximumSessions)) {}

DecodeSessionCache::~DecodeSessionCache() = default;

DecodeSessionCache::Lease
DecodeSessionCache::checkout(const QString &path,
                             const DecodeSession::Profile &profile) {
  if (path.isEmpty())
    return {};
  const QString wantedProfile = profile.key();
  const QString wantedSource = FrameCache::sourceKeyFor(path);
  {
    QMutexLocker locker(&m_mutex);
    for (auto it = m_idle.begin(); it != m_idle.end(); ++it) {
      DecodeSession *candidate = it->get();
      if (!candidate || candidate->profile().key() != wantedProfile ||
          candidate->sourceKey() != wantedSource)
        continue;
      std::unique_ptr<DecodeSession> session = std::move(*it);
      m_idle.erase(it);
      ++m_reuseHits;
      locker.unlock();
      session->touch();
      return Lease(this, std::move(session));
    }
    ++m_opens;
  }

  // Opened outside the lock: this is the expensive path, and holding the pool
  // lock through a header parse would stall every other preview request.
  auto session = std::make_unique<DecodeSession>(path, profile);
  if (!session->open())
    return {};
  return Lease(this, std::move(session));
}

void DecodeSessionCache::giveBack(std::unique_ptr<DecodeSession> session) {
  if (!session)
    return;
  // A cancel token belonging to the returning worker must not outlive the lease,
  // or the next borrower inherits an aborting decoder.
  session->setCancelToken(nullptr);
  QMutexLocker locker(&m_mutex);
  m_idle.push_back(std::move(session));
  trimLocked();
}

void DecodeSessionCache::trimLocked() {
  while (int(m_idle.size()) > m_maximumSessions) {
    auto oldest = std::min_element(
        m_idle.begin(), m_idle.end(),
        [](const std::unique_ptr<DecodeSession> &a,
           const std::unique_ptr<DecodeSession> &b) {
          return a->lastUsedMsSinceEpoch() < b->lastUsedMsSinceEpoch();
        });
    m_idle.erase(oldest);
  }
}

void DecodeSessionCache::drop(const QString &path) {
  const QString sourceKey = FrameCache::sourceKeyFor(path);
  QMutexLocker locker(&m_mutex);
  m_idle.erase(std::remove_if(m_idle.begin(), m_idle.end(),
                              [&](const std::unique_ptr<DecodeSession> &s) {
                                return s && s->sourceKey() == sourceKey;
                              }),
               m_idle.end());
}

void DecodeSessionCache::clear() {
  QMutexLocker locker(&m_mutex);
  m_idle.clear();
}

void DecodeSessionCache::setMaximumSessions(int count) {
  QMutexLocker locker(&m_mutex);
  m_maximumSessions = qMax(1, count);
  trimLocked();
}

int DecodeSessionCache::idleSessions() const {
  QMutexLocker locker(&m_mutex);
  return int(m_idle.size());
}

int DecodeSessionCache::maximumSessions() const {
  QMutexLocker locker(&m_mutex);
  return m_maximumSessions;
}

quint64 DecodeSessionCache::reuseHits() const {
  QMutexLocker locker(&m_mutex);
  return m_reuseHits;
}

quint64 DecodeSessionCache::opens() const {
  QMutexLocker locker(&m_mutex);
  return m_opens;
}
