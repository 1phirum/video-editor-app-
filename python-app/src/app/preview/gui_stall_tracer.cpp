#include "app/preview/gui_stall_tracer.h"

#include <QByteArray>
#include <QtGlobal>

#include <atomic>
#include <cstdlib>

#if defined(__GNUC__)
// MinGW's libstdc++ ships the Itanium ABI demangler; MSVC has no equivalent and
// does not need one, since dbghelp undecorates its own names.
#include <cxxabi.h>
#define CUTPRO_HAVE_CXA_DEMANGLE 1
#endif

#ifdef Q_OS_WIN
// windows.h has to come first: dbghelp.h uses its types and does not include it.
#include <windows.h>

#include <dbghelp.h>
#endif

namespace {

#ifdef Q_OS_WIN

// A duplicated handle, not GetCurrentThread()'s pseudo-handle - that one resolves
// to whichever thread asks, so the monitor would suspend itself.
std::atomic<HANDLE> g_guiThreadHandle{nullptr};
std::atomic<DWORD> g_guiThreadId{0};
std::atomic_bool g_symbolsReady{false};
std::atomic_bool g_symbolsFailed{false};
std::atomic_bool g_disabled{false};

// dbghelp is single-threaded by contract. Only the monitor thread symbolises, so
// there is deliberately no lock here; adding one would imply this is shared.
bool ensureSymbols() {
  if (g_symbolsReady.load(std::memory_order_acquire))
    return true;
  if (g_symbolsFailed.load(std::memory_order_acquire))
    return false;
  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME |
                SYMOPT_FAIL_CRITICAL_ERRORS);
  // TRUE enumerates the modules already loaded. Deferred loads keep that cheap:
  // nothing is parsed until an address inside a module is looked up.
  if (!SymInitialize(GetCurrentProcess(), nullptr, TRUE)) {
    g_symbolsFailed.store(true, std::memory_order_release);
    return false;
  }
  g_symbolsReady.store(true, std::memory_order_release);
  return true;
}

// The only part that runs with the GUI thread frozen, and so the only part whose
// cost matters. No allocation, no symbol lookup, no lock that the frozen thread
// could already be holding - just a bounded walk of its return addresses.
int unwindSuspended(HANDLE thread, DWORD64 *frames, int maxFrames) {
#if defined(__x86_64__) || defined(_M_X64)
  if (SuspendThread(thread) == static_cast<DWORD>(-1))
    return 0;

  int count = 0;
  CONTEXT context{};
  context.ContextFlags = CONTEXT_FULL;
  if (GetThreadContext(thread, &context)) {
    DWORD64 previousStackPointer = 0;
    while (count < maxFrames && context.Rip != 0) {
      frames[count++] = context.Rip;
      // A frame that did not move the stack pointer upwards means the unwind is
      // not making progress, and following it would loop until maxFrames.
      if (context.Rsp <= previousStackPointer)
        break;
      previousStackPointer = context.Rsp;

      DWORD64 imageBase = 0;
      PRUNTIME_FUNCTION function =
          RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
      if (function) {
        void *handlerData = nullptr;
        DWORD64 establisherFrame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function,
                         &context, &handlerData, &establisherFrame, nullptr);
        continue;
      }
      // A leaf function has no unwind data, so its return address is simply at
      // the top of the stack. Read it through ReadProcessMemory rather than
      // dereferencing: a stack pointer that is not a valid address then fails
      // the capture instead of taking the process down with it.
      DWORD64 returnAddress = 0;
      SIZE_T read = 0;
      if (!ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<LPCVOID>(context.Rsp),
                             &returnAddress, sizeof(returnAddress), &read) ||
          read != sizeof(returnAddress))
        break;
      context.Rip = returnAddress;
      context.Rsp += sizeof(returnAddress);
    }
  }

  ResumeThread(thread);
  return count;
#else
  Q_UNUSED(thread);
  Q_UNUSED(frames);
  Q_UNUSED(maxFrames);
  return 0;
#endif
}

// dbghelp undecorates MSVC names but knows nothing about the Itanium ABI that
// GCC uses, so a frame in our own DLL or in a Qt DLL built with MinGW comes back
// as _ZN11QQmlBinding8evaluateEv. Unreadable at a glance, and the whole point of
// this file is to be readable at a glance.
QString demangle(const QByteArray &name) {
  if (name.isEmpty())
    return {};
#ifdef CUTPRO_HAVE_CXA_DEMANGLE
  auto tryDemangle = [](const char *candidate) -> QString {
    int status = 0;
    char *readable = abi::__cxa_demangle(candidate, nullptr, nullptr, &status);
    if (status != 0 || !readable) {
      // The buffer is owned by the caller even when the status is non-zero.
      if (readable)
        std::free(readable);
      return {};
    }
    const QString result = QString::fromLocal8Bit(readable);
    std::free(readable);
    return result;
  };

  const QString direct = tryDemangle(name.constData());
  if (!direct.isEmpty())
    return direct;
  // dbghelp hands back export-table names with the leading underscore already
  // stripped, which is exactly the character __cxa_demangle looks for first.
  if (!name.startsWith('_')) {
    const QString restored = tryDemangle(QByteArray("_") + name);
    if (!restored.isEmpty())
      return restored;
  }
#endif
  return QString::fromLocal8Bit(name);
}

