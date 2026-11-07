#pragma once

#include <QHash>
#include <QMutex>
#include <QString>

// Short, stable stand-ins for media paths, so a source can be named inside an
// image:// URL.
//
// Qt parses an image provider id as the path part of a URL, and a Windows path
// does not survive that round trip: the drive colon terminates the host, the
// separators are rewritten and every space is percent-encoded. Each on-demand
// preview provider therefore needs the same indirection, and there is no reason
// for each to invent its own - a clip's thumbnail URL and its waveform URL are
// built from one handle, resolved back to one path.
//
// Tokens are never recycled. A URL held by a QML Image outlives the media entry
// it was built from, and resolving a stale token to a path whose artefacts have
// been dropped is harmless; handing that URL a *different* file would not be.
class MediaTokenRegistry final {
public:
  static MediaTokenRegistry &instance();

  // Same path in, same token out, for the lifetime of the process.
  QString token(const QString &path);
  QString path(const QString &token) const;
  int count() const;

private:
  mutable QMutex m_mutex;
  QHash<QString, QString> m_tokenByPath;
  QHash<QString, QString> m_pathByToken;
  int m_next = 1;
};
