#include "app/preview/ffmpeg_preview_decoder.h"

#include "app/preview/gui_thread_watchdog.h"
#include "app/preview/preview_decode_policy.h"
#include "app/media/ffmpeg_runtime.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QFileInfo>
#include <QIODevice>
#include <QMediaDevices>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QtMath>

#include <new>

namespace {
// The output buffer every preview sink is built with. PreviewDecodePolicy still
// varies its own suggestion per source (150 ms for large files, 250 ms
// otherwise), but a sink cannot be resized after start() and rebuilding one costs
// the device open this whole change exists to avoid. So the sink takes the widest
// value once: extra margin only adds latency, while too little causes dropouts.
constexpr int kAudioBufferUs = 250000;
} // namespace

FfmpegPreviewDecoder::FfmpegPreviewDecoder(QObject *parent)
    : QObject(parent), m_nativeVideo(this) {
  m_videoProcess.setProcessChannelMode(QProcess::SeparateChannels);
  m_audioProcess.setProcessChannelMode(QProcess::SeparateChannels);

  connect(&m_videoProcess, &QProcess::readyReadStandardOutput, this,
          &FfmpegPreviewDecoder::consumeVideoOutput);
  connect(&m_videoProcess, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError) {
            if (!m_stopping && m_videoProcess.error() != QProcess::Crashed)
              setError(QStringLiteral("FFmpeg preview could not start: %1")
                           .arg(m_videoProcess.errorString()));
            emit stateChanged();
          });
  connect(&m_videoProcess,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int exitCode, QProcess::ExitStatus exitStatus) {
            consumeVideoOutput();
            if (!m_stopping &&
                (exitStatus != QProcess::NormalExit || exitCode != 0) &&
                m_frame.isNull()) {
              const QString details =
                  QString::fromUtf8(m_videoProcess.readAllStandardError())
                      .trimmed();
              setError(details.isEmpty()
                           ? QStringLiteral("FFmpeg could not decode the preview.")
                           : details);
            }
            emit stateChanged();
          });

  connect(&m_audioProcess, &QProcess::readyReadStandardOutput, this,
          &FfmpegPreviewDecoder::consumeAudioOutput);
  // Draining begins when ffmpeg says it is up, not when a blocking wait says so.
  // startAudio() used to call waitForStarted(1000), and the tracer caught the GUI
  // thread 944 ms deep inside it:
  //   FfmpegPreviewDecoder::startAudio -> QProcess -> CreateProcessW
  //     -> CreateProcessInternalW -> BasepCheckWinSaferRestrictions
  //       -> ntdll!NtOpenKey
  // That is Windows evaluating software-restriction policy for ffmpeg.exe, plus
  // whatever the resident scanner does to the image before it runs. It is not
  // something this process can make faster - but nothing here needed to wait for
  // it, because QProcess reports both outcomes as signals.
  connect(&m_audioProcess, &QProcess::started, this,
          [this]() { m_audioDrainTimer.start(); });
  connect(&m_audioProcess, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError) {
            if (m_stopping)
              return;
            // Failed to start, or died mid-stream: there is no more PCM coming,
            // so drop what is queued. The sink itself stays open for the next
            // attempt - closing it is what used to cost 845 ms to undo.
            m_audioDrainTimer.stop();
            m_audioBuffer.clear();
            m_audioOut.reset();
            if (m_audioSink)
              m_audioSink->reset();
            m_audioDevice = nullptr;
          });
  connect(&m_audioProcess,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int, QProcess::ExitStatus) { consumeAudioOutput(); });

  m_audioDrainTimer.setInterval(10);
  connect(&m_audioDrainTimer, &QTimer::timeout, this,
          &FfmpegPreviewDecoder::consumeAudioOutput);

  connect(&m_nativeVideo, &NativeVideoPreviewDecoder::frameReady, this,
          &FfmpegPreviewDecoder::frameReady);
  connect(&m_nativeVideo, &NativeVideoPreviewDecoder::stateChanged, this,
          &FfmpegPreviewDecoder::stateChanged);
  connect(&m_nativeVideo, &NativeVideoPreviewDecoder::errorChanged, this,
          [this]() { setError(m_nativeVideo.error()); });

  // Slave the picture to the audio device clock. Set once, here, before any
  // decode thread exists; the callback is then read on the decode thread.
  m_nativeVideo.setAudioClock([this]() { return audioClockSourceMs(); });
}

