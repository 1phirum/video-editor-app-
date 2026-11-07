#include "tools/crash_report/minidump_writer.h"

#include <QFileInfo>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
// dbghelp.h must follow windows.h; MinGW's header does not include it itself.
#include <dbghelp.h>
#endif

namespace crashreport {

#ifdef Q_OS_WIN

bool available() { return true; }

DumpResult writeDump(const DumpRequest &request) {
  DumpResult result;
  if (!request.processHandle || request.path.isEmpty()) {
    result.error = QStringLiteral("no target");
    return result;
  }

  HANDLE file = CreateFileW(
      reinterpret_cast<const wchar_t *>(request.path.utf16()), GENERIC_WRITE, 0,
      nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    result.error = QStringLiteral("cannot create %1 (error %2)")
                       .arg(request.path)
                       .arg(GetLastError());
    return result;
  }

  // MiniDumpWithIndirectlyReferencedMemory is what makes a small dump useful:
  // it pulls in the memory the captured stacks point at, so locals and QObject
  // fields are readable instead of showing as inaccessible. ScanMemory adds the
  // module references found on those stacks, which is how frames in Qt's
  // release DLLs still resolve to a module+offset that addr2line can use.
  MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
      MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
      MiniDumpScanMemory | MiniDumpWithUnloadedModules |
      MiniDumpWithProcessThreadData);
  if (request.fullMemory)
    type = static_cast<MINIDUMP_TYPE>(type | MiniDumpWithFullMemory);

  MINIDUMP_EXCEPTION_INFORMATION exception{};
  MINIDUMP_EXCEPTION_INFORMATION *exceptionPointer = nullptr;
  if (request.exceptionPointers != 0 && request.exceptionThreadId != 0) {
    exception.ThreadId = static_cast<DWORD>(request.exceptionThreadId);
    exception.ExceptionPointers = reinterpret_cast<PEXCEPTION_POINTERS>(
        static_cast<ULONG_PTR>(request.exceptionPointers));
    // The pointer lives in the target, so the dump writer must be told to read
    // it from there rather than dereference it here.
    exception.ClientPointers = TRUE;
    exceptionPointer = &exception;
  }

  const BOOL ok = MiniDumpWriteDump(
      static_cast<HANDLE>(request.processHandle),
      static_cast<DWORD>(request.processId), file, type, exceptionPointer,
      nullptr, nullptr);
  const DWORD error = ok ? 0 : GetLastError();
  CloseHandle(file);

  if (!ok) {
    result.error = QStringLiteral("MiniDumpWriteDump failed (0x%1)")
                       .arg(error, 8, 16, QLatin1Char('0'));
    return result;
  }
  result.ok = true;
  result.bytes = QFileInfo(request.path).size();
  return result;
}

#else // !Q_OS_WIN

bool available() { return false; }

DumpResult writeDump(const DumpRequest &) {
  DumpResult result;
  result.error = QStringLiteral("minidumps are Windows only");
  return result;
}

#endif

} // namespace crashreport
