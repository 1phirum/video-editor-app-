#include "app/preview/gui_stall_report.h"

#include <QStringList>

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

qint64 monotonicMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

// Written only by the GUI thread, read by the monitor thread. Every label is a
// string literal or an interned copy, so the monitor can safely dereference a
// pointer the GUI thread has already moved past.
std::atomic<const char *> g_stack[GuiScopeStack::kMaxDepth];
// Paired with the labels rather than kept in a second structure, so a frame and
// the moment it was entered can never disagree.
std::atomic<qint64> g_enteredMs[GuiScopeStack::kMaxDepth];
std::atomic<int> g_depth{0};

// Set by markTurn for QML work. Sits below the C++ frames in the chain: the QML
// handler is the outer one.
std::atomic<const char *> g_turnLabel{nullptr};
std::atomic<qint64> g_turnEnteredMs{0};
// Identifies the mark that owns the current label, so a queued clear posted by
// an earlier mark cannot wipe a later one's.
std::atomic<quint64> g_turnGeneration{0};

std::atomic<quint64> g_overflows{0};

// Interned QML labels. std::unordered_set is node-based, so an element's address
// survives every later insertion - which is what lets the monitor thread hold a
// raw pointer into it. Touched only from the GUI thread, and it grows to the
// number of distinct call sites in the QML, which is a handful.
std::unordered_set<std::string> &labelPool() {
  static std::unordered_set<std::string> pool;
  return pool;
}

} // namespace

int GuiScopeStack::push(const char *label) {
  const int depthOnEntry = g_depth.load(std::memory_order_relaxed);
  // Past the ceiling the label is dropped but the depth still counts, so the
  // matching pop restores the right level and the chain simply shows the first
  // kMaxDepth frames.
  if (depthOnEntry >= 0 && depthOnEntry < kMaxDepth) {
    g_stack[depthOnEntry].store(label, std::memory_order_relaxed);
    g_enteredMs[depthOnEntry].store(monotonicMs(), std::memory_order_release);
  } else {
    g_overflows.fetch_add(1, std::memory_order_relaxed);
  }
  g_depth.store(depthOnEntry + 1, std::memory_order_release);
  return depthOnEntry;
}

void GuiScopeStack::pop(int depthOnEntry) {
  g_depth.store(depthOnEntry, std::memory_order_release);
}

quint64 GuiScopeStack::turnGeneration() {
  return g_turnGeneration.load(std::memory_order_relaxed);
}

bool GuiScopeStack::markTurn(const QString &label) {
  // A C++ frame is the more specific attribution, and it will be gone before a
  // queued clear could run, so taking the slot now would leave a label that
  // outlives the turn it describes.
  if (g_depth.load(std::memory_order_acquire) > 0)
    return false;
  const char *interned = labelPool().insert(label.toStdString()).first->c_str();
  g_turnGeneration.fetch_add(1, std::memory_order_relaxed);
  g_turnEnteredMs.store(monotonicMs(), std::memory_order_release);
  g_turnLabel.store(interned, std::memory_order_release);
  return true;
}

void GuiScopeStack::clearTurn(quint64 generation) {
  if (g_turnGeneration.load(std::memory_order_relaxed) != generation)
    return;
  g_turnLabel.store(nullptr, std::memory_order_release);
}

QString GuiScopeStack::Snapshot::verdict(qint64 ageMs) const {
  if (depth <= 0)
    return QStringLiteral("stalled somewhere unmarked");
  if (repeats > 1)
    return QStringLiteral("re-entering itself %1 levels deep in").arg(repeats);
  if (wedged(ageMs))
    return QStringLiteral("wedged in");
  return QStringLiteral("churning through");
}

GuiScopeStack::Snapshot GuiScopeStack::snapshot(qint64 stallBeganMs) {
  const qint64 now = monotonicMs();
  Snapshot out;
  QStringList parts;

  // A turn label older than the stall is a leftover, not a description. Its
  // clear is a queued event, so the event loop completing even one turn since
  // then - which the heartbeat at stallBeganMs proves it did - would have run
  // it. Printing it anyway is what made a freeze carry the name of whatever ran
  // before it.
  if (const char *turn = g_turnLabel.load(std::memory_order_acquire)) {
    const qint64 entered = g_turnEnteredMs.load(std::memory_order_acquire);
    if (entered >= stallBeganMs) {
      parts.append(QString::fromLatin1(turn));
      out.blocking = turn;
      out.innermostOpenMs = now - entered;
      ++out.depth;
    }
  }

  const int frames =
      qBound(0, g_depth.load(std::memory_order_acquire), kMaxDepth);
  std::unordered_map<std::string, int> seen;
  for (int i = 0; i < frames; ++i) {
    const char *label = g_stack[i].load(std::memory_order_relaxed);
    if (!label)
      continue; // unwinding while this reads; a gap beats a lock
    const qint64 entered = g_enteredMs[i].load(std::memory_order_acquire);
    parts.append(QString::fromLatin1(label));
    if (entered > stallBeganMs)
      ++out.enteredDuringStall;
    // Innermost wins: the deepest frame still open is the one the thread is
    // actually in, whether or not it was entered before the thread went quiet.
    out.blocking = label;
    out.innermostOpenMs = now - entered;
    ++out.depth;
    const int count = ++seen[label];
    if (count > out.repeats) {
      out.repeats = count;
      out.repeated = label;
    }
  }

  out.chain = parts.isEmpty() ? QStringLiteral("no marked scope")
                              : parts.join(QStringLiteral(" > "));
  return out;
}

QVariantMap GuiScopeStack::statistics() {
  QVariantMap stats;
  stats[QStringLiteral("guiScopeLabelsInterned")] =
      static_cast<qulonglong>(labelPool().size());
  stats[QStringLiteral("guiScopeOverflows")] =
      static_cast<qulonglong>(g_overflows.load(std::memory_order_relaxed));
  stats[QStringLiteral("guiScopeDepthNow")] =
      g_depth.load(std::memory_order_relaxed);
  return stats;
}
