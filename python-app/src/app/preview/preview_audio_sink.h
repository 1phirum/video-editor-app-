#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <memory>

#include "core/module_api.h"

class QAudioSink;
class QIODevice;
class QTimer;

// The preview audio output, moved off the GUI thread.
//
// Why this file exists, measured rather than assumed. With the audio warm-up
// scoped call by call, the stall tracer named one call and not its neighbours:
//   GUI thread STILL blocked after 2038 ms, "wedged in":
//     "Backend::warmAudioOutput > FfmpegPreviewDecoder::warmAudioOutput
//        > QAudioSink::start"
// QAudioSink::start() is where Qt initialises a client on the endpoint, which on
// Windows is a synchronous RPC into the audio service. Enumerating the default
// device and negotiating a format never crossed the 250 ms report threshold; this
// one call did, twice, and it is a real "Not Responding" because it happens after
// the window is up.
//
// It cannot be made faster, but it does not have to happen on the GUI thread. The
// sink and the write pump live here, on a dedicated thread with its own event
// loop; the GUI thread only ever appends bytes under a mutex.
//
// Device enumeration deliberately stays with the caller. The QAudioDevice it
// passes in is a copyable value, safe to hand across threads, whereas the
// platform device manager is a singleton already created on the GUI thread.
class CUTPRO_PREVIEW_API PreviewAudioSinkWorker final : public QObject {
  Q_OBJECT

public:
  explicit PreviewAudioSinkWorker(QObject *parent = nullptr);
  ~PreviewAudioSinkWorker() override;

  // Callable from any thread. Appends PCM for the pump to drain.
  void enqueue(const QByteArray &pcm);
  qint64 queuedBytes() const;
  // True once a start attempt failed on this thread. The caller falls back to its
  // own inline sink rather than losing audio: whether a COM-initialised worker
  // thread can drive the Windows backend is exactly the kind of thing that must
  // be observed, not assumed.
  bool failed() const;

public slots:
  // Called once when the thread starts. Creates the pump timer here so it belongs
  // to this thread; COM is left to Qt's own audio backend, which initialises the
  // apartment it wants on first use.
  void initialize();
  void configure(const QAudioDevice &device, const QAudioFormat &format);
  // Opens the device and immediately stops feeding it, so the first Play is a
  // local re-arm instead of a service round trip.
  void warm();
  void begin(double volume);
  // Live, while the sink is running. Muting a track used to be a start() argument
  // and nothing else, so toggling it during playback changed the icon and left the
  // audio playing; and un-muting had nothing to reach, because the stream was
  // never opened. The level moves instead of the stream.
  void setVolume(double volume);
  // Drops queued PCM and re-arms; the device stays open. What a scrub or a seek
  // wants, and what used to cost 845 ms to undo when the sink was destroyed.
  void resetQueue();
  void teardown();

signals:
  void failedChanged();

private:
  bool ensureSink();
  void pump();

  mutable QMutex m_mutex;
  QByteArray m_queue;
  QAudioDevice m_device;
  QAudioFormat m_format;
  std::unique_ptr<QAudioSink> m_sink;
  QIODevice *m_out = nullptr;
  QTimer *m_pump = nullptr;
  double m_volume = 1.0;
  bool m_failed = false;
};

// Owns the worker and its thread. Every method is safe to call from the GUI
// thread and none of them block on the audio service.
class CUTPRO_PREVIEW_API PreviewAudioSink final : public QObject {
  Q_OBJECT

public:
  explicit PreviewAudioSink(QObject *parent = nullptr);
  ~PreviewAudioSink() override;

  void configure(const QAudioDevice &device, const QAudioFormat &format);
  void warm();
  void begin(double volume);
  void setVolume(double volume);
  void reset();
  void enqueue(const QByteArray &pcm);
  qint64 queuedBytes() const;
  bool failed() const;

private:
  QThread m_thread;
  PreviewAudioSinkWorker *m_worker = nullptr;
};
