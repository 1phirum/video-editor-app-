// License verification entry points used by the app.
//
// The app only ever verifies; it never signs. The Ed25519 public key is
// embedded (obfuscated) in license_pubkey.cpp and assembled at runtime by
// publicKey(). Signing lives entirely in the separate license_gen tool.
#ifndef CUTPRO_LICENSE_H
#define CUTPRO_LICENSE_H

#include <QByteArray>
#include <QString>

namespace license {

// The 32-byte Ed25519 public key, reassembled from its obfuscated form.
// Defined in license_pubkey.cpp.
QByteArray publicKey();

// The SHA-256 of the genuine public key, stored as a separate embedded
// constant (NOT recomputed from publicKey()). The integrity layer compares
// sha256(publicKey()) against this so that swapping in an attacker's key - to
// self-sign both licences and the integrity manifest - is detected. Bumping the
// key means regenerating this alongside it (license_gen keygen prints both).
// Defined in license_pubkey.cpp.
QByteArray publicKeyDigest();

// Outcome of checking a key against this machine.
enum class Status {
  Valid,   // signature matches this machine and the key has not expired
  Invalid, // malformed, wrong machine, or bad signature
  Expired, // signature is genuine for this machine but the date has passed
};

// Full check: parse the CP2 key, verify the Ed25519 signature over
// (this-machine fingerprint || expiry) using the embedded public key, then
// enforce the embedded expiry against today's date.
Status check(const QString &key);

// The expiry day-count (days since 1970-01-01, 0 == perpetual) carried by a
// key, or 0 if the key is malformed. Does not verify the signature.
quint32 expiryDaysOf(const QString &key);

// Convenience: true only when check() == Valid.
inline bool verify(const QString &key) { return check(key) == Status::Valid; }

} // namespace license

#endif // CUTPRO_LICENSE_H
