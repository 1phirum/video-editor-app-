#include "app/guard/license_session.h"

#include "app/guard/binary_integrity.h"
#include "app/guard/tamper.h"
#include "app/license/license.h"

#include <QSettings>
#include <QString>

namespace guard {
namespace {

// Same registry location Backend uses (org "Antigravity" / app "Cut Pro"),
// honouring the test override. Re-implemented here on purpose so this reader is
// independent of backend_license.cpp.
QString storedKey() {
  const QString testFile = qEnvironmentVariable("CUTPRO_SETTINGS_FILE");
  QSettings store =
      testFile.isEmpty()
          ? QSettings()
          : QSettings(testFile, QSettings::IniFormat);
  store.beginGroup(QStringLiteral("License"));
  const QString key = store.value(QStringLiteral("key")).toString();
  store.endGroup();
  return key;
}

} // namespace

bool licensedNow() {
  const QString key = storedKey();
  const bool ok = !key.isEmpty() && license::verify(key);
  if (!ok)
    trip(TamperReason::LicenseForged);
  return ok;
}

quint64 exportGuardSeed() {
  // Fold in the key public-key self-check too, so an export triggers the same
  // swapped-key detection every other site does.
  verifyEmbeddedPublicKey();

  const QString key = storedKey();
  const bool genuine = !key.isEmpty() && license::verify(key);
  if (genuine && !tripped())
    return 0;

  // Not genuine (or already tripped). Ensure the poison flag is armed and hand
  // back a stable non-zero seed the exporter can key its watermark off.
  trip(TamperReason::LicenseForged);
  return poisonSeed();
}

} // namespace guard
