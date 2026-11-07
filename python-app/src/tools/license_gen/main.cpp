// license_gen - the vendor-only, offline Cut Pro license generator.
//
// This tool is NEVER shipped. It is built only when the app is configured with
// -DCUTPRO_BUILD_LICENSE_TOOLS=ON, is not a dependency of the cutpro target,
// and is not copied by any deploy step. The Ed25519 key material lives in a
// file the vendor keeps outside the repo; this tool loads it on demand.
//
// Usage:
//   license_gen keygen
//       Generate a fresh Ed25519 keypair. Prints the secret key material (hex;
//       SAVE IT to a secret file, e.g. license_priv.key) and a ready-to-paste
//       C++ snippet of the obfuscated public key for
//       src/app/license/license_pubkey.cpp. Run this ONCE, ever.
//
//   license_gen sign <MACHINE-ID> --key <keyfile>
//       Mint a license key for a machine. MACHINE-ID is the string the app
//       shows the user. Prints the CP1-... license key to paste back.
//       The key file may also be supplied via CUTPRO_LICENSE_PRIVKEY (a path
//       or the raw hex).
//
// Key file format: 96 hex-encoded bytes = public key (32) || private key (64).
// orlp's ed25519_sign needs both halves, so we keep them together.
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMap>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "app/license/license_codec.h"

extern "C" {
#include "ed25519.h"
}

using namespace license;

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

QString toHex(const QByteArray &data) {
  return QString::fromLatin1(data.toHex());
}

// Print the public key as an obfuscated (XOR-masked, split) snippet ready to
// paste into license_pubkey.cpp.
void printPublicKeySnippet(const QByteArray &pub) {
  QByteArray mask(kPublicKeyBytes, 0);
  auto *rng = QRandomGenerator::system();
  for (int i = 0; i < kPublicKeyBytes; ++i)
    mask[i] = static_cast<char>(rng->bounded(256));

  QByteArray masked(kPublicKeyBytes, 0);
  for (int i = 0; i < kPublicKeyBytes; ++i)
    masked[i] = static_cast<char>(pub[i] ^ mask[i]);

  auto emitArray = [&](const char *name, const QByteArray &bytes) {
    out << "constexpr unsigned char " << name << "[kPublicKeyBytes] = {\n";
    for (int i = 0; i < bytes.size(); ++i) {
      if (i % 8 == 0)
        out << "    ";
      out << QString::asprintf("0x%02X, ",
                               static_cast<unsigned char>(bytes[i]));
      if (i % 8 == 7)
        out << "\n";
    }
    out << "};\n";
  };

  out << "\n// --- BEGIN GENERATED PUBLIC KEY ---\n";
  emitArray("maskedKey", masked);
  emitArray("mask", mask);
  // SHA-256 of the genuine public key, stored apart so the integrity layer can
  // detect a swapped key (see license::publicKeyDigest).
  const QByteArray digest =
      QCryptographicHash::hash(pub, QCryptographicHash::Sha256);
  out << "// SHA-256 of the genuine public key (publicKeyDigest):\n";
  out << "constexpr unsigned char publicKeySha256[32] = {\n";
  for (int i = 0; i < digest.size(); ++i) {
    if (i % 8 == 0)
      out << "    ";
    out << QString::asprintf("0x%02x, ", static_cast<unsigned char>(digest[i]));
    if (i % 8 == 7)
      out << "\n";
  }
  out << "};\n";
  out << "// --- END GENERATED PUBLIC KEY ---\n";
}

int doKeygen() {
  unsigned char seed[32];
  auto *rng = QRandomGenerator::system(); // OS CSPRNG on Windows
  for (int i = 0; i < 32; ++i)
    seed[i] = static_cast<unsigned char>(rng->bounded(256));

  unsigned char pub[kPublicKeyBytes];
  unsigned char priv[64];
  ed25519_create_keypair(pub, priv, seed);

  const QByteArray pubBytes(reinterpret_cast<char *>(pub), kPublicKeyBytes);
  const QByteArray privBytes(reinterpret_cast<char *>(priv), 64);
  const QByteArray keyfile = pubBytes + privBytes; // 96 bytes: pub || priv

  out << "=== Ed25519 keypair (KEEP THE KEY FILE SECRET) ===\n\n";
  out << "KEY FILE (hex) - save this to a file, e.g. license_priv.key, and\n";
  out << "pass it to `license_gen sign ... --key license_priv.key`:\n";
  out << toHex(keyfile) << "\n";
  out << "\nPUBLIC KEY (hex): " << toHex(pubBytes) << "\n";
  printPublicKeySnippet(pubBytes);
  out.flush();
  return 0;
}

