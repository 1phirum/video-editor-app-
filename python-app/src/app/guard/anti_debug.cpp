#include "app/guard/anti_debug.h"

#include "app/guard/tamper.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace guard {

#ifdef Q_OS_WIN
namespace {

// NtQueryInformationProcess is resolved at runtime rather than linked, so the
// import table does not advertise "this binary is looking for a debug port".
using NtQIP_t = LONG(WINAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);

constexpr ULONG kProcessDebugPort = 7;
constexpr ULONG kProcessDebugFlags = 31;
constexpr ULONG kProcessDebugObjectHandle = 30;

NtQIP_t resolveNtQIP() {
  HMODULE nt = GetModuleHandleW(L"ntdll.dll");
  if (!nt)
    return nullptr;
  return reinterpret_cast<NtQIP_t>(
      reinterpret_cast<void *>(GetProcAddress(nt, "NtQueryInformationProcess")));
}

bool pebBeingDebugged() {
  // The PEB flag IsDebuggerPresent() reads, but inlined so a hook on the API
  // export does not hide it.
#if defined(_M_X64) || defined(__x86_64__)
  auto *peb = reinterpret_cast<BYTE *>(__readgsqword(0x60));
  return peb && peb[2] != 0; // PEB->BeingDebugged
#else
  return IsDebuggerPresent() != FALSE;
#endif
}

bool remoteDebugger() {
  BOOL present = FALSE;
  return CheckRemoteDebuggerPresent(GetCurrentProcess(), &present) && present;
}

bool debugPortOrObject(NtQIP_t ntqip) {
  if (!ntqip)
    return false;
  DWORD_PTR port = 0;
  if (ntqip(GetCurrentProcess(), kProcessDebugPort, &port, sizeof(port),
            nullptr) == 0 &&
      port != 0)
    return true;

  HANDLE obj = nullptr;
  if (ntqip(GetCurrentProcess(), kProcessDebugObjectHandle, &obj, sizeof(obj),
            nullptr) == 0 &&
      obj != nullptr)
    return true;

  DWORD flags = 0;
  // ProcessDebugFlags reads back 0 when a debugger is attached (the kernel
  // clears NoDebugInherit), so a zero here is the positive.
  if (ntqip(GetCurrentProcess(), kProcessDebugFlags, &flags, sizeof(flags),
            nullptr) == 0 &&
      flags == 0)
    return true;

  return false;
}

} // namespace

bool sweep() {
  static NtQIP_t ntqip = resolveNtQIP();
  const bool detected =
      IsDebuggerPresent() || pebBeingDebugged() || remoteDebugger() ||
      debugPortOrObject(ntqip);
  if (detected)
    trip(TamperReason::DebuggerPresent);
  return detected;
}

#else  // non-Windows: not a shipping target.
bool sweep() { return false; }
#endif // Q_OS_WIN

} // namespace guard
