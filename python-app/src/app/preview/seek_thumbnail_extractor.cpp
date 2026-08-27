#include "app/preview/seek_thumbnail_extractor.h"

#include "app/media/media_path.h"
#include "app/preview/codec_decode_traits.h"
#include "app/preview/decode_cost_model.h"

#include <QElapsedTimer>
#include <QTransform>

#include <cmath>
#include <limits>
#include <utility>

#if defined(CUTPRO_HAS_NATIVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "app/preview/swscale_colorspace.h"
#endif

#if !defined(CUTPRO_HAS_NATIVE_FFMPEG)

struct SeekThumbnailExtractor::Private {
  QString error = QStringLiteral("This build has no direct FFmpeg linkage.");
};

bool SeekThumbnailExtractor::available() { return false; }
SeekThumbnailExtractor::SeekThumbnailExtractor(const QString &)
    : d(std::make_unique<Private>()) {}
SeekThumbnailExtractor::~SeekThumbnailExtractor() = default;
void SeekThumbnailExtractor::setCancelToken(const std::atomic_bool *) {}
bool SeekThumbnailExtractor::open(const Options &) { return false; }
bool SeekThumbnailExtractor::isOpen() const { return false; }
QString SeekThumbnailExtractor::error() const { return d->error; }
qint64 SeekThumbnailExtractor::durationMs() const { return 0; }
QSize SeekThumbnailExtractor::sourceSize() const { return {}; }
int SeekThumbnailExtractor::rotationDegrees() const { return 0; }
QVector<qint64> SeekThumbnailExtractor::keyframeTimestampsMs() const {
  return {};
}
QImage SeekThumbnailExtractor::frameAt(qint64) { return {}; }
QImage SeekThumbnailExtractor::decodePass(qint64) { return {}; }
QString SeekThumbnailExtractor::codecName() const { return {}; }

#else

namespace {

// A long source's sample table has one entry per frame. Reading them all is a
// memory walk, but the resulting keyframe list still has to stay bounded.
constexpr int kMaximumKeyframes = 200000;

int interruptIo(void *opaque) {
  auto *cancel = static_cast<const std::atomic_bool *>(opaque);
  return cancel && cancel->load(std::memory_order_acquire) ? 1 : 0;
}

QString ffmpegError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(code, buffer, sizeof(buffer));
  return QString::fromUtf8(buffer);
}

// Scale-to-fit that never enlarges: a 160x90 cell taken from a 3840x2160 source
// should be downscaled once, and a 64x64 GIF should stay 64x64 rather than being
// blown up into a blurry cell.
QSize fitted(const QSize &source, const QSize &bound) {
  if (source.isEmpty())
    return {};
  if (bound.isEmpty())
    return source;
  QSize result = source;
  if (result.width() > bound.width() || result.height() > bound.height())
    result = source.scaled(bound, Qt::KeepAspectRatio);
  return {qMax(1, result.width()), qMax(1, result.height())};
}

// Read straight off the already-open stream instead of probing the file a second
// time: on a multi-gigabyte source a second open costs another header parse for
// one integer.
int rotationOf(const AVStream *stream) {
  const AVCodecParameters *codec = stream ? stream->codecpar : nullptr;
  if (!codec)
    return 0;
  const AVPacketSideData *side =
      av_packet_side_data_get(codec->coded_side_data, codec->nb_coded_side_data,
                              AV_PKT_DATA_DISPLAYMATRIX);
  if (!side || side->size < 9 * sizeof(int32_t))
    return 0;
  const double theta =
      -av_display_rotation_get(reinterpret_cast<const int32_t *>(side->data));
  if (std::isnan(theta))
    return 0;
  int degrees = int(std::lround(theta / 90.0)) * 90;
  degrees %= 360;
  if (degrees < 0)
    degrees += 360;
  return degrees;
}

} // namespace

struct SeekThumbnailExtractor::Private {
  QString path;
  Options options;
  const std::atomic_bool *cancel = nullptr;

