#include "app/diagnostics/crash_channel.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>

#include <atomic>
#include <cstring>

#include "core/version.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace diag {
namespace {

struct State {
  ChannelData *view = nullptr;
  QString mappingName;
  QString crashEventName;
  QString dumpDoneEventName;
#ifdef Q_OS_WIN
  HANDLE mapping = nullptr;
  HANDLE crashEvent = nullptr;
  HANDLE dumpDoneEvent = nullptr;
#endif
  // The app's own zero point. Both sides talk in milliseconds since this, not
  // in wall clock: a wall clock can step backwards and a stall measured across
  // such a step reads as negative.
  QElapsedTimer clock;
};

State &state() {
  static State s;
  return s;
}

// Copies at most `capacity - 1` bytes and always terminates. Deliberately not
// QByteArray-based at the call site that matters: the exception filter runs
// with a possibly corrupt heap, so nothing here may allocate.
void copyFixed(char *destination, std::size_t capacity, const char *source,
               int sourceLength) {
  if (!destination || capacity == 0)
    return;
  std::size_t count = 0;
  if (source && sourceLength > 0) {
    count = static_cast<std::size_t>(sourceLength);
    if (count > capacity - 1)
      count = capacity - 1;
    std::memcpy(destination, source, count);
  }
  destination[count] = '\0';
}

void copyText(char *destination, std::size_t capacity, const QString &text) {
  const QByteArray latin1 = text.toLatin1();
  copyFixed(destination, capacity, latin1.constData(), latin1.size());
}

} // namespace

bool CrashChannel::valid() { return state().view != nullptr; }

ChannelData *CrashChannel::data() { return state().view; }

QString CrashChannel::mappingName() { return state().mappingName; }
QString CrashChannel::crashEventName() { return state().crashEventName; }
QString CrashChannel::dumpDoneEventName() {
  return state().dumpDoneEventName;
}

void CrashChannel::setEventNames(const QString &crashEvent,
                                 const QString &dumpDoneEvent) {
  State &s = state();
  if (!crashEvent.isEmpty())
    s.crashEventName = crashEvent;
  if (!dumpDoneEvent.isEmpty())
    s.dumpDoneEventName = dumpDoneEvent;
}

#ifdef Q_OS_WIN

bool CrashChannel::createHost() {
  State &s = state();
  if (s.view)
    return true;
  const DWORD pid = GetCurrentProcessId();
  s.mappingName = QStringLiteral("CutProChannel.%1").arg(pid);
  s.crashEventName = QStringLiteral("CutProCrash.%1").arg(pid);
  s.dumpDoneEventName = QStringLiteral("CutProDumpDone.%1").arg(pid);

  s.mapping = CreateFileMappingW(
      INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
      static_cast<DWORD>(sizeof(ChannelData)),
      reinterpret_cast<const wchar_t *>(s.mappingName.utf16()));
  if (!s.mapping)
    return false;
  void *view = MapViewOfFile(s.mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                             sizeof(ChannelData));
  if (!view) {
    CloseHandle(s.mapping);
    s.mapping = nullptr;
    return false;
  }
  std::memset(view, 0, sizeof(ChannelData));
  s.view = static_cast<ChannelData *>(view);
  s.clock.start();

  s.view->magic = kChannelMagic;
  s.view->version = kChannelVersion;
  s.view->pid = pid;
  s.view->guiThreadId = GetCurrentThreadId();
  s.view->startedAtMs = 0;
  copyFixed(s.view->appVersion, sizeof(s.view->appVersion), core::kVersion,
            static_cast<int>(std::strlen(core::kVersion)));

  // Manual reset on both: a crash is a one-way latch, and the reporter must
  // still see the signal if it was between waits when it was set.
  s.crashEvent = CreateEventW(
      nullptr, TRUE, FALSE,
      reinterpret_cast<const wchar_t *>(s.crashEventName.utf16()));
  s.dumpDoneEvent = CreateEventW(
      nullptr, TRUE, FALSE,
      reinterpret_cast<const wchar_t *>(s.dumpDoneEventName.utf16()));
  return true;
}

bool CrashChannel::openClient(const QString &mappingName) {
  State &s = state();
  if (s.view)
    return true;
  s.mappingName = mappingName;
  HANDLE mapping = OpenFileMappingW(
      FILE_MAP_READ, FALSE,
      reinterpret_cast<const wchar_t *>(mappingName.utf16()));
  if (!mapping)
    return false;
  void *view =
      MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(ChannelData));
  if (!view) {
    CloseHandle(mapping);
    return false;
  }
  ChannelData *block = static_cast<ChannelData *>(view);
  // A reporter left behind by an older build would otherwise read fields at
  // the wrong offsets and report confident nonsense.
  if (block->magic != kChannelMagic || block->version != kChannelVersion) {
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return false;
  }
  s.mapping = mapping;
  s.view = block;
  s.clock.start();
  return true;
}

