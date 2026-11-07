#include "app/license/license.h"

#include "app/license/license_codec.h"
#include "app/license/machine_fingerprint.h"

#include <QByteArray>
#include <QDate>

extern "C" {
#include "ed25519.h"
}

namespace license {

namespace {
// Today's day count since 1970-01-01, matching the generator's expiry units.
quint32 currentEpochDay() {
  const qint64 days = QDate(1970, 1, 1).daysTo(QDate::currentDate());
  return days < 0 ? 0 : static_cast<quint32>(days);
}

// Split a key string into its expiry-day count and 64-byte signature. Returns
// false if the prefix, base32, or length is wrong.
bool parseKey(const QString &key, quint32 *expiryDays, QByteArray *signature) {
  const QString prefix = QString::fromLatin1(kKeyPrefix);
  QString body = key.trimmed();
  if (!body.startsWith(prefix, Qt::CaseInsensitive))
    return false;
  body = body.mid(prefix.size());

  const QByteArray payload = base32Decode(body);
  if (payload.size() != kExpiryBytes + kSignatureBytes)
    return false;

  *expiryDays = decodeExpiryDays(payload.left(kExpiryBytes));
  *signature = payload.mid(kExpiryBytes, kSignatureBytes);
  return true;
}
} // namespace

quint32 expiryDaysOf(const QString &key) {
  quint32 expiryDays = 0;
  QByteArray signature;
  if (!parseKey(key, &expiryDays, &signature))
    return 0;
  return expiryDays;
}

Status check(const QString &key) {
  quint32 expiryDays = 0;
  QByteArray signature;
  if (!parseKey(key, &expiryDays, &signature))
    return Status::Invalid;

  const QByteArray pub = publicKey();
  if (pub.size() != kPublicKeyBytes)
    return Status::Invalid;

  // The message is this machine's fingerprint plus the expiry carried by the
  // key, so a signature only validates when both match what was signed.
  const QByteArray message = signedMessage(fingerprintMessage(), expiryDays);
  const int ok = ed25519_verify(
      reinterpret_cast<const unsigned char *>(signature.constData()),
      reinterpret_cast<const unsigned char *>(message.constData()),
      static_cast<size_t>(message.size()),
      reinterpret_cast<const unsigned char *>(pub.constData()));
  if (ok != 1)
    return Status::Invalid;

  // 0 == perpetual. Otherwise valid through the expiry day inclusive.
  if (expiryDays != 0 && currentEpochDay() > expiryDays)
    return Status::Expired;
  return Status::Valid;
}

} // namespace license