FfmpegPreviewDecoder::~FfmpegPreviewDecoder() {
  stop();
  // stop() deliberately keeps the sink alive for the next Play; the destructor is
  // the one place that really has to close it.
  releaseAudioSink();
}

QString FfmpegPreviewDecoder::executable() {
  return FfmpegRuntime::executable();
}

QString FfmpegPreviewDecoder::seconds(qint64 milliseconds) {
  return QString::number(qMax<qint64>(0, milliseconds) / 1000.0, 'f', 3);
}

QSize FfmpegPreviewDecoder::previewSize(int sourceWidth, int sourceHeight) {
  if (sourceWidth <= 0 || sourceHeight <= 0)
    return QSize(640, 360);
  // One policy for both preview paths: the source's own resolution, trimmed
  // only to what the monitor can display. The process fallback used to cap at
  // 720 px, which is a picture the user cannot get back at export time.
  return PreviewDecodePolicy::playbackSize(sourceWidth, sourceHeight);
}

bool FfmpegPreviewDecoder::running() const {
  return !m_paused && (m_nativeVideo.running() ||
         m_videoProcess.state() != QProcess::NotRunning ||
         m_audioProcess.state() != QProcess::NotRunning);
}

QImage FfmpegPreviewDecoder::frame(qint64 *sourceMs) const {
  const QImage nativeFrame = m_nativeVideo.frame(sourceMs);
  if (!nativeFrame.isNull())
    return nativeFrame;
  QMutexLocker locker(&m_frameMutex);
  if (sourceMs)
    *sourceMs = presentedSourceMs();
  return m_frame.copy();
}

qint64 FfmpegPreviewDecoder::presentedSourceMs() const {
  // Whichever path produced the picture answers for it. The in-process decoder
  // carries each frame's own pts, so it is authoritative when it has published
  // anything; the pipe fallback has to count frames instead.
  const qint64 native = m_nativeVideo.presentedSourceMs();
  if (native >= 0)
    return native;
  if (m_streamFramesShown < 0)
    return -1;
  return m_streamStartSourceMs +
         qint64(double(m_streamFramesShown) * m_streamFrameIntervalMs);
}

qint64 FfmpegPreviewDecoder::audioClockSourceMs() const {
  // The audio device clock, in source milliseconds, for the video decoder to
  // pace against. Only the threaded sink reports a position; the GUI-thread
  // fallback sink and a session with no audio return -1, and the decoder falls
  // back to its own wall-clock pacing. playedUSecs() is already -1 whenever the
  // sink is not running (a muted/absent track never begins one), so no separate
  // "audio enabled" flag is needed here.
  // A sink can open after video has already started, even for a source without
  // an audio stream. Its initial zero is not a usable media clock until this
  // session has actually supplied PCM. Keep a drained real stream's clock valid.
  if (!m_audioSamplesReceived.load(std::memory_order_acquire) || m_audioOut.failed())
    return -1;
  const qint64 playedUs = m_audioOut.playedUSecs();
  if (playedUs < 0)
    return -1;
  return m_audioStartSourceMs.load(std::memory_order_acquire) +
      qRound64(playedUs / 1000.0 * m_audioPlaybackSpeed.load(std::memory_order_acquire));
}

void FfmpegPreviewDecoder::setError(const QString &message) {
  const QString clean = message.trimmed();
  if (clean == m_error)
    return;
  m_error = clean;
  emit errorChanged();
}

void FfmpegPreviewDecoder::stopProcess(QProcess &process) {
  if (process.state() == QProcess::NotRunning)
    return;
  // Killed outright rather than asked politely. terminate() lets ffmpeg notice
  // the request at its own pace, and it may be in the middle of opening a
  // multi-gigabyte source; the old terminate-wait-kill-wait sequence could hold
  // the GUI thread for half a second per process, twice per transport press.
  // That is what made the play and step buttons feel dead after a large clip was
  // loaded. A preview ffmpeg writes to a pipe we are about to drop and has no
  // state worth flushing, so there is nothing to lose by killing it.
  //
  // The short wait stays because the QProcess is reused on the next start and
  // cannot be restarted while its child is alive - but TerminateProcess returns
  // immediately, so in practice this costs a few milliseconds instead of 500.
  process.kill();
  process.waitForFinished(250);
}