void CrashChannel::installExceptionFilter() {
  static std::atomic_bool installed{false};
  if (installed.exchange(true))
    return;
  SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *pointers) -> LONG {
    State &s = state();
    if (s.view && pointers && pointers->ExceptionRecord) {
      s.view->exceptionCode = pointers->ExceptionRecord->ExceptionCode;
      s.view->exceptionAddress = reinterpret_cast<unsigned long long>(
          pointers->ExceptionRecord->ExceptionAddress);
      s.view->exceptionPointers =
          reinterpret_cast<unsigned long long>(pointers);
      s.view->exceptionThreadId = GetCurrentThreadId();
    }
    if (s.crashEvent) {
      SetEvent(s.crashEvent);
      // Bounded: if the reporter is not there, or is itself broken, the
      // process still has to die rather than hang in its own crash handler.
      if (s.dumpDoneEvent)
        WaitForSingleObject(s.dumpDoneEvent, 20000);
    }
    return EXCEPTION_EXECUTE_HANDLER;
  });
}

bool CrashChannel::waitForCrash(int timeoutMs) {
  State &s = state();
  if (!s.crashEvent) {
    if (s.crashEventName.isEmpty())
      return false;
    s.crashEvent = OpenEventW(
        SYNCHRONIZE, FALSE,
        reinterpret_cast<const wchar_t *>(s.crashEventName.utf16()));
    if (!s.crashEvent)
      return false;
  }
  return WaitForSingleObject(s.crashEvent, static_cast<DWORD>(timeoutMs)) ==
         WAIT_OBJECT_0;
}

void CrashChannel::signalDumpDone() {
  State &s = state();
  if (!s.dumpDoneEvent && !s.dumpDoneEventName.isEmpty()) {
    s.dumpDoneEvent = OpenEventW(
        EVENT_MODIFY_STATE, FALSE,
        reinterpret_cast<const wchar_t *>(s.dumpDoneEventName.utf16()));
  }
  if (s.dumpDoneEvent)
    SetEvent(s.dumpDoneEvent);
}

#else // !Q_OS_WIN

bool CrashChannel::createHost() { return false; }
bool CrashChannel::openClient(const QString &) { return false; }
void CrashChannel::installExceptionFilter() {}
bool CrashChannel::waitForCrash(int) { return false; }
void CrashChannel::signalDumpDone() {}

#endif

void CrashChannel::setScopeChain(const QString &text) {
  if (ChannelData *block = state().view)
    copyText(block->scopeChain, sizeof(block->scopeChain), text);
}

void CrashChannel::setVerdict(const QString &text) {
  if (ChannelData *block = state().view)
    copyText(block->verdict, sizeof(block->verdict), text);
}

void CrashChannel::setCensus(const QString &text, int items, bool truncated) {
  ChannelData *block = state().view;
  if (!block)
    return;
  copyText(block->census, sizeof(block->census), text);
  block->censusItems = static_cast<unsigned int>(items < 0 ? 0 : items);
  block->censusTruncated = truncated ? 1u : 0u;
  block->censusAtMs = state().clock.isValid() ? state().clock.elapsed() : 0;
}

void CrashChannel::noteHeartbeat(unsigned long long beat, long long atMs) {
  if (ChannelData *block = state().view) {
    block->heartbeatAtMs = atMs;
    // Written last: the reporter reads the counter to decide whether the beat
    // is new, so the timestamp has to already be there when it changes.
    block->heartbeat = beat;
  }
}

void CrashChannel::noteStall(unsigned int reports, unsigned int severe,
                             long long worstMs) {
  if (ChannelData *block = state().view) {
    block->stallReports = reports;
    block->severeStalls = severe;
    block->worstStallMs = worstMs;
  }
}

void CrashChannel::markWindowShown() {
  if (ChannelData *block = state().view)
    block->windowShown = 1u;
}

void CrashChannel::markCleanExit() {
  if (ChannelData *block = state().view)
    block->cleanExit = 1u;
}

QStringList CrashChannel::describe() {
  const ChannelData *block = state().view;
  if (!block)
    return {QStringLiteral("crash channel: unavailable")};
  QStringList lines;
  lines << QStringLiteral("pid            %1").arg(block->pid);
  lines << QStringLiteral("appVersion     %1")
               .arg(QString::fromLatin1(block->appVersion));
  lines << QStringLiteral("guiThreadId    %1").arg(block->guiThreadId);
  lines << QStringLiteral("windowShown    %1").arg(block->windowShown);
  lines << QStringLiteral("heartbeat      %1 at %2 ms")
               .arg(block->heartbeat)
               .arg(block->heartbeatAtMs);
  lines << QStringLiteral("stalls         %1 (%2 severe), worst %3 ms")
               .arg(block->stallReports)
               .arg(block->severeStalls)
               .arg(block->worstStallMs);
  if (block->scopeChain[0])
    lines << QStringLiteral("guiScopeChain  %1")
                 .arg(QString::fromLatin1(block->scopeChain));
  if (block->verdict[0])
    lines << QStringLiteral("verdict        %1")
                 .arg(QString::fromLatin1(block->verdict));
  if (block->exceptionCode)
    lines << QStringLiteral("exception      0x%1 at 0x%2 on thread %3")
                 .arg(block->exceptionCode, 8, 16, QLatin1Char('0'))
                 .arg(block->exceptionAddress, 16, 16, QLatin1Char('0'))
                 .arg(block->exceptionThreadId);
  return lines;
}

} // namespace diag
