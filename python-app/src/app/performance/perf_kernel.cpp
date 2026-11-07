#include "app/performance/perf_kernel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QRunnable>
#include <QSemaphore>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <limits>

namespace cutpro::perf {
namespace {

// Index of the first span starting at or after `timeMs`. Everything that could
// overlap a query ending there lies strictly below it.
int lowerBoundByStart(const QVector<Span> &spans, qint64 timeMs) {
  int low = 0;
  int high = spans.size();
  while (low < high) {
    const int mid = low + (high - low) / 2;
    if (spans.at(mid).startMs < timeMs)
      low = mid + 1;
    else
      high = mid;
  }
  return low;
}

} // namespace

void IntervalIndex::build(const QVariantList &items,
                          const std::function<bool(const QVariantMap &)> &accept,
                          const QString &startKey, const QString &lengthKey) {
  QVector<Span> spans;
  spans.reserve(items.size());
  for (int i = 0; i < items.size(); ++i) {
    const QVariantMap item = items.at(i).toMap();
    if (accept && !accept(item))
      continue;
    const qint64 start = item.value(startKey).toLongLong();
    const qint64 length = item.value(lengthKey).toLongLong();
    // A zero or negative length covers no time, so no query could return it and
    // keeping it would only widen the backwards walk every query does.
    if (length <= 0)
      continue;
    spans.append(Span{start, start + length, i});
  }
  buildFromSpans(std::move(spans));
  m_pin = items;
}

void IntervalIndex::buildFromSpans(QVector<Span> spans) {
  // Spans handed over directly have no list behind them, so there is nothing for
  // syncTo() to compare against and it must not think there is.
  m_pin.clear();
  m_spans = std::move(spans);
  std::sort(m_spans.begin(), m_spans.end(),
            [](const Span &left, const Span &right) {
              if (left.startMs != right.startMs)
                return left.startMs < right.startMs;
              return left.endMs < right.endMs;
            });
  finishBuild();
}

void IntervalIndex::finishBuild() {
  m_maxEnd.resize(m_spans.size());
  m_edges.clear();
  m_edges.reserve(m_spans.size() * 2);
  qint64 runningMax = std::numeric_limits<qint64>::min();
  for (int i = 0; i < m_spans.size(); ++i) {
    const Span &span = m_spans.at(i);
    runningMax = std::max(runningMax, span.endMs);
    m_maxEnd[i] = runningMax;
    m_edges.append(span.startMs);
    m_edges.append(span.endMs);
  }
  m_totalEndMs = m_spans.isEmpty() ? 0 : runningMax;
  std::sort(m_edges.begin(), m_edges.end());
  m_edges.erase(std::unique(m_edges.begin(), m_edges.end()), m_edges.end());
  m_built = true;
}

bool IntervalIndex::syncTo(
    const QVariantList &items,
    const std::function<bool(const QVariantMap &)> &accept,
    const QString &startKey, const QString &lengthKey) {
  if (m_built && m_pin.size() == items.size() &&
      m_pin.constData() == items.constData())
    return false;
  build(items, accept, startKey, lengthKey);
  return true;
}

void IntervalIndex::clear() {
  m_spans.clear();
  m_maxEnd.clear();
  m_edges.clear();
  m_totalEndMs = 0;
  m_pin.clear();
  m_built = false;
}

QVector<int> IntervalIndex::overlapping(qint64 fromMs, qint64 toMs) const {
  QVector<int> hits;
  if (m_spans.isEmpty() || toMs <= fromMs)
    return hits;
  for (int i = lowerBoundByStart(m_spans, toMs) - 1; i >= 0; --i) {
    // Nothing in [0..i] reaches fromMs, so nothing earlier can either. This is
    // the line that turns a full scan into a couple of steps.
    if (m_maxEnd.at(i) <= fromMs)
      break;
    const Span &span = m_spans.at(i);
    if (span.endMs > fromMs)
      hits.append(span.index);
  }
  // The walk went backwards; callers want source order.
  std::reverse(hits.begin(), hits.end());
  return hits;
}

bool IntervalIndex::anyOverlap(qint64 fromMs, qint64 toMs) const {
  if (m_spans.isEmpty() || toMs <= fromMs)
    return false;
  for (int i = lowerBoundByStart(m_spans, toMs) - 1; i >= 0; --i) {
    if (m_maxEnd.at(i) <= fromMs)
      return false;
    if (m_spans.at(i).endMs > fromMs)
      return true;
  }
  return false;
}

int IntervalIndex::firstAt(qint64 timeMs) const {
  if (m_spans.isEmpty())
    return -1;
  int best = -1;
  qint64 bestStart = 0;
  for (int i = lowerBoundByStart(m_spans, timeMs + 1) - 1; i >= 0; --i) {
    if (m_maxEnd.at(i) <= timeMs)
      break;
    const Span &span = m_spans.at(i);
    if (span.endMs > timeMs && (best < 0 || span.startMs < bestStart)) {
      best = span.index;
      bestStart = span.startMs;
    }
  }
  return best;
}

qint64 IntervalIndex::nextEdgeAfter(qint64 timeMs, qint64 fallback) const {
  const auto it = std::upper_bound(m_edges.cbegin(), m_edges.cend(), timeMs);
  return it == m_edges.cend() ? fallback : *it;
}

qint64 IntervalIndex::previousEdgeBefore(qint64 timeMs, qint64 fallback) const {
  const auto it = std::lower_bound(m_edges.cbegin(), m_edges.cend(), timeMs);
  return it == m_edges.cbegin() ? fallback : *(it - 1);
}

namespace {

class ChunkRunnable : public QRunnable {
public:
  ChunkRunnable(const std::function<void(int, int)> *chunk, int first, int last,
                QSemaphore *done)
      : m_chunk(chunk), m_first(first), m_last(last), m_done(done) {
    setAutoDelete(true);
  }

