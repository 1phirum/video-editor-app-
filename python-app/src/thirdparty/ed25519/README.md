# Vendored: orlp/ed25519

Source: https://github.com/orlp/ed25519 (public domain / zlib).

These files are copied unchanged from upstream `src/`. They provide Ed25519
signing (`ed25519_sign`, `ed25519_create_keypair`) and verification
(`ed25519_verify`), with a bundled SHA-512 (`sha512.c`) used internally by the
signature scheme. No external crypto dependency (OpenSSL/libsodium) is required.

Used by:
- the app (verify only), compiled into the `cutpro` executable target;
- the vendor-only `license_gen` tool (keygen + sign).

Built as the `cutpro_ed25519` static library (see root CMakeLists.txt). Do not
edit these files; to update, re-copy from upstream.