  AVFormatContext *format = nullptr;
  AVCodecContext *codec = nullptr;
  SwsContext *scaler = nullptr;
  AVFrame *frame = nullptr;
  AVPacket *packet = nullptr;
  int streamIndex = -1;
  AVRational timeBase{1, 1000};
  qint64 durationMs = 0;
  QSize codedSize;
  QSize rotatedSize;
  int rotation = 0;
  QSize scaledSize;
  QString error;
  bool opened = false;
  // Codec-driven decoder setup. Whether non-keyframe packets may be discarded is
  // a property of the codec, not of the request: AV1 and VP9 cannot parse the
  // stream that is left behind.
  CodecDecodeTraits::Traits traits;
  // Set once a keyframe-only attempt has come back empty on this source, so
  // every later request on the same session decodes properly instead of
  // repeating a fast path that does not work here.
  bool keyframeOnlyFailed = false;
  // What the cached scaler was last configured for, so the colour matrix is
  // reapplied only when swscale has actually rebuilt itself underneath it.
  SwsContext *configuredScaler = nullptr;
  int configuredColorspace = -1;
  int configuredRange = -1;

  // Full decode: every packet is fed to the decoder, so the picture at an
  // arbitrary position can be reached from its keyframe.
  void useFullDecode() {
    if (!codec || codec->skip_frame == AVDISCARD_DEFAULT)
      return;
    codec->skip_frame = AVDISCARD_DEFAULT;
    avcodec_flush_buffers(codec);
  }

  bool keyframeOnly() const {
    return codec && codec->skip_frame == AVDISCARD_NONKEY;
  }

  bool cancelled() const {
    return cancel && cancel->load(std::memory_order_acquire);
  }

  ~Private() { close(); }

  void close() {
    if (scaler) {
      sws_freeContext(scaler);
      scaler = nullptr;
    }
    configuredScaler = nullptr;
    configuredColorspace = -1;
    configuredRange = -1;
    keyframeOnlyFailed = false;
    traits = CodecDecodeTraits::Traits();
    if (frame)
      av_frame_free(&frame);
    if (packet)
      av_packet_free(&packet);
    if (codec)
      avcodec_free_context(&codec);
    if (format)
      avformat_close_input(&format);
    opened = false;
  }

  QImage convert(const AVFrame *source);
  QImage rotate(QImage image) const;
};

QImage SeekThumbnailExtractor::Private::convert(const AVFrame *source) {
  if (!source || source->width <= 0 || source->height <= 0)
    return {};
  const QSize target = scaledSize.isEmpty()
                           ? fitted(QSize(source->width, source->height),
                                    options.maximumFrameSize)
                           : scaledSize;
  if (target.isEmpty())
    return {};

  // RGB888 keeps the swscale output directly usable as a QImage and as JPEG
  // input, with no alpha channel to carry through the filmstrip composition.
  // Bicubic rather than bilinear: a still is resampled once and then looked at,
  // and on the monitor path this is the picture the user judges the file by. A
  // frame already at its target size is only format-converted, so the filter
  // costs nothing there.
  const bool sameSize =
      target == QSize(source->width, source->height);
  scaler = sws_getCachedContext(
      scaler, source->width, source->height, AVPixelFormat(source->format),
      target.width(), target.height(), AV_PIX_FMT_RGB24,
      sameSize ? SWS_POINT : SWS_BICUBIC, nullptr, nullptr, nullptr);
  if (!scaler) {
    error = QStringLiteral("Could not create the thumbnail scaler.");
    return {};
  }
  if (scaler != configuredScaler ||
      int(source->colorspace) != configuredColorspace ||
      int(source->color_range) != configuredRange) {
    PreviewColor::applyFrameColorspace(scaler, source);
    configuredScaler = scaler;
    configuredColorspace = int(source->colorspace);
    configuredRange = int(source->color_range);
  }

  QImage image(target, QImage::Format_RGB888);
  if (image.isNull())
    return {};
  uint8_t *planes[4] = {image.bits(), nullptr, nullptr, nullptr};
  int strides[4] = {int(image.bytesPerLine()), 0, 0, 0};
  if (sws_scale(scaler, source->data, source->linesize, 0, source->height,
                planes, strides) <= 0) {
    error = QStringLiteral("Could not scale the thumbnail.");
    return {};
  }
  return rotate(std::move(image));
}

