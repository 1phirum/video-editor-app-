#include "app/media/media_path.h"

#include <QDir>
#include <QFileInfo>

namespace {

// libav is given absolute, forward-slashed paths. QDir::cleanPath also removes
// "." and ".." segments, which matters for the extended-length form below.
QString resolved(const QString &path) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty())
    return {};
  const QFileInfo info(trimmed);
  const QString absolute =
      info.isAbsolute() ? info.absoluteFilePath() : QDir::current().absoluteFilePath(trimmed);
  return QDir::cleanPath(absolute);
}

bool isExtendedForm(const QString &path) {
  return path.startsWith(QStringLiteral("\\\\?\\")) ||
         path.startsWith(QStringLiteral("//?/"));
}

} // namespace

QString MediaPath::nativeDecodePath(const QString &path) {
  const QString clean = resolved(path);
  if (clean.isEmpty())
    return {};
#if defined(Q_OS_WIN)
  if (isExtendedForm(clean))
    return QDir::toNativeSeparators(clean);
  if (clean.length() < kLongPathThreshold)
    return clean;
  const QString native = QDir::toNativeSeparators(clean);
  // UNC shares use the "\\?\UNC\server\share" spelling; a leading "\\" would
  // otherwise become an invalid "\\?\\\server" path.
  if (native.startsWith(QStringLiteral("\\\\")))
    return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
  return QStringLiteral("\\\\?\\") + native;
#else
  return clean;
#endif
}

QByteArray MediaPath::toFfmpegFileName(const QString &path) {
  // toUtf8(), never QFile::encodeName(): libav expects UTF-8 on every platform.
  return nativeDecodePath(path).toUtf8();
}

QByteArray MediaPath::toFfmpegUrl(const QString &path) {
  const QByteArray name = toFfmpegFileName(path);
  if (name.isEmpty())
    return {};
  if (name.startsWith("file:"))
    return name;
  return QByteArrayLiteral("file:") + name;
}

bool MediaPath::isDecodable(const QString &path, QString *reason) {
  const auto fail = [reason](const QString &message) {
    if (reason)
      *reason = message;
    return false;
  };

  const QString clean = resolved(path);
  if (clean.isEmpty())
    return fail(QStringLiteral("The media path is empty."));

  const QFileInfo info(clean);
  if (!info.exists())
    return fail(QStringLiteral("%1 no longer exists.").arg(info.fileName()));
  if (!info.isFile())
    return fail(QStringLiteral("%1 is not a file.").arg(info.fileName()));
  if (!info.isReadable())
    return fail(QStringLiteral("%1 is not readable.").arg(info.fileName()));
  if (info.size() <= 0)
    return fail(QStringLiteral("%1 is empty.").arg(info.fileName()));

  // An unpaired surrogate survives inside a QString but cannot be encoded, so
  // libav would receive replacement characters and open the wrong name (or
  // nothing). Catch it here rather than inside the decode thread.
  const QByteArray utf8 = clean.toUtf8();
  if (utf8.isEmpty() || QString::fromUtf8(utf8) != clean)
    return fail(QStringLiteral("%1 has a file name this system cannot encode.")
                    .arg(info.fileName()));

  if (reason)
    reason->clear();
  return true;
}

QString MediaPath::duplicateKey(const QString &path) {
  const QString clean = resolved(path);
  if (clean.isEmpty())
    return {};
  // canonicalFilePath() is empty for a file that has just been deleted, so keep
  // the cleaned path as the fallback key instead of collapsing to "".
  const QString canonical = QFileInfo(clean).canonicalFilePath();
  const QString key = canonical.isEmpty() ? clean : canonical;
#if defined(Q_OS_WIN)
  return key.toCaseFolded();
#else
  return key;
#endif
}
