#pragma once

#include <QString>

// Writes a minidump of *another* process.
//
// This is the whole reason the reporter is a separate executable. Dumping from
// inside the target requires the target to be running well enough to call
// dbghelp - which allocates and takes the loader lock - and a hung process by
// definition is not. Called with a foreign process handle, MiniDumpWriteDump
// suspends every thread in the target, walks all of their stacks and resumes
// them, with no cooperation from the target at all.
namespace crashreport {

struct DumpRequest {
  // Target. The handle needs PROCESS_QUERY_INFORMATION and PROCESS_VM_READ.
  void *processHandle = nullptr;
  unsigned int processId = 0;
  // Thread that raised the exception, and the EXCEPTION_POINTERS address *in
  // the target's address space*. Both zero for a hang dump, which is a normal
  // dump with no exception stream.
  unsigned int exceptionThreadId = 0;
  unsigned long long exceptionPointers = 0;
  QString path;
  // Full memory turns a 2 GB editor into a 2 GB file, which is not something to
  // drop into a user's AppData without asking. The default set is thread
  // contexts, stacks, module list and whatever those stacks point at - enough
  // to symbolise every frame, usually tens of megabytes.
  bool fullMemory = false;
};

struct DumpResult {
  bool ok = false;
  qint64 bytes = 0;
  QString error;
};

DumpResult writeDump(const DumpRequest &request);

// True when dbghelp could be resolved. False builds still produce the text
// report, which is the part that names the QML culprit.
bool available();

} // namespace crashreport
