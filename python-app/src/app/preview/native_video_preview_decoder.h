#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QString>

#include <atomic>
#include <memory>
#include <thread>

#include "app/preview/frame_buffer_pool.h"
#include "core/module_api.h"

// Only pointers to these libav types are stored, so the FFmpeg headers stay
// private to the implementation and out of every consumer of this header.
struct AVBufferRef;

// In-process FFmpeg preview decoder. It keeps packet/frame work off the GUI
// thread and publishes small QImages through the existing image provider.
// Unlike the old QProcess pipe, no process is recreated for every frame/seek.
//
// Threading contract:
//  - start()/stop() are called from the GUI thread only.
//  - Exactly one decode thread exists at a time. start() joins the previous one
//    before launching the next, so a rapid scrub cannot leave two threads
//    writing to the same members. Cancellation is checked at every packet, at
//    every catch-up frame and inside the playback pacing sleep, and libav I/O is
//    aborted through an interrupt callback, so the join costs at most one frame
//    of decode work even on a multi-gigabyte source.
//  - Every published frame carries the generation it was decoded for. A frame
//    from a superseded request is discarded rather than shown, so the monitor
//    always ends on the newest seek instead of whichever decode finished last.
class CUTPRO_PREVIEW_API NativeVideoPreviewDecoder final : public QObject {
  Q_OBJECT

public:
  explicit NativeVideoPreviewDecoder(QObject *parent = nullptr);
  ~NativeVideoPreviewDecoder() override;

  bool start(const QString &path, qint64 sourcePositionMs, qint64 durationMs,
             int sourceWidth, int sourceHeight, double frameRate,
             bool realtime, bool singleFrame);
  void stop();

  bool running() const { return m_running.load(std::memory_order_acquire); }
  QImage frame() const;
  QString error() const;

  // Frames the decoder produced but did not publish because the pool was at its
  // ceiling or the UI had not consumed the previous frame. Exposed for the
  // performance tests and the debug overlay.
  quint64 droppedFrames() const {
    return m_droppedFrames.load(std::memory_order_relaxed);
  }
  // Source position of the frame currently on screen, or -1 before this session
  // has published one.
  //
  // The monitor's playhead used to be an open-loop wall clock started the moment
  // Play was pressed, while the picture only starts once this thread has opened
  // the container, seeked and decoded a frame. Everything derived from the
  // playhead - the time display, the subtitle overlay, the still rendered on
  // pause - therefore ran ahead of the image by that startup cost, which is why
  // pausing appeared to jump the picture forward.
  //
  // Written by the decode thread on every publish, read by the GUI thread on
  // every UI tick, so it is an atomic rather than mutex-guarded: one qint64, no
  // invariant to protect, and the reader always wants the newest value.
  qint64 presentedSourceMs() const {
    return m_presentedSourceMs.load(std::memory_order_acquire);
  }
  qint64 frameMemoryBytes() const {
    return m_pool ? m_pool->allocatedBytes() : 0;
  }

signals:
  void frameReady(quint64 revision);
  void stateChanged();
  void errorChanged();

private:
  struct Request {
    QString path;
    qint64 sourcePositionMs = 0;
    qint64 durationMs = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    double frameRate = 30.0;
    bool realtime = true;
    bool singleFrame = false;
    quint64 generation = 0;
  };

  void decode(Request request);
  void publishFrame(QImage image, quint64 generation, qint64 sourceMs);
  void setError(const QString &message);
  void joinDecodeThread();
  // Creates the hardware decode device on first use and reuses it for every
  // later request. Building a D3D11 device costs tens of milliseconds and is not
  // interruptible, which is far too expensive to repeat on every scrub. Only the
  // decode thread calls this, and start() joins the previous thread first, so
  // the cache needs no lock.
  AVBufferRef *sharedHardwareDevice();
  void releaseHardwareDevice();
  bool superseded(quint64 generation) const {
    return generation != m_generation.load(std::memory_order_acquire);
  }

  mutable QMutex m_frameMutex;
  mutable QMutex m_errorMutex;
  QImage m_frame;
  QString m_error;
  std::thread m_thread;
  std::shared_ptr<FrameBufferPool> m_pool;
  AVBufferRef *m_hardwareDevice = nullptr;
  bool m_hardwareProbed = false;
  bool m_hardwareDisabled = false;
  QSize m_poolFrameSize;
  std::atomic_bool m_stopRequested{false};
  std::atomic_bool m_running{false};
  std::atomic<quint64> m_generation{0};
  std::atomic<quint64> m_droppedFrames{0};
  std::atomic<quint64> m_revision{0};
  std::atomic<qint64> m_presentedSourceMs{-1};
};