  void run() override {
    // An exception escaping run() unwinds a pool thread, and the release below
    // would never happen - so the caller's acquire() would wait for the life of
    // the process. A chunk that throws is a bug, but it must not be that bug.
    try {
      (*m_chunk)(m_first, m_last);
    } catch (const std::exception &error) {
      qWarning("cutpro::perf::parallelFor: chunk [%d,%d) threw: %s", m_first,
               m_last, error.what());
    } catch (...) {
      qWarning("cutpro::perf::parallelFor: chunk [%d,%d) threw", m_first,
               m_last);
    }
    m_done->release();
  }

private:
  const std::function<void(int, int)> *m_chunk;
  int m_first;
  int m_last;
  QSemaphore *m_done;
};

} // namespace

void parallelFor(int begin, int end,
                 const std::function<void(int, int)> &chunk, int minChunk,
                 int maxThreads) {
  parallelForCancellable(begin, end, chunk, {}, minChunk, maxThreads);
}

bool parallelForCancellable(int begin, int end,
                            const std::function<void(int, int)> &chunk,
                            const std::function<bool()> &cancelled,
                            int minChunk, int maxThreads) {
  if (end <= begin)
    return true;
  if (!chunk)
    return false;

  const int total = end - begin;
  minChunk = std::max(1, minChunk);
  QThreadPool *pool = QThreadPool::globalInstance();
  int threads = maxThreads > 0 ? maxThreads : pool->maxThreadCount();
  threads = std::max(1, std::min(threads, total / minChunk));
  if (threads <= 1) {
    if (cancelled && cancelled())
      return false;
    chunk(begin, end);
    return true;
  }

  const int chunkSize = (total + threads - 1) / threads;
  QSemaphore done;
  int dispatched = 0;
  int first = begin;
  bool stopped = false;
  // Every chunk but the last goes to the pool; the last runs here. This thread is
  // about to block anyway, so using it is free, and it guarantees forward
  // progress even if the pool is saturated by something else.
  while (first < end) {
    const int last = std::min(end, first + chunkSize);
    if (cancelled && cancelled()) {
      stopped = true;
      break;
    }
    if (last >= end) {
      chunk(first, last);
      break;
    }
    pool->start(new ChunkRunnable(&chunk, first, last, &done));
    ++dispatched;
    first = last;
  }
  // Nothing below this line may touch `chunk`: the runnables hold a pointer to
  // it, and the caller owns it only until we return.
  if (dispatched > 0)
    done.acquire(dispatched);
  return !stopped;
}

struct BudgetedRun::State {
  std::function<void(int)> step;
  std::function<void(int, int)> progress;
  std::function<void(bool)> finished;
  int total = 0;
  int done = 0;
  int budgetMs = 6;
  bool cancelled = false;
  bool running = true;
};

void BudgetedRun::cancel() const {
  if (m_state)
    m_state->cancelled = true;
}

bool BudgetedRun::isRunning() const { return m_state && m_state->running; }

int BudgetedRun::done() const { return m_state ? m_state->done : 0; }

int BudgetedRun::total() const { return m_state ? m_state->total : 0; }

BudgetedRun runBudgeted(QObject *owner, int total,
                        const std::function<void(int)> &step,
                        const std::function<void(int, int)> &progress,
                        const std::function<void(bool)> &finished,
                        int budgetMs) {
  BudgetedRun handle;
  handle.m_state = std::make_shared<BudgetedRun::State>();
  const std::shared_ptr<BudgetedRun::State> state = handle.m_state;
  state->step = step;
  state->progress = progress;
  state->finished = finished;
  state->total = std::max(0, total);
  state->budgetMs = std::max(1, budgetMs);

  QObject *context = owner ? owner : QCoreApplication::instance();
  if (!step || state->total == 0 || !context) {
    state->running = false;
    // An empty range completed; a missing step callable did not. Either way it is
    // reported through the event loop, so a caller never has to handle finished()
    // arriving before runBudgeted() has returned.
    const bool trivialCompleted = static_cast<bool>(step);
    if (finished && context)
      QTimer::singleShot(0, context, [finished, trivialCompleted]() {
        finished(trivialCompleted);
      });
    return handle;
  }

  QTimer *timer = new QTimer(context);
  timer->setInterval(0);
  QObject::connect(timer, &QTimer::timeout, timer, [timer, state]() {
    FrameBudget budget(state->budgetMs);
    while (state->done < state->total && !state->cancelled) {
      state->step(state->done);
      ++state->done;
      // Every eighth step, not every step: a step costing a few hundred
      // nanoseconds would otherwise spend a quarter of its time reading a clock,
      // and a step slow enough for eight of them to matter is over budget by
      // itself anyway.
      if ((state->done & 7) == 0 && budget.expired())
        break;
    }
    if (state->progress)
      state->progress(state->done, state->total);
    if (state->done < state->total && !state->cancelled)
      return;
    state->running = false;
    timer->stop();
    const bool completed = !state->cancelled && state->done >= state->total;
    if (state->finished)
      state->finished(completed);
    timer->deleteLater();
  });
  timer->start();
  return handle;
}

namespace {

struct LedgerEntry {
  qint64 calls = 0;
  qint64 totalNs = 0;
  qint64 worstNs = 0;
};

std::atomic<bool> g_ledgerEnabled{false};

QMutex &ledgerMutex() {
  static QMutex mutex;
  return mutex;
}

QHash<QString, LedgerEntry> &ledgerEntries() {
  static QHash<QString, LedgerEntry> entries;
  return entries;
}

QString formatMs(double milliseconds) {
  return QString::number(milliseconds, 'f', milliseconds >= 100.0 ? 0 : 2);
}

} // namespace

void Ledger::setEnabled(bool enabled) {
  g_ledgerEnabled.store(enabled, std::memory_order_relaxed);
}

bool Ledger::isEnabled() {
  return g_ledgerEnabled.load(std::memory_order_relaxed);
}

void Ledger::record(const char *label, qint64 nanoseconds) {
  if (!label || !isEnabled())
    return;
  const QMutexLocker locker(&ledgerMutex());
  LedgerEntry &entry = ledgerEntries()[QString::fromLatin1(label)];
  ++entry.calls;
  entry.totalNs += nanoseconds;
  entry.worstNs = std::max(entry.worstNs, nanoseconds);
}

void Ledger::reset() {
  const QMutexLocker locker(&ledgerMutex());
  ledgerEntries().clear();
}

QString Ledger::report() {
  QVector<std::pair<QString, LedgerEntry>> rows;
  {
    const QMutexLocker locker(&ledgerMutex());
    rows.reserve(ledgerEntries().size());
    for (auto it = ledgerEntries().cbegin(); it != ledgerEntries().cend(); ++it)
      rows.append({it.key(), it.value()});
  }
  if (rows.isEmpty())
    return QStringLiteral("perf ledger: nothing recorded (Ledger::setEnabled)");

  // Worst total first: the label that owns the most wall clock is the only one
  // worth reading past.
  std::sort(rows.begin(), rows.end(),
            [](const auto &left, const auto &right) {
              return left.second.totalNs > right.second.totalNs;
            });

  QStringList lines;
  lines.append(QStringLiteral("%1  %2  %3  %4  %5")
                   .arg(QStringLiteral("label"), -44)
                   .arg(QStringLiteral("calls"), 9)
                   .arg(QStringLiteral("total ms"), 10)
                   .arg(QStringLiteral("mean us"), 10)
                   .arg(QStringLiteral("worst ms"), 10));
  for (const auto &row : rows) {
    const LedgerEntry &entry = row.second;
    const double meanUs =
        entry.calls > 0 ? double(entry.totalNs) / double(entry.calls) / 1000.0
                        : 0.0;
    lines.append(QStringLiteral("%1  %2  %3  %4  %5")
                     .arg(row.first, -44)
                     .arg(entry.calls, 9)
                     .arg(formatMs(double(entry.totalNs) / 1e6), 10)
                     .arg(QString::number(meanUs, 'f', 1), 10)
                     .arg(formatMs(double(entry.worstNs) / 1e6), 10));
  }
  return lines.join(QLatin1Char('\n'));
}

void Ledger::dump() { qInfo().noquote() << '\n' << report(); }

} // namespace cutpro::perf
