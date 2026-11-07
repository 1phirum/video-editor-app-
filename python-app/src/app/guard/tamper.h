// Process-wide tamper state.
//
// The anti-crack layer never fails at the point it detects a problem: a crack
// is written by watching what happens right after a patch, so a check that
// aborts on its own line hands the attacker the exact address to NOP. Instead
// every detector calls tamper::trip(); that only *arms* a poison flag, and the
// consequences (refuse to run, corrupt export output, a delayed abort through
// the crash reporter) are read back elsewhere, far from the detector, at
// varied and partly randomized times. See [[cross-dll-pmf-connect-hazard]] for
// why this lives in a low module both the backend and the exporter can link.
//
// Deliberately recoverable: trip() poisons only in-memory state for this run.
// A clean reinstall clears it, so a false positive (an antivirus touching a
// DLL, a half-applied auto-update) never permanently bricks a paying customer.
#ifndef CUTPRO_GUARD_TAMPER_H
#define CUTPRO_GUARD_TAMPER_H

#include <QtGlobal>

namespace guard {

// Reasons are kept only for the (optional) delayed crash-report string; they
// are not surfaced to the UI, so a cracker cannot grep the binary for a message
// that names the check that caught them.
enum class TamperReason : quint32 {
  None = 0,
  DebuggerPresent = 0xA13C7E01u,
  BinaryModified = 0x5F02B9C4u,
  PublicKeySwapped = 0x9C4471E2u,
  LicenseForged = 0x2E8A6D53u,
  ManifestInvalid = 0x77B15AF0u,
};

// Arm the poison flag. Cheap, thread-safe, and idempotent. Does NOT fail here.
void trip(TamperReason reason);

// True once trip() has ever been called this run. Read this from places that
// are structurally distant from the detectors (startup gate, export builder,
// periodic sweep) and react there.
bool tripped();

// A non-crashing consequence used inside long-running work: with a low, seeded
// probability it turns a tripped state into an actual std::abort() routed
// through the crash reporter, so a cracked build dies unpredictably minutes
// later rather than at a findable instruction. No-op when not tripped.
void maybeAbortIfTripped();

// The single 64-bit value the exporter mixes into its render (see
// license_session.h). 0 when the process is clean, non-zero once tripped, so a
// bypassed copy produces corrupted output without any separate boolean to flip.
quint64 poisonSeed();

} // namespace guard

#endif // CUTPRO_GUARD_TAMPER_H
