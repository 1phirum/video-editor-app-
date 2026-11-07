#pragma once

// Performance primitives for the parts of the app that have to survive a
// twenty-thousand-clip sequence: a ten hour source transcribed into 19 831
// subtitle cues, each of which becomes a clip, a spoken voice, and a row in
// three separate models.
//
// Nothing here knows what a clip is. Everything takes plain spans, index ranges
// or a QVariantList plus the two field names to read off it, so a subsystem can
// adopt one piece without inheriting anything and without a link edge to
// Backend.
//
// The four costs this module exists to remove, in the order they usually show up
// in a profile of a large sequence:
//
//  1. "which clip is under the playhead", answered by walking the list. At
//     19 831 cues and 60 redraws a second that is over a million QVariantMap
//     lookups per second of playback. IntervalIndex answers it in about fifteen
//     comparisons and no allocation.
//  2. per-item work on the GUI thread with no yield, so one edit becomes a
//     multi-second freeze. runBudgeted() spends a fixed slice of each event loop
//     turn and continues on the next one, which keeps the window painting and
//     keeps Cancel clickable.
//  3. per-item work that is pure CPU and independent - shaping cues, measuring
//     text, hashing - run one at a time on one core. parallelFor() spreads it
//     over the pool in contiguous ranges, which needs no locking.
//  4. derived data recomputed without bound (thumbnails, waveform tiles, glyph
//     layouts), so a long sequence turns into an out-of-memory rather than
//     merely a slow one. LruCache bounds it by approximate byte cost.
//
// Plus Ledger, which is how you find out which of the four you actually have.

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>

#include <functional>
#include <memory>
#include <utility>

class QObject;

namespace cutpro::perf {

// A half-open span [startMs, endMs) and where it came from.
struct Span {
  qint64 startMs = 0;
  qint64 endMs = 0;
  // Position in the list the index was built from, so a hit can be turned back
  // into the caller's own object without a second lookup.
  int index = -1;

  bool contains(qint64 timeMs) const {
    return timeMs >= startMs && timeMs < endMs;
  }
  qint64 lengthMs() const { return endMs - startMs; }
};

// An implicit interval tree over a flat array: spans sorted by start, plus a
// running maximum of end. Every query binary-searches for the last span that can
// start before the query ends, then walks backwards and stops the moment that
// running maximum proves nothing earlier can reach the query. For subtitle cues -
// short, in order, barely overlapping - the walk stops after a step or two, which
// is what makes this O(log n) in practice and not only in theory.
//
// Build cost is one sort. syncTo() is there so no caller has to remember to
// rebuild: it compares the list's buffer address and length against the one the
// index was built from, and a QVariantList mutated anywhere has necessarily
// detached, so the addresses stop matching. That is the same trick
// Backend::ensureClipCaches() uses, for the same reason - none of the forty-odd
// places that mutate the clip list has to know this index exists.
//
// One index per (list, predicate) pair. syncTo() cannot see that a predicate
// changed, only that the list did.
class IntervalIndex {
public:
  // Reads startMs and durationMs off each map. `accept` is asked about every
  // entry and may be empty, meaning take all of them; pass a predicate to build
  // a per-track or per-kind index. Entries with no positive length are skipped -
  // they cover no time, so no query could ever return them.
  void build(const QVariantList &items,
             const std::function<bool(const QVariantMap &)> &accept = {},
             const QString &startKey = QStringLiteral("startMs"),
             const QString &lengthKey = QStringLiteral("durationMs"));

  // Same index, from spans already in hand. Sorting happens here, so the vector
  // may arrive in any order.
  void buildFromSpans(QVector<Span> spans);

  // Rebuilds only when `items` is not the exact list this index was built from.
  // Returns true when a rebuild actually happened, which is also the cheapest
  // "did the clip list move" test available to a caller.
  bool syncTo(const QVariantList &items,
              const std::function<bool(const QVariantMap &)> &accept = {},
              const QString &startKey = QStringLiteral("startMs"),
              const QString &lengthKey = QStringLiteral("durationMs"));

  void clear();
  bool isEmpty() const { return m_spans.isEmpty(); }
  int size() const { return m_spans.size(); }
  // Largest end over every span, which for a clip list is the sequence length.
  qint64 totalEndMs() const { return m_totalEndMs; }

  // Indices into the list this was built from, ascending by start.
  QVector<int> overlapping(qint64 fromMs, qint64 toMs) const;
  QVector<int> at(qint64 timeMs) const {
    return overlapping(timeMs, timeMs + 1);
  }
  // The earliest-starting span covering timeMs, or -1. This is the per-frame
  // "which caption is showing" question, and it allocates nothing at all.
  int firstAt(qint64 timeMs) const;
  // Stops at the first hit, so a placement or collision test does not pay for
  // the whole answer.
  bool anyOverlap(qint64 fromMs, qint64 toMs) const;

  // Snapping, and skip-to-next-edit navigation: the nearest span boundary
  // strictly after / before timeMs, or `fallback` when there is none.
  qint64 nextEdgeAfter(qint64 timeMs, qint64 fallback = -1) const;
  qint64 previousEdgeBefore(qint64 timeMs, qint64 fallback = -1) const;