void FfmpegPreviewDecoder::stop() {
  const bool wasRunning = running() || m_paused;
  m_paused = false;
  m_stopping = true;
  m_audioSamplesReceived.store(false, std::memory_order_release);
  m_audioDrainTimer.stop();
  m_nativeVideo.stop();
  stopProcess(m_videoProcess);
  stopProcess(m_audioProcess);
  // reset(), not stop() and not a destroy: it drops whatever PCM is still queued
  // - which is what a scrub or a seek wants - while leaving the device open. The
  // old code destroyed the sink here, which meant every Play, every step and
  // every scrub paid the AUDIOSES device open again on the GUI thread. That was
  // 845 ms per transport press on the machine where this was traced.
  m_audioOut.reset();
  if (m_audioSink)
    m_audioSink->reset();
  m_audioDevice = nullptr;
  m_videoBuffer.clear();
  m_audioBuffer.clear();
  m_stopping = false;
  if (wasRunning)
    emit stateChanged();
}

bool FfmpegPreviewDecoder::pause() {
  if (m_paused)
    return true;
  // The native decoder can retain its packet, codec and pending frame. The
  // process-video fallback keeps using stop/start for its unpaced raw pipe.
  if (!m_nativeVideo.running())
    return false;
  m_paused = true;
  m_nativeVideo.pause();
  m_audioOut.pause();
  if (m_audioSink)
    m_audioSink->suspend();
  emit stateChanged();
  return true;
}

void FfmpegPreviewDecoder::resume() {
  if (!m_paused)
    return;
  m_audioOut.resume();
  if (m_audioSink)
    m_audioSink->resume();
  m_paused = false;
  m_nativeVideo.resume();
  emit stateChanged();
}

bool FfmpegPreviewDecoder::start(const QString &path,
                                 const QString &mediaKind,
                                 qint64 sourcePositionMs, qint64 durationMs,
                                 int sourceWidth, int sourceHeight,
                                 double frameRate, bool audioEnabled,
                                 double volume, const QString &audioPath,
                                 double playbackSpeed) {
  CUTPRO_GUI_SCOPE("FfmpegPreviewDecoder::start");
  stop();
  m_playbackSpeed = qIsFinite(playbackSpeed) && playbackSpeed > 0
                        ? qBound(0.05, playbackSpeed, 32.0) : 1.0;
  m_audioPlaybackSpeed.store(m_playbackSpeed, std::memory_order_release);
  setError(QString());
  if (!QFileInfo::exists(path)) {
    setError(QStringLiteral("Preview media does not exist: %1").arg(path));
    return false;
  }

  bool started = false;
  if (mediaKind != QStringLiteral("audio"))
    started = startVideo(path, sourcePositionMs, qRound64(durationMs * m_playbackSpeed), sourceWidth,
                         sourceHeight, frameRate, true, false);
  if (audioEnabled && mediaKind != QStringLiteral("image")) {
    startAudio(audioPath.trimmed().isEmpty() ? path : audioPath,
               sourcePositionMs, durationMs, volume);
    started = started || m_audioProcess.state() != QProcess::NotRunning;
  }
  emit stateChanged();
  return started;
}

bool FfmpegPreviewDecoder::requestFrame(const QString &path,
                                        qint64 sourcePositionMs,
                                        int sourceWidth, int sourceHeight) {
  CUTPRO_GUI_SCOPE("FfmpegPreviewDecoder::requestFrame");
  stop();
  m_playbackSpeed = 1.0;
  setError(QString());
  if (!QFileInfo::exists(path)) {
    setError(QStringLiteral("Preview media does not exist: %1").arg(path));
    return false;
  }
  const bool started = startVideo(path, sourcePositionMs, 0, sourceWidth,
                                  sourceHeight, 1.0, false, true);
  emit stateChanged();
  return started;
}

