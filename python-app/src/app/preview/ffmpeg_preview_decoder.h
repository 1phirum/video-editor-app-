#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <memory>

#include "app/preview/native_video_preview_decoder.h"

class QAudioSink;
class QIODevice;
class QMediaDevices;

// Decodes export-preview video and audio through the FFmpeg executable.
// Video is streamed as raw RGB frames; audio is streamed as signed 16-bit PCM
// and handed to Qt only for native device output. No Qt media decoder is used.
class FfmpegPreviewDecoder final : public QObject {
  Q_OBJECT

public:
  explicit FfmpegPreviewDecoder(QObject *parent = nullptr);
  ~FfmpegPreviewDecoder() override;

  bool start(const QString &path, const QString &mediaKind,
             qint64 sourcePositionMs, qint64 durationMs, int sourceWidth,
             int sourceHeight, double frameRate, bool audioEnabled,
             double volume, const QString &audioPath = QString());
  bool requestFrame(const QString &path, qint64 sourcePositionMs,
                    int sourceWidth, int sourceHeight);
  void stop();

  bool running() const;
  QString error() const { return m_error; }
  QImage frame() const;

  // Opens the audio output device now, so that the first Play does not.
  //
  // The stall tracer caught the GUI thread inside startAudio() for 845 ms:
  //   Backend::setPlaying -> playingChanged -> Backend::startPreviewDecode
  //     -> FfmpegPreviewDecoder::start -> startAudio -> Qt6Multimedia
  //       -> AUDIOSES.DLL -> RPCRT4!NdrClientCall3
  //         -> ntdll!NtAlpcSendWaitReceivePort
  //
  // That is a synchronous RPC to the Windows audio service: enumerate the
  // endpoints, ask the default one what it supports, initialise a client on it.
  // It costs what it costs, and it cannot be moved to a worker thread without
  // making the device object live on a thread that is not the one using it. What
  // it can be is paid once, at a moment when the user is not waiting on a button.
  //
  // Idempotent, cheap after the first call, and safe to call when there is no
  // audio device at all.
  void warmAudioOutput();

signals:
  void frameReady(quint64 revision);
  void stateChanged();
  void errorChanged();

private:
  static QString executable();
  static QSize previewSize(int sourceWidth, int sourceHeight);
  static QString seconds(qint64 milliseconds);

  bool startVideo(const QString &path, qint64 sourcePositionMs,
                  qint64 durationMs, int sourceWidth, int sourceHeight,
                  double frameRate, bool realtime, bool singleFrame);
  void startAudio(const QString &path, qint64 sourcePositionMs,
                  qint64 durationMs, double volume);
  // Enumerates the default output and negotiates a format, once. Everything
  // after the first call is a cached read; returns false when there is no usable
  // output at all, in which case preview stays silent rather than failing.
  bool resolveAudioOutput();
  // Closes the sink and forgets the QIODevice it handed out. Only for a real
  // teardown - a device change, a format change, or the destructor. An ordinary
  // stop() keeps the sink so the next start does not re-enter AUDIOSES.
  void releaseAudioSink();
  void consumeVideoOutput();
  void consumeAudioOutput();
  void setError(const QString &message);
  void stopProcess(QProcess &process);

  QProcess m_videoProcess;
  QProcess m_audioProcess;
  NativeVideoPreviewDecoder m_nativeVideo;
  QByteArray m_videoBuffer;
  QByteArray m_audioBuffer;
  mutable QMutex m_frameMutex;
  QImage m_frame;
  QSize m_frameSize;
  qsizetype m_frameBytes = 0;
  quint64 m_revision = 0;
  QString m_error;
  std::unique_ptr<QAudioSink> m_audioSink;
  QIODevice *m_audioDevice = nullptr;
  QTimer m_audioDrainTimer;
  // Held only so its audioOutputsChanged signal can invalidate the cache below.
  // Without it, unplugging headphones mid-session would leave preview writing
  // into a sink attached to a device that no longer exists.
  std::unique_ptr<QMediaDevices> m_mediaDevices;
  QAudioDevice m_audioOutputDevice;
  QAudioFormat m_audioFormat;
  bool m_audioOutputResolved = false;
  bool m_stopping = false;
};
