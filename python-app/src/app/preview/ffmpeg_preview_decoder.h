#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <atomic>
#include <memory>

#include "app/preview/native_video_preview_decoder.h"
#include "app/preview/preview_audio_sink.h"
#include "core/module_api.h"

class QAudioSink;
class QIODevice;
class QMediaDevices;

// Decodes export-preview video and audio through the FFmpeg executable.
// Video is streamed as raw RGB frames; audio is streamed as signed 16-bit PCM
// and handed to Qt only for native device output. No Qt media decoder is used.
class CUTPRO_PREVIEW_API FfmpegPreviewDecoder final : public QObject {
  Q_OBJECT

public:
  explicit FfmpegPreviewDecoder(QObject *parent = nullptr);
  ~FfmpegPreviewDecoder() override;

  bool start(const QString &path, const QString &mediaKind,
             qint64 sourcePositionMs, qint64 durationMs, int sourceWidth,
             int sourceHeight, double frameRate, bool audioEnabled,
             double volume, const QString &audioPath = QString(),
             double playbackSpeed = 1.0);
  bool requestFrame(const QString &path, qint64 sourcePositionMs,
                    int sourceWidth, int sourceHeight);
  void stop();
  bool pause();
  void resume();
  bool paused() const { return m_paused; }

  // The playback level, changeable while a session is running.
  //
  // Mute used to be expressible only as start()'s audioEnabled flag, which meant a
  // track muted mid-playback kept playing until the next seek, and a track
  // un-muted mid-playback had no stream to come back to. The stream now always
  // runs when the source has audio and the level carries the mute instead, so both
  // directions take effect on the next pump rather than on the next session.
  void setAudioVolume(double volume);

  bool running() const;
  QString error() const { return m_error; }
  QImage frame(qint64 *sourceMs = nullptr) const;

  // Source position of the frame that is actually on screen, or -1 before the
  // current session has produced one. The monitor drives its playhead from this
  // instead of from a wall clock started when Play was pressed.
  qint64 presentedSourceMs() const;

  // The audio device clock in source milliseconds, or -1 when no audio is
  // playing (a silent clip, or the sink failed). This is the master clock the
  // picture is slaved to; the monitor now also drives its playhead from it so
  // the playhead shares the frames' own time base instead of free-running on a
  // wall clock and being yanked back to each new frame's pts (the playhead
  // "shake"). Safe to call from the GUI thread: it reads lock-free atomics and
  // one mutex-guarded flag.
  qint64 audioClockSourceMs() const;

  // Pixel width of that frame, or 0 when there is none. Playback runs at
  // whatever resolution the frame budget allows; a paused still is decoded at
  // the source's own. The monitor compares this against the width it actually
  // draws before deciding the swap is worth a repaint.
  int presentedWidth() const { return m_nativeVideo.presentedWidth(); }

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
  // It costs what it costs. Scoping it call by call showed which part costs it -
  // QAudioSink::start(), 2038 ms with the window already up - so the sink now
  // lives on PreviewAudioSink's own thread and this call only asks it to open.
  // Enumeration and format negotiation stay here: neither ever crossed the
  // 250 ms report threshold, and the platform device manager is a GUI-thread
  // singleton.
  //
  // Idempotent, cheap after the first call, and safe to call when there is no
  // audio device at all.
  void warmAudioOutput();

  // Post-fader peak level (0..1) of each output channel since the last read, for
  // the on-screen audio meters. Reading clears it. Safe from the GUI thread.
  void audioPeakLevels(float *left, float *right);

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
  // Process-fallback bookkeeping for presentedSourceMs(). The raw pipe carries
  // no timestamps, but the fps filter makes the mapping exact: frame i of the
  // stream is the source frame at start + i * interval. Unused when the in-process
  // decoder is compiled in, which reports its frames' own pts.
  qint64 m_streamStartSourceMs = 0;
  double m_streamFrameIntervalMs = 0.0;
  qint64 m_streamFramesShown = -1;
  std::unique_ptr<QAudioSink> m_audioSink;
  // The level the current session is playing at, so a live change survives a sink
  // that has not been created yet.
  double m_audioVolume = 1.0;
  // Source position the audio stream was started at, so the sink's played-time
  // (which counts from zero at each begin()) maps back to a source timestamp for
  // audioClockSourceMs(). Atomic: set on the GUI thread in startAudio(), read on
  // the video decode thread through the master-clock callback.
  std::atomic<qint64> m_audioStartSourceMs{0};
  std::atomic<double> m_audioPlaybackSpeed{1.0};
  std::atomic_bool m_audioSamplesReceived{false};
  double m_playbackSpeed = 1.0;
  QIODevice *m_audioDevice = nullptr;
  QTimer m_audioDrainTimer;
  // The threaded output. Every write goes here unless it reports that starting a
  // sink on its thread failed, in which case m_audioSink above still works and
  // preview audio degrades to the old GUI-thread path instead of going silent.
  PreviewAudioSink m_audioOut;
  // Ceiling on PCM handed to the pump before the process pipe is left to apply
  // backpressure. This is now the audio read-ahead cushion: ffmpeg runs WITHOUT
  // -re (see startAudio), so it races ahead and this ~2 s queue fills routinely,
  // then the pipe backpressures ffmpeg. The pump thread drains this queue, so a
  // GUI-thread stall of up to ~2 s cannot starve the device - that cushion is the
  // fix for the playback stutter / audio-repeat. 48000 Hz * 2 ch * 2 bytes * 2 s.
  static constexpr qint64 kMaxQueuedAudioBytes = 48000 * 2 * 2 * 2;
  // Held only so its audioOutputsChanged signal can invalidate the cache below.
  // Without it, unplugging headphones mid-session would leave preview writing
  // into a sink attached to a device that no longer exists.
  std::unique_ptr<QMediaDevices> m_mediaDevices;
  QAudioDevice m_audioOutputDevice;
  QAudioFormat m_audioFormat;
  bool m_audioOutputResolved = false;
  bool m_stopping = false;
  bool m_paused = false;
};
