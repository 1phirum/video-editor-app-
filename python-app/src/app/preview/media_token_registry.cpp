#include "app/preview/media_token_registry.h"

#include <QMutexLocker>

MediaTokenRegistry &MediaTokenRegistry::instance() {
  static MediaTokenRegistry registry;
  return registry;
}

QString MediaTokenRegistry::token(const QString &path) {
  if (path.isEmpty())
    return {};
  QMutexLocker locker(&m_mutex);
  const auto found = m_tokenByPath.constFind(path);
  if (found != m_tokenByPath.constEnd())
    return *found;
  // "s" for source. Kept short because it appears in every tile and waveform
  // URL the timeline builds, and those are compared as strings on every repaint.
  const QString token = QStringLiteral("s%1").arg(m_next++);
  m_tokenByPath.insert(path, token);
  m_pathByToken.insert(token, path);
  return token;
}

QString MediaTokenRegistry::path(const QString &token) const {
  QMutexLocker locker(&m_mutex);
  return m_pathByToken.value(token);
}

int MediaTokenRegistry::count() const {
  QMutexLocker locker(&m_mutex);
  return m_tokenByPath.size();
}
