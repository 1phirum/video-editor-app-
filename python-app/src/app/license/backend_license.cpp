// Backend license methods: machine id, activation, startup verification, and a
// second (guard) verification site.
//
// This is the app's only writer of the license record. Verification proper
// lives in license.cpp; here we bridge it to the Backend's QML-facing API and
// to the registry, mirroring the applyAppSettings validate->persist->emit idiom
// in backend_settings.cpp.
#include "app/core_app/backend.h"

#include "app/guard/binary_integrity.h"
#include "app/guard/tamper.h"
#include "app/license/license.h"
#include "app/license/machine_fingerprint.h"

#include "app/license/license_codec.h"

#include <QDate>
#include <QDateTime>
#include <QSettings>
#include <QTimeZone>
#include <cstdlib>
#include <memory>

namespace {
// Same store the rest of the app uses (org "Antigravity" / app "Cut Pro" ->
// HKCU\Software\Antigravity\Cut Pro on Windows), honoring the test override.
// The license record lives under its own "License" group.
std::unique_ptr<QSettings> licenseStore() {
  const QString testFile = qEnvironmentVariable("CUTPRO_SETTINGS_FILE");
  if (!testFile.isEmpty())
    return std::make_unique<QSettings>(testFile, QSettings::IniFormat);
  return std::make_unique<QSettings>();
}

QString readStoredKey() {
  auto store = licenseStore();
  store->beginGroup(QStringLiteral("License"));
  const QString key = store->value(QStringLiteral("key")).toString();
  store->endGroup();
  return key;
}

// A cheap checksum of the stored key, kept in m_licenseGuard so a second site
// can notice if m_licensed was flipped to true without a matching key.
quint64 guardFor(const QString &key) {
  quint64 h = 1469598103934665603ULL; // FNV-1a
  const QByteArray bytes = key.toUtf8();
  for (unsigned char c : bytes) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  // Fold in a constant so a zeroed field is not a valid guard for "".
  return h ^ 0x9E3779B97F4A7C15ULL;
}
} // namespace

QString Backend::machineId() const { return license::machineIdDisplay(); }

QVariantMap Backend::licenseInfo() const {
  QVariantMap info;
  info.insert(QStringLiteral("licensed"), m_licensed);
  info.insert(QStringLiteral("machineId"), license::machineIdDisplay());

  const quint32 days = license::expiryDaysOf(readStoredKey());
  const bool perpetual = (days == 0);
  info.insert(QStringLiteral("perpetual"), perpetual);
  if (perpetual) {
    info.insert(QStringLiteral("expiryDate"), QString());
    info.insert(QStringLiteral("expiryMsecs"), QVariant::fromValue<qint64>(0));
  } else {
    const QDate expiry = QDate(1970, 1, 1).addDays(days);
    info.insert(QStringLiteral("expiryDate"), expiry.toString(Qt::ISODate));
    // The key is valid through the expiry day inclusive, so it lapses at the
    // start of the following day (local time).
    const QDateTime lapse(expiry.addDays(1), QTime(0, 0), QTimeZone::LocalTime);
    info.insert(QStringLiteral("expiryMsecs"),
                QVariant::fromValue<qint64>(lapse.toMSecsSinceEpoch()));
  }
  return info;
}

bool Backend::verifyStoredLicense() {
  const QString key = readStoredKey();
  const bool ok = !key.isEmpty() && license::verify(key);
  m_licenseGuard = ok ? guardFor(key) : 0;
  return ok;
}

void Backend::enforceLicenseGuard() {
  // Also re-check that nobody swapped the embedded public key for their own.
  guard::verifyEmbeddedPublicKey();

  const QString key = readStoredKey();
  const bool reallyLicensed = !key.isEmpty() && license::verify(key);
  const quint64 expected = reallyLicensed ? guardFor(key) : 0;
  // If the in-memory gate claims licensed but a fresh verify disagrees, or the
  // guard checksum was tampered, the app has been patched. Do NOT abort on this
  // line: that hands a cracker the exact address to NOP. Instead arm the
  // process poison flag (which grays out and stamps every export, and is what
  // the jittered timer's maybeAbortIfTripped() reads) and let the consequence
  // land later, elsewhere, unpredictably.
  if (m_licensed != reallyLicensed || m_licenseGuard != expected) {
    guard::trip(guard::TamperReason::LicenseForged);
    guard::maybeAbortIfTripped();
  }
}

bool Backend::activateLicense(const QString &key) {
  const QString trimmed = key.trimmed();
  const license::Status status = license::check(trimmed);
  if (status == license::Status::Expired) {
    setError(QStringLiteral("This license key has expired."));
    return false;
  }
  if (status != license::Status::Valid) {
    setError(QStringLiteral("This license key is not valid for this machine."));
    return false;
  }

  auto store = licenseStore();
  store->beginGroup(QStringLiteral("License"));
  store->setValue(QStringLiteral("key"), trimmed);
  store->endGroup();
  store->sync();
  if (store->status() != QSettings::NoError) {
    setError(QStringLiteral("Could not save the license. Check permissions."));
    return false;
  }

  m_licenseGuard = guardFor(trimmed);
  if (!m_licensed) {
    m_licensed = true;
    emit licensedChanged();
  }
  return true;
}