QImage SeekThumbnailExtractor::Private::rotate(QImage image) const {
  if (rotation == 0 || image.isNull())
    return image;
  QTransform transform;
  transform.rotate(rotation);
  return image.transformed(transform, Qt::SmoothTransformation);
}

bool SeekThumbnailExtractor::available() { return true; }

SeekThumbnailExtractor::SeekThumbnailExtractor(const QString &path)
    : d(std::make_unique<Private>()) {
  d->path = path;
}

SeekThumbnailExtractor::~SeekThumbnailExtractor() = default;

void SeekThumbnailExtractor::setCancelToken(const std::atomic_bool *cancel) {
  d->cancel = cancel;
  if (d->format)
    d->format->interrupt_callback.opaque = const_cast<std::atomic_bool *>(cancel);
}

bool SeekThumbnailExtractor::isOpen() const { return d->opened; }

QString SeekThumbnailExtractor::error() const { return d->error; }

qint64 SeekThumbnailExtractor::durationMs() const { return d->durationMs; }

QSize SeekThumbnailExtractor::sourceSize() const { return d->rotatedSize; }

int SeekThumbnailExtractor::rotationDegrees() const { return d->rotation; }

QVector<qint64> SeekThumbnailExtractor::keyframeTimestampsMs() const {
  QVector<qint64> keyframes;
  if (!d->opened || !d->format || d->streamIndex < 0)
    return keyframes;
  AVStream *stream = d->format->streams[d->streamIndex];
  const int entries = avformat_index_get_entries_count(stream);
  if (entries <= 0)
    return keyframes;
  keyframes.reserve(qMin(entries, kMaximumKeyframes));
  const double toMs = av_q2d(d->timeBase) * 1000.0;
  for (int i = 0; i < entries && keyframes.size() < kMaximumKeyframes; ++i) {
    const AVIndexEntry *entry = avformat_index_get_entry(stream, i);
    if (!entry || !(entry->flags & AVINDEX_KEYFRAME))
      continue;
    // Converted the same way frameAt() converts a seek target, so a snapped
    // position and the seek it produces cannot disagree.
    const qint64 ms = qint64(double(entry->timestamp) * toMs);
    if (ms >= 0)
      keyframes.append(ms);
  }
  return keyframes;
}