// Runs after the thread is going again, so it is free to allocate and to call
// into dbghelp. Everything our own backend exports resolves by name here:
// cutpro_backend.dll is built with WINDOWS_EXPORT_ALL_SYMBOLS, so its export
// table alone is enough even without a PDB, and the Qt DLLs export their public
// API the same way. Frames that resolve to neither are reported as
// module+0xoffset, which addr2line turns back into file:line against this build.
QString describeAddress(DWORD64 address) {
  QString moduleName;
  QString offsetInModule;
  HMODULE module = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(address), &module) &&
      module) {
    char path[MAX_PATH] = {};
    if (GetModuleFileNameA(module, path, sizeof(path) - 1) > 0) {
      moduleName = QString::fromLocal8Bit(path);
      const int slash = moduleName.lastIndexOf(QLatin1Char('\\'));
      if (slash >= 0)
        moduleName = moduleName.mid(slash + 1);
    }
    offsetInModule = QStringLiteral("+0x%1")
                         .arg(address - reinterpret_cast<DWORD64>(module), 0, 16);
  }

  QString symbol;
  bool symbolIsNear = false;
  if (ensureSymbols()) {
    // SYMBOL_INFO is variable length - the name is written past the struct, so
    // the buffer has to carry room for it.
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto *info = reinterpret_cast<SYMBOL_INFO *>(buffer);
    info->SizeOfStruct = sizeof(SYMBOL_INFO);
    info->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), address, &displacement, info)) {
      // NameLen explicitly: Name is declared CHAR[1] and grows past the struct,
      // so anything that infers a length from the declared array type reads one
      // character and stops.
      symbol = demangle(QByteArray(info->Name, static_cast<int>(info->NameLen)));
      if (displacement != 0)
        symbol += QStringLiteral("+0x%1").arg(displacement, 0, 16);
      // Without a PDB the only symbols are exports, so a large displacement means
      // the real function is some unexported one after the named export, not that
      // the named export is enormous. Saying "nearest" rather than naming it
      // outright is the difference between a lead and a wrong answer.
      symbolIsNear = displacement >= 0x1000;
    }
  }

  if (symbol.isEmpty())
    return moduleName.isEmpty() ? QStringLiteral("0x%1").arg(address, 0, 16)
                                : moduleName + offsetInModule;
  if (moduleName.isEmpty())
    return symbol;
  if (symbolIsNear)
    return QStringLiteral("%1%2 (nearest export: %3)")
        .arg(moduleName, offsetInModule, symbol);
  return QStringLiteral("%1!%2").arg(moduleName, symbol);
}

#endif // Q_OS_WIN

} // namespace

void GuiStallTracer::rememberGuiThread() {
#ifdef Q_OS_WIN
  if (qEnvironmentVariableIntValue("CUTPRO_NO_STALL_TRACE") > 0) {
    g_disabled.store(true, std::memory_order_release);
    return;
  }
  HANDLE duplicate = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                       GetCurrentProcess(), &duplicate,
                       THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                           THREAD_QUERY_INFORMATION,
                       FALSE, 0))
    return;
  g_guiThreadId.store(GetCurrentThreadId(), std::memory_order_release);
  HANDLE previous =
      g_guiThreadHandle.exchange(duplicate, std::memory_order_acq_rel);
  if (previous)
    CloseHandle(previous);
#endif
}

bool GuiStallTracer::available() {
#ifdef Q_OS_WIN
  return !g_disabled.load(std::memory_order_acquire) &&
         g_guiThreadHandle.load(std::memory_order_acquire) != nullptr;
#else
  return false;
#endif
}

QStringList GuiStallTracer::captureGuiBacktrace() {
#ifdef Q_OS_WIN
  if (!available())
    return {};
  HANDLE thread = g_guiThreadHandle.load(std::memory_order_acquire);
  // Suspending the calling thread would hang the process outright, and this is
  // cheap insurance against a future caller that forgets which thread it is on.
  if (GetCurrentThreadId() == g_guiThreadId.load(std::memory_order_acquire))
    return {};

  DWORD64 frames[kMaxFrames] = {};
  const int count = unwindSuspended(thread, frames, kMaxFrames);

  QStringList described;
  described.reserve(count);
  for (int i = 0; i < count; ++i)
    described.append(describeAddress(frames[i]));
  return described;
#else
  return {};
#endif
}
