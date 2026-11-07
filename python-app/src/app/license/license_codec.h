// Base32 (Crockford) codec + license-key grouping helpers.
//
// Shared verbatim between the app (verify path) and the vendor-only
// license_gen tool (sign path) so both sides agree byte-for-byte on how a
// Machine ID and a license key are encoded. Pure functions, no Qt-object
// state; depends only on Qt Core (QByteArray/QString).
#ifndef CUTPRO_LICENSE_CODEC_H
#define CUTPRO_LICENSE_CODEC_H

#include <QByteArray>
#include <QString>

namespace license {

// Crockford base32 (alphabet 0-9 A-Z minus I, L, O, U). Encoding is
// uppercase and unpadded; decoding is case-insensitive, ignores dashes and
// spaces, and maps the visually-ambiguous letters back (I/L -> 1, O -> 0).
QString base32Encode(const QByteArray &data);
// Returns the decoded bytes, or an empty QByteArray if any character is not a
// legal base32 symbol.
QByteArray base32Decode(const QString &text);

// Insert a dash every `group` characters, e.g. "ABCDEFGH" -> "ABCD-EFGH".
QString group(const QString &text, int groupSize);

// Version marker that prefixes every license key. Bumping it lets a future
// key format be told apart and rejected by an older app. CP2 adds a signed
// expiry to the CP1 (perpetual, signature-only) format.
constexpr const char *kKeyPrefix = "CP2-";

// Number of fingerprint bytes that are signed and shown as the Machine ID.
// 80 bits is ample for node-locking (the threat is spoofing a readable
// machine identifier, not a hash collision).
constexpr int kFingerprintBytes = 10;
// Expiry, stored as a big-endian day count since 1970-01-01. 0 == perpetual.
constexpr int kExpiryBytes = 4;
// Ed25519 signature and public-key sizes.
constexpr int kSignatureBytes = 64;
constexpr int kPublicKeyBytes = 32;

// The exact bytes that get signed / verified: the machine fingerprint followed
// by the 4-byte big-endian expiry day count. Both the app and the generator
// build the message through this one function so they agree byte-for-byte.
QByteArray signedMessage(const QByteArray &fingerprint, quint32 expiryDays);

// Big-endian 4-byte encoding of the expiry day count (helper for key payloads).
QByteArray encodeExpiryDays(quint32 expiryDays);
quint32 decodeExpiryDays(const QByteArray &fourBytes);

} // namespace license

#endif // CUTPRO_LICENSE_CODEC_H
