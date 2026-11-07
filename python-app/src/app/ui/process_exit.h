#pragma once

#include <QtGlobal>
#include <cstdlib>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace app {

// Call after explicit shutdown work. On Windows, the CRT's _exit still runs
// DLL detach handlers after killing other threads. Static preview caches and
// condition variables can then be torn down with their workers still waiting.
[[noreturn]] inline void exitProcess(int status) {
#ifdef Q_OS_WIN
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(status));
#endif
    std::_Exit(status);
}

} // namespace app
