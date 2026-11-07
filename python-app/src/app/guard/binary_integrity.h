// Runtime binary-integrity verification.
//
// Two independent anchors, both free and both reusing the Ed25519 key the
// licence system already trusts:
//
//  1. A signed manifest (integrity.man) shipped beside cutpro.exe listing the
//     SHA-256 of every executable/DLL/tool in the install. The manifest is
//     signed by the vendor's Ed25519 private key (the same key that mints
//     licences) and verified here with the embedded public key. A patched DLL
//     changes a hash; a cracker cannot re-sign the manifest without the private
//     key. Produced per release by `license_gen stamp` (vendor-only).
//
//  2. Authenticode: WinVerifyTrust on the running image and its DLLs. Patching
//     a signed binary breaks its signature. Only enforced once the build is
//     code-signed (CUTPRO_REQUIRE_SIGNATURE), so an unsigned dev build is not
//     punished.
//
// Plus a self-check that the reassembled licence public key still hashes to its
// known digest, so swapping in an attacker's key (to self-sign both licences
// and the manifest) is itself detected.
//
// Everything routes through tamper::trip(); nothing aborts on these lines.
#ifndef CUTPRO_GUARD_BINARY_INTEGRITY_H
#define CUTPRO_GUARD_BINARY_INTEGRITY_H

#include <QString>

namespace guard {

// Manifest file name, expected in the application directory.
inline constexpr const char *kManifestName = "integrity.man";

// Verify the signed manifest against the files on disk. On any failure (missing
// or bad-signature manifest, or a file whose hash does not match) trips tamper
// state. When built without CUTPRO_ENFORCE_INTEGRITY this is a no-op, so dev
// builds without a manifest run normally. appDir defaults to the running app's
// directory.
void verifyManifest(const QString &appDir = QString());

// Verify the embedded licence public key still matches its known digest. Trips
// PublicKeySwapped on mismatch. Safe to call from many sites.
void verifyEmbeddedPublicKey();

// Full startup sweep: public-key self-check, manifest, and (when signed)
// Authenticode. Call once early in main() and again from the periodic timer.
void verifyAll();

} // namespace guard

#endif // CUTPRO_GUARD_BINARY_INTEGRITY_H