bool FfmpegPreviewDecoder::startVideo(const QString &path,
                                      qint64 sourcePositionMs,
                                      qint64 durationMs, int sourceWidth,
                                      int sourceHeight, double frameRate,
                                      bool realtime, bool singleFrame) {
#if defined(CUTPRO_HAS_NATIVE_FFMPEG)
  return m_nativeVideo.start(path, sourcePositionMs, durationMs, sourceWidth,
                             sourceHeight, frameRate, realtime, singleFrame,
                             m_playbackSpeed);
#else
  const PreviewDecodePolicy::Profile profile =
      PreviewDecodePolicy::forSource(path, sourceWidth, sourceHeight, frameRate,
                                     singleFrame
                                         ? PreviewDecodePolicy::Intent::Still
                                         : PreviewDecodePolicy::Intent::Playback);
  m_frameSize = profile.frameSize;
  m_frameBytes = qsizetype(m_frameSize.width()) * m_frameSize.height() * 3;
  m_videoBuffer.clear();

  const double outputFrameRate =
      qBound(1.0, frameRate > 0 ? frameRate : 30.0, 60.0);
  // Where this stream starts and how far apart its frames are, so
  // presentedSourceMs() can name the frame on screen. Reset here rather than in
  // stop(): pausing stops the session and then asks what was showing.
  m_streamStartSourceMs = qMax<qint64>(0, sourcePositionMs);
  m_streamFrameIntervalMs =
      singleFrame ? 0.0
                  : 1000.0 / qMax(1.0, qMin(outputFrameRate,
                                            profile.maximumFrameRate));
  m_streamFramesShown = -1;
  QStringList arguments{QStringLiteral("-hide_banner"),
                        QStringLiteral("-loglevel"),
                        QStringLiteral("error"),
                        QStringLiteral("-nostdin")};
  if (realtime)
    arguments << QStringLiteral("-readrate") << QString::number(m_playbackSpeed);
  if (profile.lightweight) {
    arguments << QStringLiteral("-probesize")
              << QString::number(profile.probeSizeBytes)
              << QStringLiteral("-analyzeduration")
              << QString::number(profile.analyzeDurationUs);
  }
  if (profile.decoderThreads > 0)
    arguments << QStringLiteral("-threads")
              << QString::number(profile.decoderThreads);
  arguments << QStringLiteral("-ss") << seconds(sourcePositionMs)
            << QStringLiteral("-i") << path;
  if (durationMs > 0)
    arguments << QStringLiteral("-t") << seconds(durationMs);
  arguments << QStringLiteral("-map") << QStringLiteral("0:v:0")
            << QStringLiteral("-an") << QStringLiteral("-sn");
  if (singleFrame)
    arguments << QStringLiteral("-frames:v") << QStringLiteral("1");

  QString filter;
  if (singleFrame) {
    // Bicubic, not fast_bilinear: this frame is the still the user studies.
    filter = QStringLiteral("scale=%1:%2:flags=bicubic")
                 .arg(m_frameSize.width())
                 .arg(m_frameSize.height());
  } else {
    filter = QStringLiteral("fps=%1,scale=%2:%3:flags=bilinear")
                 .arg(qMin(outputFrameRate, profile.maximumFrameRate), 0, 'f', 3)
                 .arg(m_frameSize.width())
                 .arg(m_frameSize.height());
  }
  arguments << QStringLiteral("-vf") << filter << QStringLiteral("-pix_fmt")
            << QStringLiteral("rgb24") << QStringLiteral("-f")
            << QStringLiteral("rawvideo") << QStringLiteral("pipe:1");

  m_videoProcess.start(executable(), arguments, QIODevice::ReadOnly);
  // Optimistic, and deliberately not waitForStarted(1500): launching ffmpeg.exe
  // costs the best part of a second on Windows once software-restriction policy
  // and the resident scanner have had their turn, and none of that has to happen
  // on the GUI thread. A real failure to start still surfaces - the errorOccurred
  // handler in the constructor calls setError with the process's own message -
  // and running() already treats a Starting process as running, so the transport
  // state does not flicker.
  return true;
#endif
}

void FfmpegPreviewDecoder::releaseAudioSink() {
  if (m_audioSink)
    m_audioSink->stop();
  m_audioSink.reset();
  m_audioDevice = nullptr;
}