// Load the 96-byte key material (pub || priv) from a file path or raw hex.
QByteArray loadKeyMaterial(const QString &source) {
  QByteArray hex;
  QFile file(source);
  if (file.exists()) {
    if (!file.open(QIODevice::ReadOnly)) {
      err << "error: cannot open key file: " << source << "\n";
      return QByteArray();
    }
    hex = file.readAll().trimmed();
  } else {
    hex = source.trimmed().toLatin1(); // treat the argument as raw hex
  }
  const QByteArray key = QByteArray::fromHex(hex);
  if (key.size() != kPublicKeyBytes + 64) {
    err << "error: key file must be " << (kPublicKeyBytes + 64)
        << " bytes (pub||priv); got " << key.size() << " bytes.\n"
        << "Regenerate it with `license_gen keygen`.\n";
    return QByteArray();
  }
  return key;
}

int doSign(const QStringList &args) {
  QString machineId;
  QString keySource = qEnvironmentVariable("CUTPRO_LICENSE_PRIVKEY");
  int days = 0, months = 0, years = 0;
  QString expiresOn; // YYYY-MM-DD
  for (int i = 0; i < args.size(); ++i) {
    if (args[i] == "--key" && i + 1 < args.size())
      keySource = args[++i];
    else if (args[i] == "--days" && i + 1 < args.size())
      days = args[++i].toInt();
    else if (args[i] == "--months" && i + 1 < args.size())
      months = args[++i].toInt();
    else if (args[i] == "--years" && i + 1 < args.size())
      years = args[++i].toInt();
    else if (args[i] == "--expires" && i + 1 < args.size())
      expiresOn = args[++i];
    else if (!args[i].startsWith("--") && machineId.isEmpty())
      machineId = args[i];
  }

  if (machineId.isEmpty()) {
    err << "usage: license_gen sign <MACHINE-ID> --key <keyfile>\n"
        << "            [--days N | --months N | --years N | --expires "
           "YYYY-MM-DD]\n"
        << "       (no expiry option = perpetual)\n";
    return 2;
  }

  // Resolve the expiry to a day count since 1970-01-01. 0 == perpetual.
  quint32 expiryDays = 0;
  if (!expiresOn.isEmpty()) {
    const QDate date = QDate::fromString(expiresOn, Qt::ISODate);
    if (!date.isValid()) {
      err << "error: --expires must be YYYY-MM-DD\n";
      return 1;
    }
    expiryDays = static_cast<quint32>(QDate(1970, 1, 1).daysTo(date));
  } else if (days || months || years) {
    const QDate date = QDate::currentDate().addDays(days).addMonths(months).addYears(years);
    expiryDays = static_cast<quint32>(QDate(1970, 1, 1).daysTo(date));
  }
  if (keySource.isEmpty()) {
    err << "error: no key file. Pass --key <file> or set "
           "CUTPRO_LICENSE_PRIVKEY.\n";
    return 2;
  }

  const QByteArray material = loadKeyMaterial(keySource);
  if (material.isEmpty())
    return 1;
  const QByteArray pub = material.left(kPublicKeyBytes);
  const QByteArray priv = material.mid(kPublicKeyBytes);

  // The Machine ID string IS the signed message (base32 of the fingerprint
  // bytes). Decode it back to the exact bytes the app will verify against.
  const QByteArray message = base32Decode(machineId);
  if (message.size() != kFingerprintBytes) {
    err << "error: Machine ID decoded to " << message.size()
        << " bytes, expected " << kFingerprintBytes
        << ". Check for typos.\n";
    return 1;
  }

  // Sign (fingerprint || expiry); the key payload carries expiry || signature.
  const QByteArray signMe = signedMessage(message, expiryDays);
  unsigned char signature[kSignatureBytes];
  ed25519_sign(signature,
               reinterpret_cast<const unsigned char *>(signMe.constData()),
               static_cast<size_t>(signMe.size()),
               reinterpret_cast<const unsigned char *>(pub.constData()),
               reinterpret_cast<const unsigned char *>(priv.constData()));

  const QByteArray sigBytes(reinterpret_cast<char *>(signature),
                            kSignatureBytes);
  const QByteArray payload = encodeExpiryDays(expiryDays) + sigBytes;
  const QString key = QString::fromLatin1(kKeyPrefix) +
                      group(base32Encode(payload), 8);

  out << "License key for " << machineId << ":\n\n" << key << "\n";
  if (expiryDays == 0)
    out << "\nExpiry: perpetual (never expires)\n";
  else
    out << "\nExpiry: "
        << QDate(1970, 1, 1).addDays(expiryDays).toString(Qt::ISODate) << "\n";
  out.flush();
  return 0;
}

