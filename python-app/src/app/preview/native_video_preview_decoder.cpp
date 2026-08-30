#include "app/preview/native_video_preview_decoder.h"

#include "app/diagnostics/playback_trace.h"
#include "app/media/media_path.h"
#include "app/preview/decode_cost_model.h"
#include "app/preview/gui_thread_watchdog.h"
#include "app/preview/preview_decode_policy.h"
#include "app/preview/swscale_colorspace.h"

#include <QElapsedTimer>
#include <QMutexLocker>
#include <QLoggingCategory>
#include <QThread>

#include <chrono>
#include <cstdarg>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace {

// Frames are now decoded at the source's resolution (see PreviewDecodePolicy),
// so the pool ceiling has to follow the frame size instead of being a constant:
// a 1080p RGBA frame is 8 MB where a 480p one was under a megabyte. These are
// the bounds the per-source budget is clamped into, and the multiplier is how
// many frames may be in flight - one on screen, one being uploaded, the rest
// decoded ahead.
constexpr qint64 kFramePoolMinimumBytes = 48LL * 1024 * 1024;
constexpr qint64 kFramePoolMaximumBytes = 320LL * 1024 * 1024;
constexpr int kFramePoolFrames = 6;

qint64 framePoolBudget(const QSize &frameSize) {
  const qint64 frameBytes =
      qint64(FrameBufferPool::bytesPerLine(frameSize.width(),
                                           QImage::Format_RGBA8888)) *
      qMax(1, frameSize.height());
  return qBound(kFramePoolMinimumBytes, frameBytes * kFramePoolFrames,
                kFramePoolMaximumBytes);
}

// Bail out of a seek-and-decode that is finding no usable frame. Without this a
// source with a broken index can spin through the whole file on the decode
// thread while the user waits for a single still.
constexpr int kMaxPacketsWithoutOutput = 2000;

// Consecutive GPU-to-CPU frame transfer failures tolerated before the session
// gives up on hardware decode and restarts in software.
constexpr int kMaxHardwareTransferFailures = 8;

// QtWarningMsg, not the default: the two-argument form of this macro enables
// qCDebug as well, which is what let the container's own chatter ("Missing key
// frame while searching for timestamp: 0") reach the console on every open of a
// long MP4 with edit lists. Real decode errors still come through as warnings.
Q_LOGGING_CATEGORY(previewFfmpegLog, "cutpro.preview.ffmpeg", QtWarningMsg)

// libav writes container warnings such as "Missing key frame while searching for
// timestamp" straight to stderr at INFO/WARNING level. On a long MP4 with edit
// lists that is a stream of console output produced from the decode thread for
// every open. Route it into a Qt logging category instead: silent by default,
// available with QT_LOGGING_RULES="cutpro.preview.ffmpeg.debug=true".
void forwardAvLog(void *context, int level, const char *format, va_list args) {
  if (level > AV_LOG_WARNING)
    return;
  char line[1024]{};
  int printPrefix = 1;
  av_log_format_line2(context, level, format, args, line, sizeof(line),
                      &printPrefix);
  const QString message = QString::fromUtf8(line).trimmed();
  if (message.isEmpty())
    return;
  if (level <= AV_LOG_ERROR)
    qCWarning(previewFfmpegLog).noquote() << message;
  else
    qCDebug(previewFfmpegLog).noquote() << message;
}

void installAvLogHandler() {
  static std::once_flag once;
  std::call_once(once, [] {
    av_log_set_level(AV_LOG_WARNING);
    av_log_set_callback(forwardAvLog);
  });
}

struct DecodeState {
  AVPixelFormat hardwareFormat = AV_PIX_FMT_NONE;
};

QString ffmpegError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(code, buffer, sizeof(buffer));
  return QString::fromUtf8(buffer);
}

int interruptIo(void *opaque) {
  auto *stop = static_cast<std::atomic_bool *>(opaque);
  return stop && stop->load(std::memory_order_acquire) ? 1 : 0;
}

AVPixelFormat selectPixelFormat(AVCodecContext *context,
                                const AVPixelFormat *formats) {
  auto *state = static_cast<DecodeState *>(context->opaque);
  if (state && state->hardwareFormat != AV_PIX_FMT_NONE) {
    for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE;
         ++format) {
      if (*format == state->hardwareFormat)
        return *format;
    }
  }
  for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE;
       ++format) {
    const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(*format);
    if (!descriptor || !(descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL))
      return *format;
  }
  return formats[0];
}