bool FfmpegPreviewDecoder::resolveAudioOutput() {
  if (m_audioOutputResolved)
    return m_audioFormat.isValid();

  // Named so that if this ever stalls again the watchdog reports the call rather
  // than "no marked scope". Every AUDIOSES round trip in this class is below it.
  CUTPRO_GUI_SCOPE("FfmpegPreviewDecoder::resolveAudioOutput");
  m_audioOutputResolved = true;
  if (!m_mediaDevices) {
    CUTPRO_GUI_SCOPE("QMediaDevices::QMediaDevices");
    m_mediaDevices = std::make_unique<QMediaDevices>();
    connect(m_mediaDevices.get(), &QMediaDevices::audioOutputsChanged, this,
            [this]() {
              // Headphones in or out, a monitor with speakers waking up: the
              // cached endpoint may be gone. Drop everything and let the next
              // start negotiate again - it is the only time re-paying the open is
              // the correct answer.
              m_audioOutputResolved = false;
              m_audioFormat = QAudioFormat();
              m_audioOutputDevice = QAudioDevice();
              m_audioOut.reset();
              releaseAudioSink();
            });
  }

  {
    // Split out from the rest so the watchdog names which AUDIOSES round trip
    // is the slow one. The 2017 ms stall this warm-up produced was reported as
    // "warmAudioOutput" as a whole, which is three separate service calls -
    // device enumeration, format negotiation and client initialisation - and
    // they have different fixes.
    CUTPRO_GUI_SCOPE("QMediaDevices::defaultAudioOutput");
    m_audioOutputDevice = QMediaDevices::defaultAudioOutput();
  }
  if (m_audioOutputDevice.isNull())
    return false;

  QAudioFormat format;
  format.setSampleRate(48000);
  format.setChannelCount(2);
  format.setSampleFormat(QAudioFormat::Int16);
  {
    CUTPRO_GUI_SCOPE("QAudioDevice::isFormatSupported");
    if (!m_audioOutputDevice.isFormatSupported(format))
      format = m_audioOutputDevice.preferredFormat();
  }
  // ffmpeg is asked for s16le below, so a device that will not take Int16 cannot
  // be fed without a conversion step this preview path does not have.
  if (format.sampleFormat() != QAudioFormat::Int16)
    return false;
  m_audioFormat = format;
  // The threaded sink is told what to open before anyone asks it to open one, so
  // that warm() and begin() are pure re-arms.
  m_audioOut.configure(m_audioOutputDevice, m_audioFormat);
  return true;
}

void FfmpegPreviewDecoder::warmAudioOutput() {
  if (!resolveAudioOutput())
    return;

  CUTPRO_GUI_SCOPE("FfmpegPreviewDecoder::warmAudioOutput");
  // Asked for, not waited for. The 2038 ms the tracer measured inside
  // QAudioSink::start() now happens on the audio thread, where nothing is
  // painting.
  m_audioOut.warm();
  if (!m_audioOut.failed())
    return;

  // Only reached if the worker could not drive a sink on its own thread. Then the
  // cost is paid here, as it was before, rather than losing preview audio.
  if (m_audioSink)
    return;
  {
    CUTPRO_GUI_SCOPE("QAudioSink::QAudioSink");
    m_audioSink =
        std::make_unique<QAudioSink>(m_audioOutputDevice, m_audioFormat);
  }
  // The widest buffer any source asks for. A sink only honours setBufferSize()
  // before start(), so a reused sink keeps whatever it was built with - and too
  // much margin only costs latency, while too little costs an underrun.
  m_audioSink->setBufferSize(m_audioFormat.bytesForDuration(kAudioBufferUs));
  // start() is what actually initialises the client on the endpoint, which is the
  // expensive half. Immediately reset so nothing is playing; the device stays
  // open and the next start() is a local re-arm rather than a service round trip.
  {
    CUTPRO_GUI_SCOPE("QAudioSink::start");
    if (!m_audioSink->start()) {
      releaseAudioSink();
      return;
    }
  }
  m_audioSink->reset();
  m_audioDevice = nullptr;
}

void FfmpegPreviewDecoder::audioPeakLevels(float *left, float *right) {
  // The threaded sink is the live path on every machine that could start it; it
  // is the one fed the PCM, so it is the one that measured the peaks.
  m_audioOut.peakLevels(left, right);
}

