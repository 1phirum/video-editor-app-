#include "app/preview/preview_audio_sink.h"

#include <QAudioSink>
#include <QIODevice>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMutexLocker>
#include <QTimer>

Q_LOGGING_CATEGORY(previewAudio, "cutpro.preview.audio")

namespace {
// The pump interval. Matches the drain timer the decoder used to run on the GUI
// thread, so latency does not change - only which thread waits.
constexpr int kPumpIntervalMs = 10;
} // namespace
PreviewAudioSinkWorker::PreviewAudioSinkWorker(QObject *parent)
    : QObject(parent) {}

PreviewAudioSinkWorker::~PreviewAudioSinkWorker() = default;

void PreviewAudioSinkWorker::initialize() {
  // No CoInitializeEx here. Qt's audio backend initialises COM on whatever
  // thread it is used from, in the apartment it wants; doing it first as MTA made
  // that fail and say so:
  //   "Failed to initialize COM library (Cannot change thread mode after it is
  //    set.)"
  // Leaving it to Qt removes the conflict. If a sink still cannot start here,
  // begin() reports it and the caller falls back to the GUI-thread path.
  m_pump = new QTimer(this);
  m_pump->setInterval(kPumpIntervalMs);
  connect(m_pump, &QTimer::timeout, this, &PreviewAudioSinkWorker::pump);
}

void PreviewAudioSinkWorker::configure(const QAudioDevice &device,
                                       const QAudioFormat &format) {
  if (m_device == device && m_format == format)
    return;
  // A different endpoint or a different format is the one thing that really does
  // invalidate a sink; everything else reuses it.
  if (m_sink) {
    m_sink->stop();
    m_sink.reset();
    m_out = nullptr;
  }
  m_device = device;
  m_format = format;
  bool wasFailed = false;
  {
    QMutexLocker locker(&m_mutex);
    wasFailed = m_failed;
    m_failed = false;
  }
  if (wasFailed)
    emit failedChanged();
}

bool PreviewAudioSinkWorker::ensureSink() {
  if (m_sink)
    return true;
  if (m_device.isNull() || !m_format.isValid())
    return false;
  m_sink = std::make_unique<QAudioSink>(m_device, m_format);
  // A sink only honours setBufferSize() before start(), and it cannot be resized
  // afterwards, so it takes the widest value any source asks for. Extra margin
  // costs latency; too little costs dropouts.
  m_sink->setBufferSize(m_format.bytesForDuration(250000));
  return true;
}

void PreviewAudioSinkWorker::warm() {
  if (failed() || !ensureSink())
    return;
  QIODevice *device = m_sink->start();
  if (!device) {
    qCWarning(previewAudio) << "QAudioSink::start failed while warming"
                            << m_sink->error();
    m_sink.reset();
    {
      QMutexLocker locker(&m_mutex);
      m_failed = true;
    }
    emit failedChanged();
    return;
  }
  // Opened, then immediately silenced. The expensive half is initialising the
  // client on the endpoint; the next begin() is a local re-arm.
  m_sink->reset();
  m_out = nullptr;
}

void PreviewAudioSinkWorker::begin(double volume) {
  m_volume = qBound(0.0, volume, 1.0);
  if (failed() || !ensureSink())
    return;
  m_sink->setVolume(m_volume);
  m_out = m_sink->start();
  if (!m_out) {
    qCWarning(previewAudio) << "QAudioSink::start failed" << m_sink->error();
    m_sink.reset();
    {
      QMutexLocker locker(&m_mutex);
      m_failed = true;
    }
    emit failedChanged();
    return;
  }
  if (m_pump && !m_pump->isActive())
    m_pump->start();
}

void PreviewAudioSinkWorker::setVolume(double volume) {
  m_volume = qBound(0.0, volume, 1.0);
  // Remembered even when there is no sink yet, so the next begin() opens at the
  // level the user is currently on rather than at the one they left.
  if (m_sink)
    m_sink->setVolume(m_volume);
}

void PreviewAudioSinkWorker::resetQueue() {
  {
    QMutexLocker locker(&m_mutex);
    m_queue.clear();
  }
  if (m_pump)
    m_pump->stop();
  if (m_sink)
    m_sink->reset();
  m_out = nullptr;
}

void PreviewAudioSinkWorker::teardown() {
  if (m_pump)
    m_pump->stop();
  if (m_sink)
    m_sink->stop();
  m_sink.reset();
  m_out = nullptr;
}

void PreviewAudioSinkWorker::enqueue(const QByteArray &pcm) {
  if (pcm.isEmpty())
    return;
  QMutexLocker locker(&m_mutex);
  m_queue += pcm;
}

qint64 PreviewAudioSinkWorker::queuedBytes() const {
  QMutexLocker locker(&m_mutex);
  return m_queue.size();
}

bool PreviewAudioSinkWorker::failed() const {
  // Written on this thread, read from the GUI thread. Guarded by the same mutex
  // as the queue rather than left racy, because the caller uses it to decide
  // whether to write to its own sink instead.
  QMutexLocker locker(&m_mutex);
  return m_failed;
}

void PreviewAudioSinkWorker::pump() {
  if (!m_sink || !m_out)
    return;
  const qint64 free = m_sink->bytesFree();
  if (free <= 0)
    return;
  QByteArray chunk;
  {
    QMutexLocker locker(&m_mutex);
    if (m_queue.isEmpty())
      return;
    chunk = m_queue.left(free);
  }
  const qint64 written = m_out->write(chunk.constData(), chunk.size());
  if (written <= 0)
    return;
  QMutexLocker locker(&m_mutex);
  m_queue.remove(0, written);
}

PreviewAudioSink::PreviewAudioSink(QObject *parent) : QObject(parent) {
  m_thread.setObjectName(QStringLiteral("cutpro-preview-audio"));
  m_worker = new PreviewAudioSinkWorker;
  m_worker->moveToThread(&m_thread);
  connect(&m_thread, &QThread::started, m_worker,
          &PreviewAudioSinkWorker::initialize);
  connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
  m_thread.start();
}

PreviewAudioSink::~PreviewAudioSink() {
  if (m_thread.isRunning()) {
    // Blocking, on purpose and only here: the sink has to be closed before the
    // audio backend is torn down under it.
    QMetaObject::invokeMethod(m_worker, "teardown", Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait(2000);
  }
}

void PreviewAudioSink::configure(const QAudioDevice &device,
                                 const QAudioFormat &format) {
  QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
                            Q_ARG(QAudioDevice, device),
                            Q_ARG(QAudioFormat, format));
}

void PreviewAudioSink::warm() {
  QMetaObject::invokeMethod(m_worker, "warm", Qt::QueuedConnection);
}

void PreviewAudioSink::begin(double volume) {
  QMetaObject::invokeMethod(m_worker, "begin", Qt::QueuedConnection,
                            Q_ARG(double, volume));
}

void PreviewAudioSink::setVolume(double volume) {
  QMetaObject::invokeMethod(m_worker, "setVolume", Qt::QueuedConnection,
                            Q_ARG(double, volume));
}

void PreviewAudioSink::reset() {
  QMetaObject::invokeMethod(m_worker, "resetQueue", Qt::QueuedConnection);
}

void PreviewAudioSink::enqueue(const QByteArray &pcm) {
  m_worker->enqueue(pcm);
}

qint64 PreviewAudioSink::queuedBytes() const { return m_worker->queuedBytes(); }

bool PreviewAudioSink::failed() const { return m_worker->failed(); }
