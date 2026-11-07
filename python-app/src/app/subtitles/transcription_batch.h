#pragma once

#include "app/subtitles/transcription_plan.h"
#include "core/module_api.h"
#include <QObject>
#include <QProcessEnvironment>
#include <QVariantMap>
#include <memory>
#include <vector>

struct TranscriptionBatchItem {
  QVariantMap media;
  TranscriptionPlan plan;
  QVariantList resumeSegments;
};

struct TranscriptionBatchRequest {
  QVector<TranscriptionBatchItem> items;
  QString program;
  QString worker;
  QString model;
  QString language;
  QString ffmpeg;
  QProcessEnvironment environment;
  qint64 maxSegmentMs = 6200;
  int concurrency = 1;
};

// Owns independent Gemini processes, buffers and scratch folders per source.
// The router already reserves one account per HTTP request; dispatching clips
// here keeps all ready accounts useful without sharing single-clip state.
class CUTPRO_SUBTITLES_API TranscriptionBatch : public QObject {
  Q_OBJECT
public:
  explicit TranscriptionBatch(QObject *parent = nullptr);
  ~TranscriptionBatch() override;
  bool start(const TranscriptionBatchRequest &request);
  void cancel();
  void clear();
  bool inProgress() const { return m_busy; }
  QVariantList jobs() const;
  QString status() const;
  double progress() const;

signals:
  void stateChanged();
  void sourceReady(const QString &mediaId, const QVariantList &segments,
                   const QString &language, bool complete);
  void jobFinished(bool success, const QString &mediaId, const QString &error);

private:
  struct Job;
  void pump();
  void launch(Job &job);
  void readOutput(Job &job, bool final = false);
  void handleEvent(Job &job, const QVariantMap &event);
  void finish(Job &job, bool cleanExit);
  TranscriptionBatchRequest m_request;
  std::vector<std::unique_ptr<Job>> m_jobs;
  int m_next = 0;
  int m_running = 0;
  bool m_busy = false;
  bool m_cancelled = false;
};