// Hardware frames arriving in a format swscale cannot read are transferred to
// the closest supported software format instead of being dropped silently.
bool transferHardwareFrame(AVFrame *destination, const AVFrame *source) {
  av_frame_unref(destination);
  return av_hwframe_transfer_data(destination, source, 0) >= 0;
}

} // namespace

NativeVideoPreviewDecoder::NativeVideoPreviewDecoder(QObject *parent)
    : QObject(parent),
      m_pool(FrameBufferPool::create(kFramePoolMinimumBytes)) {
  installAvLogHandler();
}

NativeVideoPreviewDecoder::~NativeVideoPreviewDecoder() {
  stop();
  releaseHardwareDevice();
}

void NativeVideoPreviewDecoder::joinDecodeThread() {
  if (!m_thread.joinable())
    return;
  // The one blocking call left on the GUI thread in this class. It is bounded in
  // theory - the decode thread checks the stop flag at every packet, at every
  // catch-up frame and inside the pacing sleep, and libav I/O is aborted through
  // the interrupt callback - so the cost should be a single frame of decode work.
  // On an 8 hour 4K AV1 source a single software frame is not cheap, and this is
  // reached from start(), which QML calls whenever the active clip changes,
  // including mid-drag. Marked rather than assumed: if this is the stall, the
  // watchdog now says so by name instead of it being one more suspect.
  CUTPRO_GUI_SCOPE("NativeVideoPreviewDecoder::joinDecodeThread");
  m_thread.join();
}

bool NativeVideoPreviewDecoder::start(const QString &path,
                                      qint64 sourcePositionMs,
                                      qint64 durationMs, int sourceWidth,
                                      int sourceHeight, double frameRate,
                                      bool realtime, bool singleFrame) {
  CUTPRO_GUI_SCOPE("NativeVideoPreviewDecoder::start");
  stop();
  setError({});

  // Validate before spawning a thread: a missing, unreadable or unencodable
  // path is a user-facing message, not a decode failure to be discovered deep
  // inside libav with a misleading "No such file or directory".
  QString reason;
  if (!MediaPath::isDecodable(path, &reason)) {
    setError(reason);
    return false;
  }

  const quint64 generation =
      m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  Request request{path,        qMax<qint64>(0, sourcePositionMs),
                  qMax<qint64>(0, durationMs),
                  sourceWidth, sourceHeight,
                  frameRate,   realtime,
                  singleFrame, generation};

  m_stopRequested.store(false, std::memory_order_release);
  m_running.store(true, std::memory_order_release);
  // Cleared here and not in stop(): the pause path stops the playback session
  // and then asks what frame was on screen, so the answer has to outlive the
  // session that produced it.
  m_presentedSourceMs.store(-1, std::memory_order_release);
  emit stateChanged();
  m_thread = std::thread([this, request]() { decode(request); });
  return true;
}

void NativeVideoPreviewDecoder::stop() {
  const bool wasRunning =
      m_running.load(std::memory_order_acquire) || m_thread.joinable();
  // Bump the generation first so a frame decoded in the race window between the
  // stop request and the thread actually exiting is discarded, not published.
  m_generation.fetch_add(1, std::memory_order_acq_rel);
  m_stopRequested.store(true, std::memory_order_release);
  joinDecodeThread();
  m_running.store(false, std::memory_order_release);
  if (wasRunning)
    emit stateChanged();
}

QImage NativeVideoPreviewDecoder::frame() const {
  QMutexLocker locker(&m_frameMutex);
  // Implicitly shared: this is a refcount bump, not a pixel copy. The pooled
  // buffer stays alive for exactly as long as some copy of the frame does.
  return m_frame;
}

QString NativeVideoPreviewDecoder::error() const {
  QMutexLocker locker(&m_errorMutex);
  return m_error;
}

void NativeVideoPreviewDecoder::setError(const QString &message) {
  const QString clean = message.trimmed();
  {
    QMutexLocker locker(&m_errorMutex);
    if (m_error == clean)
      return;
    m_error = clean;
  }
  emit errorChanged();
}