bool SeekThumbnailExtractor::open(const Options &options) {
  d->close();
  d->options = options;
  d->error.clear();

  QString reason;
  if (!MediaPath::isDecodable(d->path, &reason)) {
    d->error = reason;
    return false;
  }

  d->format = avformat_alloc_context();
  if (!d->format) {
    d->error = QStringLiteral("Out of memory opening the media file.");
    return false;
  }
  // Registered before the open so a cancelled build aborts even while libav is
  // still reading the header off a slow or network disk.
  d->format->interrupt_callback.callback = interruptIo;
  d->format->interrupt_callback.opaque =
      const_cast<std::atomic_bool *>(d->cancel);

  AVDictionary *open_options = nullptr;
  av_dict_set(&open_options, "probesize", "4000000", 0);
  av_dict_set(&open_options, "analyzeduration", "2000000", 0);
  const int opened = avformat_open_input(
      &d->format, MediaPath::toFfmpegUrl(d->path).constData(), nullptr,
      &open_options);
  av_dict_free(&open_options);
  if (opened < 0) {
    d->error = ffmpegError(opened);
    d->format = nullptr;
    return false;
  }
  if (avformat_find_stream_info(d->format, nullptr) < 0) {
    d->error = QStringLiteral("Could not read the stream details.");
    d->close();
    return false;
  }

  const AVCodec *decoder = nullptr;
  d->streamIndex = av_find_best_stream(d->format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                       &decoder, 0);
  if (d->streamIndex < 0 || !decoder) {
    d->error = QStringLiteral("The file has no video stream to preview.");
    d->close();
    return false;
  }

  AVStream *stream = d->format->streams[d->streamIndex];
  d->timeBase = stream->time_base;
  if (d->format->duration > 0)
    d->durationMs = qint64(d->format->duration) * 1000 / AV_TIME_BASE;
  else if (stream->duration > 0)
    d->durationMs = qint64(stream->duration * av_q2d(stream->time_base) * 1000.0);

  d->codec = avcodec_alloc_context3(decoder);
  if (!d->codec ||
      avcodec_parameters_to_context(d->codec, stream->codecpar) < 0) {
    d->error = QStringLiteral("Could not prepare the video decoder.");
    d->close();
    return false;
  }
  // Two threads is the sweet spot for single-still work on H.264: frame
  // threading adds latency proportional to the thread count because the decoder
  // buffers that many frames before returning the first one. A software AV1 or
  // HEVC decoder is slow enough that the trade goes the other way, so the count
  // and the threading type both come from the codec.
  d->traits = CodecDecodeTraits::fromCodecName(
      QString::fromLatin1(avcodec_get_name(stream->codecpar->codec_id)));
  DecodeCostModel::instance().noteSource(
      d->path, d->traits.name, stream->codecpar->width,
      stream->codecpar->height,
      stream->avg_frame_rate.den > 0 ? av_q2d(stream->avg_frame_rate) : 0.0);
  d->codec->thread_count = d->traits.stillThreadCount;
  d->codec->thread_type = d->traits.frameThreadingForStills
                              ? (FF_THREAD_FRAME | FF_THREAD_SLICE)
                              : FF_THREAD_SLICE;
  // The decisive optimisation for the codecs that allow it. AVDISCARD_NONKEY
  // makes the decoder throw away every non-keyframe packet without decoding it,
  // so a still costs exactly one frame of work no matter how long the GOP is.
  //
  // It cannot be used on every codec. An AV1 packet is a temporal unit of OBUs
  // and a VP9 packet can be a superframe; the packets a container flags as
  // keyframes are not a decodable stream on their own once the others are gone,
  // and dav1d answers with "Error parsing OBU data" and no frame at all - which
  // is what made every filmstrip cell of the AV1 test source fail, repeatedly
  // and at full CPU. Those codecs decode from the keyframe instead.
  const bool mayDiscard =
      !options.exactSeek && d->traits.keyframeOnlyDiscardSafe;
  d->codec->skip_frame = mayDiscard ? AVDISCARD_NONKEY : AVDISCARD_DEFAULT;
  d->codec->skip_loop_filter = AVDISCARD_NONREF;
  d->codec->flags2 |= AV_CODEC_FLAG2_FAST;
  if (avcodec_open2(d->codec, decoder, nullptr) < 0) {
    d->error = QStringLiteral("Could not open the video decoder.");
    d->close();
    return false;
  }

  d->frame = av_frame_alloc();
  d->packet = av_packet_alloc();
  if (!d->frame || !d->packet) {
    d->error = QStringLiteral("Out of memory preparing the decoder.");
    d->close();
    return false;
  }

  d->codedSize = QSize(d->codec->width, d->codec->height);
  d->rotation = rotationOf(stream);
  d->rotatedSize = (d->rotation == 90 || d->rotation == 270)
                       ? QSize(d->codedSize.height(), d->codedSize.width())
                       : d->codedSize;
  // The bound applies to what the user sees, so it is computed on the rotated
  // size and then swapped back for swscale, which works on the coded frame.
  const QSize fittedRotated = fitted(d->rotatedSize, options.maximumFrameSize);
  d->scaledSize = (d->rotation == 90 || d->rotation == 270)
                      ? QSize(fittedRotated.height(), fittedRotated.width())
                      : fittedRotated;
  d->opened = true;
  return true;
}

