#pragma once

#include <QVariantMap>

// Shared by selected-clip transcription, single-clip windows and translation.
// Capture this budget when a job starts: changing Settings must not interrupt
// requests already in flight. Child windows of a multi-clip batch stay at one
// request each because TranscriptionBatch owns that batch's account budget.
class GeminiWorkPolicy {
public:
  static int parallelism(const QVariantMap &settings, int readyAccounts) {
    if (!settings.value("geminiUsePool", true).toBool() ||
        !settings.value("geminiBulkProcessing", true).toBool())
      return 1;
    return qBound(1, readyAccounts, 8);
  }
};
