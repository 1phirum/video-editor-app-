#include "app/license/machine_fingerprint.h"

#include "app/license/license_codec.h"

#include <QCryptographicHash>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace license {

namespace {

#ifdef Q_OS_WIN
// HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid, a REG_SZ GUID written at
// OS install. Force the 64-bit registry view so a 32-bit build (or WoW
// redirection) can't read a different value.
QByteArray readMachineGuid() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Cryptography", 0,
                    KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
    return QByteArray();

  wchar_t buffer[256];
  DWORD size = sizeof(buffer);
  DWORD type = 0;
  QByteArray guid;
  if (RegQueryValueExW(key, L"MachineGuid", nullptr, &type,
                       reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS &&
      type == REG_SZ) {
    guid = QString::fromWCharArray(buffer).trimmed().toUtf8();
  }
  RegCloseKey(key);
  return guid;
}

// Volume serial of the drive Windows is installed on (resolved from
// GetSystemDirectoryW rather than assuming C:).
quint32 readSystemVolumeSerial() {
  wchar_t sysDir[MAX_PATH];
  const UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
  if (len == 0 || len >= MAX_PATH)
    return 0;

  // Root path: drive letter + ":\".
  wchar_t root[4] = {sysDir[0], L':', L'\\', L'\0'};
  DWORD serial = 0;
  if (!GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr,
                             nullptr, 0))
    return 0;
  return static_cast<quint32>(serial);
}
#endif // Q_OS_WIN

} // namespace

QByteArray fingerprintMessage() {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  // Domain-separation tag: bumping "v1" deliberately invalidates old keys.
  hash.addData(QByteArrayLiteral("CutPro-v1|"));

#ifdef Q_OS_WIN
  hash.addData(readMachineGuid());
  hash.addData(QByteArrayLiteral("|"));
  const quint32 serial = readSystemVolumeSerial();
  hash.addData(QByteArrayView(reinterpret_cast<const char *>(&serial),
                              sizeof(serial)));
#else
  // Non-Windows builds are not a shipping target; keep a deterministic value
  // so the code compiles and the flow can be exercised.
  hash.addData(QByteArrayLiteral("non-windows-build"));
#endif

  return hash.result().left(kFingerprintBytes);
}

QString machineIdDisplay() {
  return group(base32Encode(fingerprintMessage()), 4);
}

} // namespace license
