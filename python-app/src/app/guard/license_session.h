// Independent licence re-verification for use away from Backend.
//
// Backend holds the primary m_licensed gate and its own guard checksum
// (backend_license.cpp). This is a SEPARATE code path that reads the stored key
// and re-runs the Ed25519 verify from the low guard module, so the exporter and
// the periodic sweep can check the licence without trusting Backend's flag -
// and so a crack that only patches Backend's startup path leaves these sites
// live. Reading the registry here (rather than calling back into Backend) is
// deliberate duplication: two independent readers are two things to patch.
#ifndef CUTPRO_GUARD_LICENSE_SESSION_H
#define CUTPRO_GUARD_LICENSE_SESSION_H

#include <QtGlobal>

namespace guard {

// Re-read the stored key and verify it against this machine, from scratch.
// Trips LicenseForged tamper state if the stored key does not verify while the
// app is nonetheless running past the activation gate is NOT assumed here - the
// caller decides what an unlicensed result means. Returns true iff genuinely
// licensed right now.
bool licensedNow();

// The value the exporter mixes into its render. Returns 0 for a genuinely
// licensed, untampered process; a stable non-zero seed otherwise. There is no
// separate boolean to flip: the branch that adds the "UNLICENSED" watermark
// keys off this being non-zero, and the value itself comes from the failed
// verify / poison state, so forcing Backend.licensed true does not clear it.
quint64 exportGuardSeed();

} // namespace guard

#endif // CUTPRO_GUARD_LICENSE_SESSION_H