void FfmpegPreviewDecoder::setAudioVolume(double volume) {
  const double bounded = qBound(0.0, volume, 1.0);
  m_audioVolume = bounded;
  // Both paths, because which one is live depends on whether the threaded sink
  // managed to start on this machine.
  if (!m_audioOut.failed())
    m_audioOut.setVolume(bounded);
  if (m_audioSink)
    m_audioSink->setVolume(bounded);
}

void FfmpegPreviewDecoder::startAudio(const QString &path,
                                      qint64 sourcePositionMs,
                                      qint64 durationMs, double volume) {
  m_audioVolume = qBound(0.0, volume, 1.0);
  if (!resolveAudioOutput())
    return;

  // Where this audio session starts, so the sink's played-time (which counts
  // from zero at each begin()) maps back to a source timestamp for the video
  // decoder's master clock. Set before begin() so the clock is coherent the
  // moment the sink reports a position.
  m_audioStartSourceMs.store(qMax<qint64>(0, sourcePositionMs),
                             std::memory_order_release);

  if (!m_audioOut.failed()) {
    // Arms the threaded sink. Queued, so pressing Play returns to the event loop
    // whether or not the endpoint is quick about it today.
    m_audioOut.begin(m_audioVolume);
  } else {
    // A device change is the only thing that invalidates a sink; everything else
    // reuses it, which is the whole point of the change.
    if (m_audioSink && m_audioSink->format() != m_audioFormat)
      releaseAudioSink();
    if (!m_audioSink) {
      m_audioSink =
          std::make_unique<QAudioSink>(m_audioOutputDevice, m_audioFormat);
      m_audioSink->setBufferSize(m_audioFormat.bytesForDuration(kAudioBufferUs));
    }

    m_audioSink->setVolume(m_audioVolume);
    m_audioDevice = m_audioSink->start();
    if (!m_audioDevice) {
      releaseAudioSink();
      return;
    }
  }

  const PreviewDecodePolicy::Profile profile =
      PreviewDecodePolicy::forSource(path, 0, 0, 0.0);
  // No -re here, deliberately. -re paces ffmpeg to 1x real time, which kept the
  // PCM queue near-empty: production tracked consumption exactly, so only the
  // 250 ms device buffer ever stood between the audio thread's pump and an
  // underrun. Any GUI-thread hitch longer than that (a synchronous preview-frame
  // upload, a seek's waitForFinished, an autosave write) drained the device
  // buffer, WASAPI replayed its stale ring-buffer tail - the audio "speaking the
  // segment again" - and because the video decoder is slaved to the audio clock
  // it froze then jumped to catch up. Playback speed does NOT depend on -re: the
  // QAudioSink endpoint clock paces consumption and the decoder slaves to that
  // clock (native_video_preview_decoder.cpp), not to ffmpeg's output rate.
  // Dropping -re lets ffmpeg race ahead and fill the existing ~2 s backpressure
  // queue (kMaxQueuedAudioBytes), which the independent pump thread drains - so a
  // GUI stall now has ~2 s of buffered audio to hide behind instead of ~0. Memory
  // stays bounded: consumeAudioOutput() stops reading the pipe at that ceiling and
  // ffmpeg blocks on write.
  QStringList arguments{QStringLiteral("-hide_banner"),
                        QStringLiteral("-loglevel"),
                        QStringLiteral("error"),
                        QStringLiteral("-nostdin")};
  if (profile.lightweight) {
    arguments << QStringLiteral("-probesize")
              << QString::number(profile.probeSizeBytes)
              << QStringLiteral("-analyzeduration")
              << QString::number(profile.analyzeDurationUs);
  }
  arguments << QStringLiteral("-ss") << seconds(sourcePositionMs)
            << QStringLiteral("-i") << path;
  if (durationMs > 0)
    arguments << QStringLiteral("-t") << seconds(durationMs);
  if (!qFuzzyCompare(m_playbackSpeed, 1.0)) {
    QStringList stages;
    double remaining = m_playbackSpeed;
    while (remaining > 2.0) {
      stages << QStringLiteral("atempo=2.0");
      remaining /= 2.0;
    }
    while (remaining < 0.5) {
      stages << QStringLiteral("atempo=0.5");
      remaining /= 0.5;
    }
    stages << QStringLiteral("atempo=%1").arg(remaining, 0, 'f', 6);
    arguments << QStringLiteral("-af") << stages.join(',');
  }
  arguments << QStringLiteral("-map") << QStringLiteral("0:a:0?")
            << QStringLiteral("-vn") << QStringLiteral("-sn")
            << QStringLiteral("-acodec") << QStringLiteral("pcm_s16le")
            << QStringLiteral("-ar") << QString::number(m_audioFormat.sampleRate())
            << QStringLiteral("-ac")
            << QString::number(m_audioFormat.channelCount())
            << QStringLiteral("-f") << QStringLiteral("s16le")
            << QStringLiteral("pipe:1");
  // Fire and forget. The started/errorOccurred handlers wired in the constructor
  // take it from here, so pressing Play returns to the event loop immediately
  // instead of holding it through CreateProcessW.
  m_audioProcess.start(executable(), arguments, QIODevice::ReadOnly);
}