  const QVector<Span> &spans() const { return m_spans; }

private:
  void finishBuild();

  QVector<Span> m_spans;    // sorted by startMs, then endMs
  QVector<qint64> m_maxEnd; // m_maxEnd[i] = largest endMs over m_spans[0..i]
  QVector<qint64> m_edges;  // every distinct boundary, ascending, deduplicated
  qint64 m_totalEndMs = 0;
  // Holds the buffer the index was built from alive, so the address comparison
  // in syncTo() cannot be fooled by a different list landing on a freed
  // allocation. Shares storage with the caller's list; it is not a deep copy.
  QVariantList m_pin;
  bool m_built = false;
};

// Splits [begin, end) into contiguous chunks and runs chunk(first, last) on the
// global thread pool, keeping the last chunk for the calling thread. Returns only
// once every chunk has finished, so anything the callable captured by reference
// is still alive throughout.
//
// `chunk` runs on pool threads and must touch no GUI object and no shared
// container. The intended shape is a pre-sized output container indexed by the
// loop variable: distinct indices need no lock, which is the whole reason this
// hands out contiguous ranges instead of single items.
//
// Ranges shorter than minChunk items run inline. A thousand trips through a
// thread pool to do a microsecond of work each is slower than the loop it
// replaced, and the crossover is higher than most people guess.
//
// Do not call this from inside a pool task: the calling thread blocks until its
// chunks are picked up, and a pool with no free thread would have to finish the
// caller first.
void parallelFor(int begin, int end,
                 const std::function<void(int first, int last)> &chunk,
                 int minChunk = 256, int maxThreads = 0);

// As above, but stops handing out new chunks once `cancelled` answers true.
// Chunks already in flight run to completion, because there is nothing safe to
// interrupt them with. Returns true when the whole range ran.
bool parallelForCancellable(int begin, int end,
                            const std::function<void(int first, int last)> &chunk,
                            const std::function<bool()> &cancelled,
                            int minChunk = 256, int maxThreads = 0);

// A deadline. Ask expired() in a loop and stop when it answers true.
class FrameBudget {
public:
  explicit FrameBudget(int budgetMs = 6) : m_budgetMs(budgetMs) {
    m_timer.start();
  }
  bool expired() const { return m_timer.elapsed() >= m_budgetMs; }
  qint64 elapsedMs() const { return m_timer.elapsed(); }
  void restart() { m_timer.restart(); }

private:
  QElapsedTimer m_timer;
  int m_budgetMs;
};

// Handle on a run started by runBudgeted(). Safe to keep after the run ends;
// cancel() on a finished run does nothing.
class BudgetedRun {
public:
  BudgetedRun() = default;
  void cancel() const;
  bool isRunning() const;
  int done() const;
  int total() const;

