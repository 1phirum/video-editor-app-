// Embedded Ed25519 public key, stored obfuscated.
//
// The 32 public-key bytes are NOT stored contiguously in the binary: they are
// split into two arrays and XOR-masked, then reassembled at runtime by
// publicKey(). This defeats a naive binary/memory scan for a known 32-byte
// key. It is obfuscation, not a security boundary.
//
// GENERATED VALUES: replace the two arrays below with the snippet printed by
//   license_gen keygen
// (built with -DCUTPRO_BUILD_LICENSE_TOOLS=ON). The placeholder below is the
// all-zero key and will reject every license until the real key is pasted in.
#include "app/license/license.h"

#include "app/license/license_codec.h"

namespace license {

namespace {
// --- BEGIN GENERATED PUBLIC KEY ---
// maskedKey[i] == realPublicKey[i] ^ mask[i]
constexpr unsigned char maskedKey[kPublicKeyBytes] = {
    0x47, 0x24, 0x3E, 0xB6, 0x45, 0x63, 0x50, 0xAA,
    0xA8, 0xCB, 0x8A, 0x9B, 0x21, 0xE6, 0x61, 0xC0,
    0x1A, 0x64, 0xE2, 0x0F, 0xDB, 0xEF, 0x83, 0xEF,
    0x52, 0x33, 0x6F, 0xE2, 0x3D, 0x83, 0x55, 0x4F,
};
constexpr unsigned char mask[kPublicKeyBytes] = {
    0xD1, 0xC9, 0x39, 0x76, 0x5B, 0x51, 0x2B, 0x27,
    0x79, 0xC1, 0x44, 0x36, 0xA0, 0xC8, 0xB9, 0x2D,
    0x47, 0xA4, 0x56, 0x24, 0xAB, 0x62, 0x2A, 0x91,
    0x3B, 0x72, 0x7F, 0x2D, 0xFB, 0x92, 0x94, 0x09,
};
// SHA-256 of the genuine public key, held apart from maskedKey/mask so the
// integrity layer can detect a swapped key. Regenerate with the key.
constexpr unsigned char publicKeySha256[32] = {
    0xd6, 0x8f, 0xb3, 0x98, 0x05, 0xbb, 0x72, 0x58,
    0xc6, 0x57, 0x17, 0xa1, 0x2a, 0x57, 0xe8, 0xd0,
    0x90, 0xc0, 0x51, 0xda, 0xba, 0x01, 0x43, 0x43,
    0xb3, 0xac, 0x06, 0xc5, 0xa3, 0x27, 0xb4, 0xfc,
};
// --- END GENERATED PUBLIC KEY ---
} // namespace

QByteArray publicKey() {
  QByteArray key;
  key.resize(kPublicKeyBytes);
  for (int i = 0; i < kPublicKeyBytes; ++i)
    key[i] = static_cast<char>(maskedKey[i] ^ mask[i]);
  return key;
}

QByteArray publicKeyDigest() {
  return QByteArray(reinterpret_cast<const char *>(publicKeySha256),
                    sizeof(publicKeySha256));
}

} // namespace license
