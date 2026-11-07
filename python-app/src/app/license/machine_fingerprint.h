// Per-machine fingerprint used to node-lock a license.
//
// The fingerprint is derived from stable Windows machine identifiers
// (MachineGuid + system-drive volume serial). The first bytes of it are what
// gets signed and what the user reads out as the "Machine ID". Pure functions,
// no Qt-object state.
#ifndef CUTPRO_MACHINE_FINGERPRINT_H
#define CUTPRO_MACHINE_FINGERPRINT_H

#include <QByteArray>
#include <QString>

namespace license {

// The raw bytes that are signed: the first kFingerprintBytes of
// SHA256("CutPro-v1|" + MachineGuid + "|" + volumeSerial). This is the exact
// message ed25519_verify/ed25519_sign operate on, and the exact bytes the
// Machine ID display encodes, so the vendor can sign from the ID alone.
QByteArray fingerprintMessage();

// Human-transcribable form of fingerprintMessage(): Crockford base32, grouped
// in blocks of four, e.g. "7Q2M-9F4K-BR31-XT8N".
QString machineIdDisplay();

} // namespace license

#endif // CUTPRO_MACHINE_FINGERPRINT_H