  struct State;

private:
  friend BudgetedRun runBudgeted(QObject *, int,
                                 const std::function<void(int)> &,
                                 const std::function<void(int, int)> &,
                                 const std::function<void(bool)> &, int);
  std::shared_ptr<State> m_state;
};

// Runs step(i) for i in [0, total) across as many event loop turns as it takes,
// spending at most budgetMs on each. The window keeps painting, the progress bar
// keeps moving, and Cancel can actually be clicked - none of which is true of the
// same loop written inline, which is why nineteen thousand cues currently arrive
// as a freeze.
//
// `owner` scopes the run: destroy it and the run stops without calling finished.
// `progress` is invoked once per turn with the running count, on the GUI thread,
// which makes it safe to write straight into a property. `finished` is invoked
// once, with false when the run was cancelled.
//
// The clock is read every eighth step rather than every step, so a step costing
// a few hundred nanoseconds does not spend a quarter of its time asking the time.
BudgetedRun runBudgeted(QObject *owner, int total,
                        const std::function<void(int index)> &step,
                        const std::function<void(int done, int total)> &progress = {},
                        const std::function<void(bool completed)> &finished = {},
                        int budgetMs = 6);

// Bounded by approximate byte cost rather than by entry count, because the things
// worth caching in a video editor differ in size by four orders of magnitude: a
// glyph layout is a few hundred bytes, a 4K thumbnail is thirty-three megabytes.
// A count-bounded cache holding either one is wrong about one of them.
//
// Not thread safe on purpose. Give each thread its own, or guard it from outside -
// a mutex inside a cache this small usually costs more than the lookup it
// protects, and the caller often already holds one.
template <typename Key, typename Value> class LruCache {
public:
  explicit LruCache(qint64 maxBytes = 64ll * 1024 * 1024)
      : m_maxBytes(maxBytes > 0 ? maxBytes : 1) {}
  ~LruCache() { clear(); }
  LruCache(const LruCache &) = delete;
  LruCache &operator=(const LruCache &) = delete;

  // Inserting an existing key replaces its value and its cost.
  void insert(const Key &key, const Value &value, qint64 costBytes) {
    costBytes = costBytes > 0 ? costBytes : 1;
    remove(key);
    // One entry larger than the whole budget would evict everything and then
    // itself. Refusing it keeps the cache useful instead of empty.
    if (costBytes > m_maxBytes)
      return;
    Node *node = new Node{key, value, costBytes, nullptr, nullptr};
    m_lookup.insert(key, node);
    m_totalBytes += costBytes;
    linkNewest(node);
    trim();
  }

  // Found entries become the most recently used. Returns false and leaves `out`
  // alone on a miss.
  bool take(const Key &key, Value *out) {
    const auto it = m_lookup.constFind(key);
    if (it == m_lookup.constEnd()) {
      ++m_misses;
      return false;
    }
    Node *node = *it;
    unlink(node);
    linkNewest(node);
    ++m_hits;
    if (out)
      *out = node->value;
    return true;
  }

  bool contains(const Key &key) const { return m_lookup.contains(key); }

  void remove(const Key &key) {
    const auto it = m_lookup.constFind(key);
    if (it == m_lookup.constEnd())
      return;
    Node *node = *it;
    m_lookup.erase(it);
    unlink(node);
    m_totalBytes -= node->cost;
    delete node;
  }

  void clear() {
    for (Node *node : m_lookup)
      delete node;
    m_lookup.clear();
    m_newest = nullptr;
    m_oldest = nullptr;
    m_totalBytes = 0;
  }

  // Shrinking the budget evicts immediately rather than waiting for the next
  // insert, which is what a caller reacting to memory pressure wants.
  void setMaxBytes(qint64 maxBytes) {
    m_maxBytes = maxBytes > 0 ? maxBytes : 1;
    trim();
  }
  qint64 maxBytes() const { return m_maxBytes; }
  qint64 totalBytes() const { return m_totalBytes; }
  int count() const { return m_lookup.size(); }
  qint64 hits() const { return m_hits; }
  qint64 misses() const { return m_misses; }
  // A cache under about 0.8 is usually either too small or keyed on something
  // that varies more than the caller thought.
  double hitRate() const {
    const qint64 total = m_hits + m_misses;
    return total > 0 ? double(m_hits) / double(total) : 0.0;
  }

private:
  struct Node {
    Key key;
    Value value;
    qint64 cost;
    Node *older;
    Node *newer;
  };

  void linkNewest(Node *node) {
    node->newer = nullptr;
    node->older = m_newest;
    if (m_newest)
      m_newest->newer = node;
    m_newest = node;
    if (!m_oldest)
      m_oldest = node;
  }

  void unlink(Node *node) {
    if (node->older)
      node->older->newer = node->newer;
    else
      m_oldest = node->newer;
    if (node->newer)
      node->newer->older = node->older;
    else
      m_newest = node->older;
    node->older = nullptr;
    node->newer = nullptr;
  }

  void trim() {
    while (m_totalBytes > m_maxBytes && m_oldest)
      remove(m_oldest->key);
  }

  QHash<Key, Node *> m_lookup;
  Node *m_newest = nullptr;
  Node *m_oldest = nullptr;
  qint64 m_maxBytes;
  qint64 m_totalBytes = 0;
  qint64 m_hits = 0;
  qint64 m_misses = 0;
};

// An aggregating timer: calls, total, mean and worst per label, across threads,
// for the life of the process.
//
// This asks a different question from the stall tracer in
// app/preview/gui_stall_tracer.h. That one reports the stack of a freeze already
// in progress, which is the right tool when one call takes a second. This one
// answers "the nine hundred milliseconds went where", which needs the sum over
// thousands of individually innocent calls - the shape almost every large-sequence
// problem in this app has turned out to have.
//
// Disabled by default. Recording costs a mutex and a hash insert, which is
// nothing beside the work being measured and is not nothing inside a tight loop,
// so it is opt-in and meant to be turned off again.
class Ledger {
public:
  static void setEnabled(bool enabled);
  static bool isEnabled();
  static void record(const char *label, qint64 nanoseconds);
  static void reset();
  // One line per label, worst total first: calls, total ms, mean us, worst ms.
  static QString report();
  // The same table through qInfo(), so it is reachable from a debugger, a
  // shortcut, or the end of a slow operation.
  static void dump();
};

// Times a scope into the Ledger. When the Ledger is off the timer is never
// started, and an unstarted QElapsedTimer is a couple of untouched integers.
class Sample {
public:
  explicit Sample(const char *label) : m_label(label) {
    if (Ledger::isEnabled())
      m_timer.start();
  }
  ~Sample() {
    if (m_timer.isValid())
      Ledger::record(m_label, m_timer.nsecsElapsed());
  }
  Sample(const Sample &) = delete;
  Sample &operator=(const Sample &) = delete;

private:
  const char *m_label;
  QElapsedTimer m_timer;
};

} // namespace cutpro::perf

#define CUTPRO_PERF_JOIN_INNER(a, b) a##b
#define CUTPRO_PERF_JOIN(a, b) CUTPRO_PERF_JOIN_INNER(a, b)
// Label must be a string literal, so the pointer outlives the sample.
#define CUTPRO_PERF_SAMPLE(label)                                              \
  ::cutpro::perf::Sample CUTPRO_PERF_JOIN(cutproPerfSample_, __LINE__)(label)