// Hash one file (streamed, so a multi-GB payload never loads into memory).
QByteArray sha256File(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return QByteArray();
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file))
    return QByteArray();
  return hash.result();
}

// stamp: walk an install directory, hash every shipped file, and write a
// signed integrity.man the app's cutpro_guard layer verifies at startup.
//
// Run this LAST in the release pipeline - AFTER Authenticode-signing the
// binaries - because the manifest hashes the final bytes on disk. Any later
// change to a listed file makes the app treat itself as patched.
int doStamp(const QStringList &args) {
  QString dirPath;
  QString keySource = qEnvironmentVariable("CUTPRO_LICENSE_PRIVKEY");
  for (int i = 0; i < args.size(); ++i) {
    if (args[i] == "--key" && i + 1 < args.size())
      keySource = args[++i];
    else if (!args[i].startsWith("--") && dirPath.isEmpty())
      dirPath = args[i];
  }

  if (dirPath.isEmpty()) {
    err << "usage: license_gen stamp <install-dir> --key <keyfile>\n";
    return 2;
  }
  if (keySource.isEmpty()) {
    err << "error: no key file. Pass --key <file> or set "
           "CUTPRO_LICENSE_PRIVKEY.\n";
    return 2;
  }

  QDir root(dirPath);
  if (!root.exists()) {
    err << "error: install dir does not exist: " << dirPath << "\n";
    return 1;
  }

  const QByteArray material = loadKeyMaterial(keySource);
  if (material.isEmpty())
    return 1;
  const QByteArray pub = material.left(kPublicKeyBytes);
  const QByteArray priv = material.mid(kPublicKeyBytes);

  // Collect (relpath, hash), skipping the manifest itself - it cannot list its
  // own hash, and it is rewritten every stamp. Relative paths use '/' so the
  // manifest is identical no matter which OS mints it.
  const QString manifest = QStringLiteral("integrity.man");
  QMap<QString, QByteArray> entries; // sorted by key = stable manifest order
  QDirIterator it(root.absolutePath(), QDir::Files | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString abs = it.next();
    const QString rel = root.relativeFilePath(abs);
    if (rel == manifest)
      continue;
    const QByteArray digest = sha256File(abs);
    if (digest.isEmpty()) {
      err << "error: cannot read " << abs << "\n";
      return 1;
    }
    entries.insert(rel, digest);
  }
  if (entries.isEmpty()) {
    err << "error: no files found under " << dirPath << "\n";
    return 1;
  }

  // Body the app signs and re-hashes. The signed region is exactly these bytes
  // (header + lines, including the final '\n'); "SIG <hex>\n" is appended after.
  QByteArray body = "CUTPRO-INTEGRITY-1\n";
  for (auto e = entries.constBegin(); e != entries.constEnd(); ++e)
    body += e.value().toHex() + " " + e.key().toUtf8() + "\n";

  unsigned char signature[kSignatureBytes];
  ed25519_sign(signature,
               reinterpret_cast<const unsigned char *>(body.constData()),
               static_cast<size_t>(body.size()),
               reinterpret_cast<const unsigned char *>(pub.constData()),
               reinterpret_cast<const unsigned char *>(priv.constData()));
  const QByteArray sigBytes(reinterpret_cast<char *>(signature),
                            kSignatureBytes);

  QByteArray blob = body;
  blob += "SIG " + sigBytes.toHex() + "\n";

  const QString outPath = root.absoluteFilePath(manifest);
  QFile outFile(outPath);
  if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    err << "error: cannot write manifest: " << outPath << "\n";
    return 1;
  }
  outFile.write(blob);
  outFile.close();

  out << "Wrote " << outPath << "\n"
      << entries.size() << " files hashed and signed.\n";
  out.flush();
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const QStringList args = app.arguments();
  if (args.size() < 2) {
    err << "usage: license_gen <keygen|sign|stamp> ...\n";
    return 2;
  }
  const QString mode = args[1];
  if (mode == "keygen")
    return doKeygen();
  if (mode == "sign")
    return doSign(args.mid(2));
  if (mode == "stamp")
    return doStamp(args.mid(2));
  err << "unknown mode: " << mode << "\n";
  return 2;
}
