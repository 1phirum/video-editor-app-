#include "app/guard/binary_integrity.h"

#include "app/guard/tamper.h"
#include "app/license/license.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

extern "C" {
#include "ed25519.h"
}

#ifdef Q_OS_WIN
#include <windows.h>
#include <softpub.h>
#include <wintrust.h>
#endif

namespace guard {
namespace {

QString appDirOr(const QString &appDir) {
  return appDir.isEmpty() ? QCoreApplication::applicationDirPath() : appDir;
}

QByteArray sha256Of(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return QByteArray();
  QCryptographicHash h(QCryptographicHash::Sha256);
  if (!h.addData(&f))
    return QByteArray();
  return h.result();
}

#ifdef CUTPRO_ENFORCE_INTEGRITY
// Parse and signature-check the manifest. On success fills `entries` with
// (relativePath, hexSha256) pairs and returns true.
bool loadSignedManifest(const QString &manifestPath,
                        QList<QPair<QString, QByteArray>> *entries) {
  QFile f(manifestPath);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  const QByteArray blob = f.readAll();

  // Split off the trailing "SIG <hex>" line; everything before it is signed.
  const int sigIdx = blob.lastIndexOf("\nSIG ");
  if (sigIdx < 0)
    return false;
  const QByteArray signedRegion = blob.left(sigIdx + 1); // include the newline
  QByteArray sigLine = blob.mid(sigIdx + 5).trimmed();
  const QByteArray signature = QByteArray::fromHex(sigLine);
  if (signature.size() != 64)
    return false;

  const QByteArray pub = license::publicKey();
  if (pub.size() != 32)
    return false;
  if (ed25519_verify(
          reinterpret_cast<const unsigned char *>(signature.constData()),
          reinterpret_cast<const unsigned char *>(signedRegion.constData()),
          static_cast<size_t>(signedRegion.size()),
          reinterpret_cast<const unsigned char *>(pub.constData())) != 1)
    return false;

  for (const QByteArray &raw : signedRegion.split('\n')) {
    const QByteArray line = raw.trimmed();
    if (line.isEmpty() || !line.contains(' ') || line.startsWith("CUTPRO-"))
      continue;
    const int sp = line.indexOf(' ');
    const QByteArray hex = line.left(sp);
    const QString rel = QString::fromUtf8(line.mid(sp + 1)).trimmed();
    if (hex.size() == 64 && !rel.isEmpty())
      entries->append({rel, hex});
  }
  return !entries->isEmpty();
}
#endif // CUTPRO_ENFORCE_INTEGRITY

#if defined(Q_OS_WIN) && defined(CUTPRO_REQUIRE_SIGNATURE)
bool authenticodeValid(const QString &path) {
  const std::wstring wide = path.toStdWString();
  WINTRUST_FILE_INFO file{};
  file.cbStruct = sizeof(file);
  file.pcwszFilePath = wide.c_str();

  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  WINTRUST_DATA data{};
  data.cbStruct = sizeof(data);
  data.dwUIChoice = WTD_UI_NONE;
  data.fdwRevocationChecks = WTD_REVOKE_NONE;
  data.dwUnionChoice = WTD_CHOICE_FILE;
  data.dwStateAction = WTD_STATEACTION_VERIFY;
  data.pFile = &file;

  const LONG status = WinVerifyTrust(nullptr, &action, &data);
  data.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(nullptr, &action, &data);
  return status == ERROR_SUCCESS;
}
#endif

} // namespace

void verifyEmbeddedPublicKey() {
  const QByteArray digest =
      QCryptographicHash::hash(license::publicKey(), QCryptographicHash::Sha256);
  if (digest != license::publicKeyDigest())
    trip(TamperReason::PublicKeySwapped);
}

void verifyManifest(const QString &appDir) {
#ifdef CUTPRO_ENFORCE_INTEGRITY
  const QDir dir(appDirOr(appDir));
  const QString manifestPath = dir.filePath(QString::fromLatin1(kManifestName));

  QList<QPair<QString, QByteArray>> entries;
  if (!loadSignedManifest(manifestPath, &entries)) {
    trip(TamperReason::ManifestInvalid);
    return;
  }

  for (const auto &entry : entries) {
    const QString path = dir.filePath(entry.first);
    const QByteArray actual = sha256Of(path).toHex();
    if (actual.isEmpty() || actual.compare(entry.second, Qt::CaseInsensitive) != 0) {
      trip(TamperReason::BinaryModified);
      return;
    }
#if defined(Q_OS_WIN) && defined(CUTPRO_REQUIRE_SIGNATURE)
    if (path.endsWith(".exe", Qt::CaseInsensitive) ||
        path.endsWith(".dll", Qt::CaseInsensitive)) {
      if (!authenticodeValid(path)) {
        trip(TamperReason::BinaryModified);
        return;
      }
    }
#endif
  }
#else
  Q_UNUSED(appDir);
#endif
}

void verifyAll() {
  verifyEmbeddedPublicKey();
  verifyManifest();
}

} // namespace guard