QImage SeekThumbnailExtractor::decodePass(qint64 positionMs) {
  if (!d->opened) {
    if (d->error.isEmpty())
      d->error = QStringLiteral("The extractor is not open.");
    return {};
  }
  if (d->cancelled())
    return {};

  QElapsedTimer timer;
  timer.start();
  const auto outOfTime = [&]() {
    return d->options.frameTimeBudgetMs > 0 &&
           timer.elapsed() > d->options.frameTimeBudgetMs;
  };

  const qint64 clamped =
      d->durationMs > 0 ? qBound<qint64>(0, positionMs, d->durationMs) : qMax<qint64>(0, positionMs);
  const qint64 targetTs =
      qint64(double(clamped) / 1000.0 / av_q2d(d->timeBase));

  // Seeking to timestamp 0 is what libav answers with "Cannot find an index
  // entry before timestamp: 0" on a file whose first edit list entry starts
  // late. There is nothing to seek to at the head of the file, so read from
  // where it already is instead.
  if (clamped > 0) {
    const int seeked = av_seek_frame(d->format, d->streamIndex, targetTs,
                                    AVSEEK_FLAG_BACKWARD);
    if (seeked < 0) {
      // A missing index is recoverable: fall back to the start and read forward,
      // which is slow but correct, rather than reporting no thumbnail at all.
      if (av_seek_frame(d->format, d->streamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0)
        avformat_seek_file(d->format, d->streamIndex, INT64_MIN, 0, INT64_MAX, 0);
    }
  }
  avcodec_flush_buffers(d->codec);

  QImage best;
  qint64 bestDelta = std::numeric_limits<qint64>::max();
  int decoded = 0;

  while (!d->cancelled() && !outOfTime()) {
    const int read = av_read_frame(d->format, d->packet);
    if (read < 0) {
      // Flush the decoder: the frame we want can still be sitting in it at EOF.
      avcodec_send_packet(d->codec, nullptr);
    } else if (d->packet->stream_index != d->streamIndex) {
      av_packet_unref(d->packet);
      continue;
    } else {
      const int sent = avcodec_send_packet(d->codec, d->packet);
      av_packet_unref(d->packet);
      if (sent < 0 && sent != AVERROR(EAGAIN))
        break;
    }

    bool drained = false;
    while (!drained) {
      const int received = avcodec_receive_frame(d->codec, d->frame);
      if (received == AVERROR(EAGAIN)) {
        break;
      }
      if (received < 0) {
        drained = true;
        break;
      }
      ++decoded;
      const qint64 bestEffort = d->frame->best_effort_timestamp != AV_NOPTS_VALUE
                                    ? d->frame->best_effort_timestamp
                                    : d->frame->pts;
      const qint64 delta = bestEffort == AV_NOPTS_VALUE
                               ? 0
                               : qAbs(bestEffort - targetTs);
      if (delta <= bestDelta) {
        QImage candidate = d->convert(d->frame);
        if (!candidate.isNull()) {
          best = std::move(candidate);
          bestDelta = delta;
        }
      }
      av_frame_unref(d->frame);

      // Keyframe mode: the first frame after a backward seek is the one that was
      // asked for, so stop rather than decoding the rest of the file.
      if (!d->options.exactSeek && !best.isNull())
        return best;
      // Exact mode: stop once the decoder has reached or passed the target.
      if (d->options.exactSeek && bestEffort != AV_NOPTS_VALUE &&
          bestEffort >= targetTs && !best.isNull())
        return best;
      if (decoded >= qMax(1, d->options.maximumDecodeFrames))
        return best;
    }

    if (read < 0)
      break;
  }

  if (best.isNull() && d->error.isEmpty())
    d->error = d->cancelled() ? QStringLiteral("Cancelled.")
                              : QStringLiteral("No frame could be decoded.");
  return best;
}

QImage SeekThumbnailExtractor::frameAt(qint64 positionMs) {
  QElapsedTimer timer;
  timer.start();
  QImage frame = decodePass(positionMs);

  // The traits table is a guess about a codec, and a guess can be wrong on a
  // particular file. If the cheap keyframe-only path returned nothing, drop it
  // for the rest of this session and ask once more properly, rather than letting
  // every later tile of the same source fail the same way.
  if (frame.isNull() && !d->cancelled() && d->keyframeOnly()) {
    d->keyframeOnlyFailed = true;
    d->useFullDecode();
    d->error.clear();
    frame = decodePass(positionMs);
  }

  DecodeCostModel::instance().noteStillDecode(d->path, double(timer.elapsed()),
                                              !frame.isNull());
  return frame;
}

QString SeekThumbnailExtractor::codecName() const { return d->traits.name; }

#endif