void NativeVideoPreviewDecoder::publishFrame(QImage image, quint64 generation,
                                             qint64 sourceMs) {
  if (image.isNull() || m_stopRequested.load(std::memory_order_acquire) ||
      superseded(generation)) {
    m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  {
    QMutexLocker locker(&m_frameMutex);
    // Re-check under the lock: stop() may have bumped the generation between
    // the check above and here, and publishing then would leave a stale frame
    // visible after the user already moved on.
    if (superseded(generation)) {
      m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    m_frame = std::move(image);
  }
  // Only frames that reach the screen move this. A dropped or superseded frame
  // leaves it alone, so it always names the picture the user is looking at.
  m_presentedSourceMs.store(sourceMs, std::memory_order_release);
  // Called from the decode thread, which is why the trace never evaluates
  // JavaScript here: it records the pacing of the picture, and only lines that
  // break the expected one-frame-per-interval rhythm are emitted.
  PlaybackTrace::instance().recordPresentedFrame(sourceMs);
  emit frameReady(m_revision.fetch_add(1, std::memory_order_relaxed) + 1);
}

AVBufferRef *NativeVideoPreviewDecoder::sharedHardwareDevice() {
#if defined(Q_OS_WIN)
  if (m_hardwareDisabled)
    return nullptr;
  if (!m_hardwareProbed) {
    m_hardwareProbed = true;
    const AVHWDeviceType deviceType = av_hwdevice_find_type_by_name("d3d11va");
    if (deviceType == AV_HWDEVICE_TYPE_NONE ||
        av_hwdevice_ctx_create(&m_hardwareDevice, deviceType, nullptr, nullptr,
                              0) < 0) {
      m_hardwareDevice = nullptr;
      m_hardwareDisabled = true;
    }
  }
  return m_hardwareDevice;
#else
  return nullptr;
#endif
}

void NativeVideoPreviewDecoder::releaseHardwareDevice() {
  if (m_hardwareDevice)
    av_buffer_unref(&m_hardwareDevice);
  m_hardwareDevice = nullptr;
  m_hardwareProbed = false;
}

void NativeVideoPreviewDecoder::decode(Request request) {
  // A single frame is a still: it gets the larger bound and the low-latency
  // decoder setup, because there is no frame rate to hold.
  const PreviewDecodePolicy::Profile profile = PreviewDecodePolicy::forSource(
      request.path, request.sourceWidth, request.sourceHeight,
      request.frameRate,
      request.singleFrame ? PreviewDecodePolicy::Intent::Still
                          : PreviewDecodePolicy::Intent::Playback);

  // Buffers sized for a previous source must not sit on the budget once the
  // frame geometry changes, and the ceiling itself moves with the geometry.
  if (profile.frameSize != m_poolFrameSize) {
    m_pool->trim();
    m_pool->setByteBudget(framePoolBudget(profile.frameSize));
    m_poolFrameSize = profile.frameSize;
  }

  AVFormatContext *format = nullptr;
  AVCodecContext *codecContext = nullptr;
  AVPacket *packet = nullptr;
  AVFrame *decodedFrame = nullptr;
  AVFrame *softwareFrame = nullptr;
  SwsContext *scaler = nullptr;
  AVDictionary *options = nullptr;
  // From the first instruction this thread runs to the first frame that reaches
  // the screen: container open, seek, hardware device creation on first use, and
  // one decode. The monitor's playhead used to start counting before all of it,
  // which is the whole of the lead the picture appeared to have.
  QElapsedTimer sessionTimer;
  sessionTimer.start();

  auto finish = [&]() {
    sws_freeContext(scaler);
    av_frame_free(&softwareFrame);
    av_frame_free(&decodedFrame);
    av_packet_free(&packet);
    // Releases this session's reference to the shared hardware device; the
    // cached device itself outlives the session.
    avcodec_free_context(&codecContext);
    avformat_close_input(&format);
    av_dict_free(&options);
    m_running.store(false, std::memory_order_release);
    emit stateChanged();
  };

  // A stale request is dropped before any I/O: during a fast scrub most queued
  // decodes are already obsolete by the time their thread starts.
  if (superseded(request.generation)) {
    finish();
    return;
  }

  format = avformat_alloc_context();
  if (!format) {
    setError(QStringLiteral("Could not allocate FFmpeg input context."));
    finish();
    return;
  }
  format->interrupt_callback.callback = interruptIo;
  format->interrupt_callback.opaque = &m_stopRequested;
  if (profile.lightweight) {
    const QByteArray probe = QByteArray::number(profile.probeSizeBytes);
    const QByteArray analyze = QByteArray::number(profile.analyzeDurationUs);
    av_dict_set(&options, "probesize", probe.constData(), 0);
    av_dict_set(&options, "analyzeduration", analyze.constData(), 0);
  }

  // UTF-8 with an explicit protocol. QFile::encodeName() would mangle any path
  // component outside the local 8-bit code page.
  const QByteArray url = MediaPath::toFfmpegUrl(request.path);
  int result = avformat_open_input(&format, url.constData(), nullptr, &options);
  if (result < 0) {
    // avformat_open_input() frees and nulls the context on failure; do not let
    // finish() close it a second time.
    format = nullptr;
    if (!m_stopRequested.load(std::memory_order_acquire) &&
        !superseded(request.generation))
      setError(QStringLiteral("FFmpeg could not open preview: %1")
                   .arg(ffmpegError(result)));
    finish();
    return;
  }

  result = avformat_find_stream_info(format, nullptr);
  // A missing or partial index is common in long recordings and is not fatal on
  // its own, so the message is only reported if stream selection then fails.
  const QString streamInfoWarning =
      result < 0 ? QStringLiteral("FFmpeg could not read stream information: %1")
                       .arg(ffmpegError(result))
                 : QString();

  const AVCodec *codec = nullptr;
  const int streamIndex =
      av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
  if (streamIndex < 0 || !codec) {
    setError(streamInfoWarning.isEmpty()
                 ? QStringLiteral("The source has no decodable video stream.")
                 : streamInfoWarning);
    finish();
    return;
  }

  AVStream *stream = format->streams[streamIndex];
  codecContext = avcodec_alloc_context3(codec);
  if (!codecContext ||
      avcodec_parameters_to_context(codecContext, stream->codecpar) < 0) {
    setError(QStringLiteral("Could not initialize the video codec."));
    finish();
    return;
  }
  codecContext->thread_count = profile.decoderThreads;
  codecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

  DecodeState decodeState;
#if defined(Q_OS_WIN)
  // Hardware decode is opportunistic. If the device or the decoder refuses it,
  // fall through to software rather than failing the preview.
  if (AVBufferRef *device = sharedHardwareDevice()) {
    const AVHWDeviceType deviceType = av_hwdevice_find_type_by_name("d3d11va");
    for (int index = 0; deviceType != AV_HWDEVICE_TYPE_NONE; ++index) {
      const AVCodecHWConfig *config = avcodec_get_hw_config(codec, index);
      if (!config)
        break;
      if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
          config->device_type == deviceType) {
        decodeState.hardwareFormat = config->pix_fmt;
        break;
      }
    }
    if (decodeState.hardwareFormat != AV_PIX_FMT_NONE) {
      codecContext->hw_device_ctx = av_buffer_ref(device);
      codecContext->opaque = &decodeState;
      codecContext->get_format = selectPixelFormat;
    }
  }
#endif

  result = avcodec_open2(codecContext, codec, nullptr);
  if (result < 0 && decodeState.hardwareFormat != AV_PIX_FMT_NONE) {
    // Retry in pure software once. Some drivers accept the device but reject
    // the profile, and a broken hardware path must not disable preview.
    decodeState.hardwareFormat = AV_PIX_FMT_NONE;
    avcodec_free_context(&codecContext);
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext ||
        avcodec_parameters_to_context(codecContext, stream->codecpar) < 0) {
      setError(QStringLiteral("Could not initialize the video codec."));
      finish();
      return;
    }
    codecContext->thread_count = profile.decoderThreads;
    codecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    result = avcodec_open2(codecContext, codec, nullptr);
  }
  if (result < 0) {
    setError(QStringLiteral("FFmpeg could not open the video decoder: %1")
                 .arg(ffmpegError(result)));
    finish();
    return;
  }

  double startSeconds = request.sourcePositionMs / 1000.0;
  if (request.sourcePositionMs > 0) {
    const int64_t targetTimestamp =
        av_rescale_q(request.sourcePositionMs, AVRational{1, 1000},
                     stream->time_base);
    // AVSEEK_FLAG_BACKWARD lands on the keyframe at or before the target, so
    // decoding forward from there reaches the requested position.
    if (av_seek_frame(format, streamIndex, targetTimestamp,
                      AVSEEK_FLAG_BACKWARD) < 0) {
      // A damaged or absent index - the "Cannot find an index entry" case - puts
      // the demuxer back at the beginning. Decoding forward to the requested
      // position from there would mean decoding hours of video on this thread,
      // so accept the first frame the file yields instead.
      av_seek_frame(format, streamIndex, 0,
                    AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
      startSeconds = 0.0;
    }
    avcodec_flush_buffers(codecContext);
  }

  packet = av_packet_alloc();
  decodedFrame = av_frame_alloc();
  softwareFrame = av_frame_alloc();
  if (!packet || !decodedFrame || !softwareFrame) {
    setError(QStringLiteral("Could not allocate FFmpeg frame buffers."));
    finish();
    return;
  }

  const double maximumFps = qMax(1.0, profile.maximumFrameRate);
  const double minimumFrameDelta = 1.0 / maximumFps;
  double firstOutputSeconds = -1.0;
  double lastOutputSeconds = -1000.0;
  const auto playbackStart = std::chrono::steady_clock::now();
  int packetsWithoutOutput = 0;
  int hardwareTransferFailures = 0;
  bool retryInSoftware = false;
  bool done = false;

  // Nothing is resampled while the frame is decoded at the source's own size, so
  // the filter only matters on the trade-down path: a still is resampled once
  // and can afford bicubic, playback stays on bilinear.
  const int scalerFlags = profile.nativeResolution ? SWS_POINT
                          : request.singleFrame    ? SWS_BICUBIC
                                                   : SWS_BILINEAR;
  // sws_getCachedContext reconfigures a context in place, which drops the colour
  // matrix set on it. Track what the current one was configured for so the
  // matrix is reapplied after any reconfiguration - and only then, since it
  // rebuilds swscale's tables.
  SwsContext *configuredScaler = nullptr;
  int configuredSourceWidth = 0;
  int configuredSourceHeight = 0;
  int configuredSourceFormat = -1;
  int configuredColorspace = -1;
  int configuredRange = -1;
  // Read, decode and convert time for one output frame, with the pacing sleep
  // left out: this is what DecodeCostModel turns into a preview bound, so it has
  // to measure work rather than waiting.
  QElapsedTimer frameWorkTimer;
  frameWorkTimer.start();

  const auto cancelled = [&]() {
    return m_stopRequested.load(std::memory_order_acquire) ||
           superseded(request.generation);
  };

  while (!done && !cancelled() && av_read_frame(format, packet) >= 0) {
    if (packet->stream_index != streamIndex) {
      av_packet_unref(packet);
      continue;
    }
    result = avcodec_send_packet(codecContext, packet);
    av_packet_unref(packet);
    if (result < 0 && result != AVERROR(EAGAIN))
      continue;

    if (++packetsWithoutOutput > kMaxPacketsWithoutOutput) {
      if (firstOutputSeconds < 0)
        setError(QStringLiteral(
            "FFmpeg could not find a decodable frame near this position."));
      break;
    }

    while (!cancelled()) {
      result = avcodec_receive_frame(codecContext, decodedFrame);
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
        break;
      if (result < 0) {
        done = true;
        break;
      }

      const int64_t timestamp = decodedFrame->best_effort_timestamp;
      const double seconds = timestamp == AV_NOPTS_VALUE
                                 ? startSeconds
                                 : timestamp * av_q2d(stream->time_base);
      // Frames between the keyframe and the requested position are decoded but
      // not shown. av_frame_unref() here is what stops the reference-counted
      // frame buffers from accumulating across a long catch-up.
      if (seconds + 0.001 < startSeconds) {
        av_frame_unref(decodedFrame);
        continue;
      }
      if (request.durationMs > 0 &&
          seconds >= startSeconds + request.durationMs / 1000.0) {
        av_frame_unref(decodedFrame);
        done = true;
        break;
      }
      if (!request.singleFrame &&
          seconds - lastOutputSeconds < minimumFrameDelta * 0.90) {
        av_frame_unref(decodedFrame);
        continue;
      }

      AVFrame *sourceFrame = decodedFrame;
      if (decodeState.hardwareFormat != AV_PIX_FMT_NONE &&
          decodedFrame->format == decodeState.hardwareFormat) {
        if (!transferHardwareFrame(softwareFrame, decodedFrame)) {
          av_frame_unref(decodedFrame);
          // A GPU that keeps refusing to hand frames back would otherwise
          // decode forever and show nothing. Abandon this session and run the
          // same request again in software. The decoder cannot be switched in
          // place: its context is still bound to the hardware pixel format.
          if (++hardwareTransferFailures >= kMaxHardwareTransferFailures) {
            retryInSoftware = true;
            done = true;
            break;
          }
          continue;
        }
        hardwareTransferFailures = 0;
        sourceFrame = softwareFrame;
      }

      // A pooled buffer, not a fresh allocation. A null image means the pool is
      // at its ceiling because the UI still holds every frame, so this frame is
      // dropped instead of growing memory.
      QImage image = m_pool->acquire(profile.frameSize);
      if (image.isNull()) {
        m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
        av_frame_unref(decodedFrame);
        continue;
      }

      scaler = sws_getCachedContext(
          scaler, sourceFrame->width, sourceFrame->height,
          static_cast<AVPixelFormat>(sourceFrame->format), image.width(),
          image.height(), AV_PIX_FMT_RGBA, scalerFlags, nullptr, nullptr,
          nullptr);
      if (!scaler) {
        av_frame_unref(decodedFrame);
        continue;
      }
      if (scaler != configuredScaler ||
          sourceFrame->width != configuredSourceWidth ||
          sourceFrame->height != configuredSourceHeight ||
          sourceFrame->format != configuredSourceFormat ||
          int(sourceFrame->colorspace) != configuredColorspace ||
          int(sourceFrame->color_range) != configuredRange) {
        PreviewColor::applyFrameColorspace(scaler, sourceFrame);
        configuredScaler = scaler;
        configuredSourceWidth = sourceFrame->width;
        configuredSourceHeight = sourceFrame->height;
        configuredSourceFormat = sourceFrame->format;
        configuredColorspace = int(sourceFrame->colorspace);
        configuredRange = int(sourceFrame->color_range);
      }
      uint8_t *destination[] = {image.bits()};
      const int destinationStride[] = {int(image.bytesPerLine())};
      const int scaled =
          sws_scale(scaler, sourceFrame->data, sourceFrame->linesize, 0,
                    sourceFrame->height, destination, destinationStride);
      av_frame_unref(decodedFrame);
      if (scaled <= 0)
        continue;

      packetsWithoutOutput = 0;
      if (firstOutputSeconds < 0) {
        firstOutputSeconds = seconds;
        if (!request.singleFrame)
          qCInfo(previewFfmpegLog).nospace()
              << "preview playback: first frame after " << sessionTimer.elapsed()
              << " ms, at source " << qint64(seconds * 1000.0) << " ms"
              << " (requested " << request.sourcePositionMs << " ms)";
      }
      if (!request.singleFrame)
        DecodeCostModel::instance().notePlaybackFrame(
            request.path, double(frameWorkTimer.elapsed()),
            qint64(image.width()) * qint64(image.height()));
      if (request.realtime && !request.singleFrame) {
        const auto target =
            playbackStart +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(seconds - firstOutputSeconds));
        // Pacing sleeps in short slices so a seek or stop is honoured within a
        // couple of milliseconds instead of waiting out the whole frame.
        while (!cancelled() && std::chrono::steady_clock::now() < target)
          QThread::msleep(2);
      }
      publishFrame(std::move(image), request.generation,
                   qint64(seconds * 1000.0));
      // Restarted after the pacing sleep so the next measurement is decode cost
      // only.
      frameWorkTimer.restart();
      lastOutputSeconds = seconds;
      if (request.singleFrame) {
        done = true;
        break;
      }
    }
  }

  finish();

  // The hardware path proved unusable on this machine. Disable it for the whole
  // decoder - not just this session - and run the same request again in
  // software, so the user sees the frame they asked for instead of an empty
  // monitor. m_hardwareDisabled makes sharedHardwareDevice() return null, so the
  // retry cannot recurse a second time.
  if (retryInSoftware && !cancelled()) {
    releaseHardwareDevice();
    m_hardwareDisabled = true;
    m_running.store(true, std::memory_order_release);
    decode(request);
  }
}
