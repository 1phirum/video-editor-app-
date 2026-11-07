#include "app/guard/tamper.h"

#include <QRandomGenerator>

#include <atomic>
#include <cstdlib>

namespace guard {

namespace {
// One word of state for the whole process. Non-zero once anything trips.
// Seeded from the reason so poisonSeed() is stable within a run but differs
// between the kinds of tamper, which spreads the resulting corruption around.
std::atomic<quint64> g_poison{0};

quint64 spread(quint32 reason) {
  // A cheap avalanche (SplitMix64 finaliser) so a small reason code becomes a
  // full-width value with no obvious relationship to it in memory.
  quint64 x = 0x9E3779B97F4A7C15ULL ^ (static_cast<quint64>(reason) << 1);
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return (x ^ (x >> 31)) | 1ULL; // never zero
}
} // namespace

void trip(TamperReason reason) {
  quint64 expected = 0;
  // First writer wins; later trips do not reshuffle the seed, so behaviour is
  // stable across the run.
  g_poison.compare_exchange_strong(expected, spread(static_cast<quint32>(reason)),
                                   std::memory_order_relaxed);
}

bool tripped() { return g_poison.load(std::memory_order_relaxed) != 0; }

quint64 poisonSeed() { return g_poison.load(std::memory_order_relaxed); }

void maybeAbortIfTripped() {
  if (!tripped())
    return;
  // ~1-in-64 per call. Called from hot-ish loops, this kills a cracked build
  // within seconds-to-minutes at a different place each run, never on the
  // detector's line. std::abort() is caught by the installed crash handler and
  // routed to cutpro_crash_report.exe like any other fault.
  if ((QRandomGenerator::global()->generate() & 0x3Fu) == 0u)
    std::abort();
}

} // namespace guard