void FfmpegPreviewDecoder::consumeVideoOutput() {
  m_videoBuffer += m_videoProcess.readAllStandardOutput();
  if (m_frameBytes <= 0)
    return;

  bool changed = false;
  while (m_videoBuffer.size() >= m_frameBytes) {
    const QImage frame(reinterpret_cast<const uchar *>(m_videoBuffer.constData()),
                       m_frameSize.width(), m_frameSize.height(),
                       m_frameSize.width() * 3, QImage::Format_RGB888);
    {
      QMutexLocker locker(&m_frameMutex);
      m_frame = frame.copy();
    }
    m_videoBuffer.remove(0, m_frameBytes);
    ++m_streamFramesShown;
    changed = true;
  }
  if (changed)
    emit frameReady(++m_revision);
}

void FfmpegPreviewDecoder::consumeAudioOutput() {
  if (m_stopping)
    return;
  // Reading the pipe allocates, and on a machine that is genuinely out of
  // memory that allocation throws. It used to throw straight out of here:
  // std::bad_alloc from QByteArray::resize inside readAllStandardOutput, with
  // nothing to catch it, so the process called terminate() and the editor died
  // taking the edit with it. Losing preview audio is the right way to lose this
  // fight - the bytes left in the pipe also stall ffmpeg, which relieves the
  // pressure instead of adding to it.
  const auto readAvailable = [this](bool *ok) -> QByteArray {
    try {
      *ok = true;
      return m_audioProcess.readAllStandardOutput();
    } catch (const std::bad_alloc &) {
      *ok = false;
      return {};
    }
  };

  if (!m_audioOut.failed()) {
    // Backpressure by not reading: leaving bytes in the pipe stalls ffmpeg, which
    // is the right answer when the endpoint is behind. Reading them into an
    // unbounded queue would not be.
    if (m_audioOut.queuedBytes() >= kMaxQueuedAudioBytes)
      return;
    bool ok = false;
    const QByteArray chunk = readAvailable(&ok);
    if (!ok)
      return;
    try {
      m_audioOut.enqueue(chunk);
      if (!chunk.isEmpty())
        m_audioSamplesReceived.store(true, std::memory_order_release);
    } catch (const std::bad_alloc &) {
      // Drop the chunk rather than the application.
    }
    return;
  }

  // Fallback path (no pump thread). This had no ceiling at all: whenever the
  // sink could not accept bytes - no sink yet, or bytesFree() at zero - the
  // buffer grew without limit until the allocation failed. Cap it at the same
  // ceiling the primary path uses and stop reading once it is reached, so this
  // path applies the same backpressure instead of hoarding.
  if (m_audioBuffer.size() < kMaxQueuedAudioBytes) {
    bool ok = false;
    const QByteArray chunk = readAvailable(&ok);
    if (ok && !chunk.isEmpty()) {
      try {
        m_audioBuffer += chunk;
      } catch (const std::bad_alloc &) {
        m_audioBuffer.clear();
        return;
      }
    }
  }
  if (!m_audioSink || !m_audioDevice || m_audioBuffer.isEmpty())
    return;

  const qint64 writable =
      qMin<qint64>(m_audioSink->bytesFree(), m_audioBuffer.size());
  if (writable <= 0)
    return;
  const qint64 written = m_audioDevice->write(m_audioBuffer.constData(), writable);
  if (written > 0)
    m_audioBuffer.remove(0, written);
}
