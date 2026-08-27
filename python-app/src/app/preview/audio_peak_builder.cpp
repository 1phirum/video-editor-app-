#include "app/preview/audio_peak_builder.h"

#include "app/media/media_path.h"
#include "app/preview/preview_cache.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QVector>

#include <algorithm>
#include <cmath>

#if defined(CUTPRO_HAS_NATIVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#endif

namespace {

// Bumped when the rendering or the sampling rule changes.
constexpr auto kVariant = "waveform-peaks-v1";

QString variantFor(const AudioPeakBuilder::Options &options, bool sampled) {
  return QStringLiteral("%1-%2x%3-%4%5")
      .arg(QString::fromLatin1(kVariant))
      .arg(options.imageWidth)
      .arg(options.imageHeight)
      .arg(options.columns)
      .arg(sampled ? QStringLiteral("-s") : QString());
}

// Bottom-anchored lobe: silence sits on the baseline and peaks grow upward, which
// is the shape the timeline already lays out under each clip.
QString render(const QVector<float> &peaks,
               const AudioPeakBuilder::Options &options,
               const QString &outputPath) {
  if (peaks.isEmpty())
    return {};
  const int width = qMax(16, options.imageWidth);
  const int height = qMax(8, options.imageHeight);
  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull())
    return {};
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(QRgb(0xff000000u | options.colorRgb)));

  const double columnWidth = double(width) / double(peaks.size());
  for (int index = 0; index < peaks.size(); ++index) {
    const double peak = qBound(0.0, double(peaks.at(index)), 1.0);
    // Always at least one pixel: a silent passage should read as a thin line
    // rather than as a hole in the clip.
    const int barHeight = qMax(1, int(std::lround(peak * (height - 1))));
    const double left = index * columnWidth;
    const double right = (index + 1) * columnWidth;
    painter.drawRect(QRectF(left, height - barHeight,
                            qMax(1.0, right - left), barHeight));
  }
  painter.end();

  if (!image.save(outputPath, "PNG")) {
    QFile::remove(outputPath);
    return {};
  }
  return PreviewCache::toUrl(outputPath);
}

} // namespace

namespace {
// Shared by build() and cached() so a cache entry is never looked up under a
// different variant than the one that wrote it.
bool usesSampledStrategy(qint64 durationMs,
                         const AudioPeakBuilder::Options &options) {
  return durationMs > 0 &&
         durationMs > qMax<qint64>(1, options.fullDecodeLimitMs);
}
} // namespace

AudioPeakBuilder::Result AudioPeakBuilder::cached(const QString &path,
                                                 qint64 durationMs,
                                                 const Options &options) {
  Result result;
  if (path.isEmpty())
    return result;
  const bool sampled = usesSampledStrategy(durationMs, options);
  const QString hit = PreviewCache::lookup(path, variantFor(options, sampled),
                                           QStringLiteral("png"));
  if (hit.isEmpty())
    return result;
  result.url = PreviewCache::toUrl(hit);
  result.columns = qBound(64, options.columns, 4096);
  result.fromCache = true;
  result.sampled = sampled;
  return result;
}

#if !defined(CUTPRO_HAS_NATIVE_FFMPEG)

bool AudioPeakBuilder::available() { return false; }

AudioPeakBuilder::Result AudioPeakBuilder::build(const QString &, qint64,
                                                 const Options &,
                                                 const std::atomic_bool *) {
  Result result;
  result.error = QStringLiteral("This build has no direct FFmpeg linkage.");
  return result;
}

#else

namespace {

int interruptIo(void *opaque) {
  auto *cancel = static_cast<const std::atomic_bool *>(opaque);
  return cancel && cancel->load(std::memory_order_acquire) ? 1 : 0;
}

// Everything needed to pull mono float samples out of one audio stream, torn down
// in one place so the many early returns below cannot leak a context.
struct AudioReader {
  AVFormatContext *format = nullptr;
  AVCodecContext *codec = nullptr;
  SwrContext *resampler = nullptr;
  AVFrame *frame = nullptr;
  AVPacket *packet = nullptr;
  int streamIndex = -1;
  AVRational timeBase{1, 1000};
  int sampleRate = 0;
  QString error;

  ~AudioReader() {
    if (resampler)
      swr_free(&resampler);
    if (frame)
      av_frame_free(&frame);
    if (packet)
      av_packet_free(&packet);
    if (codec)
      avcodec_free_context(&codec);
    if (format)
      avformat_close_input(&format);
  }

  bool open(const QString &path, const std::atomic_bool *cancel) {
    format = avformat_alloc_context();
    if (!format) {
      error = QStringLiteral("Out of memory opening the audio stream.");
      return false;
    }
    format->interrupt_callback.callback = interruptIo;
    format->interrupt_callback.opaque = const_cast<std::atomic_bool *>(cancel);
    if (avformat_open_input(&format, MediaPath::toFfmpegUrl(path).constData(),
                            nullptr, nullptr) < 0) {
      format = nullptr;
      error = QStringLiteral("Could not open the media file.");
      return false;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
      error = QStringLiteral("Could not read the stream details.");
      return false;
    }
    const AVCodec *decoder = nullptr;
    streamIndex =
        av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (streamIndex < 0 || !decoder) {
      error = QStringLiteral("The file has no audio stream.");
      return false;
    }
    AVStream *stream = format->streams[streamIndex];
    timeBase = stream->time_base;
    codec = avcodec_alloc_context3(decoder);
    if (!codec || avcodec_parameters_to_context(codec, stream->codecpar) < 0) {
      error = QStringLiteral("Could not prepare the audio decoder.");
      return false;
    }
    codec->thread_count = 1;
    if (avcodec_open2(codec, decoder, nullptr) < 0) {
      error = QStringLiteral("Could not open the audio decoder.");
      return false;
    }
    sampleRate = codec->sample_rate > 0 ? codec->sample_rate : 48000;

    // Mono float is the only shape the envelope needs; letting swresample do the
    // downmix keeps this code independent of the source's channel layout.
    AVChannelLayout mono;
    av_channel_layout_default(&mono, 1);
    const int prepared = swr_alloc_set_opts2(
        &resampler, &mono, AV_SAMPLE_FMT_FLT, sampleRate, &codec->ch_layout,
        codec->sample_fmt, codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&mono);
    if (prepared < 0 || !resampler || swr_init(resampler) < 0) {
      error = QStringLiteral("Could not prepare the audio resampler.");
      return false;
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet) {
      error = QStringLiteral("Out of memory preparing the audio decoder.");
      return false;
    }
    return true;
  }

  // Peak magnitude of the resampled frame currently held in `frame`.
  float peakOfFrame() {
    const int maximumOut =
        swr_get_out_samples(resampler, frame->nb_samples) + 256;
    if (maximumOut <= 0)
      return 0.0f;
    QVector<float> buffer(maximumOut, 0.0f);
    uint8_t *output[1] = {reinterpret_cast<uint8_t *>(buffer.data())};
    const int converted = swr_convert(
        resampler, output, maximumOut,
        const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
    float peak = 0.0f;
    for (int i = 0; i < converted; ++i)
      peak = qMax(peak, std::fabs(buffer.at(i)));
    return qMin(1.0f, peak);
  }
};

} // namespace

bool AudioPeakBuilder::available() { return true; }

AudioPeakBuilder::Result AudioPeakBuilder::build(const QString &path,
                                                 qint64 durationMs,
                                                 const Options &options,
                                                 const std::atomic_bool *cancel) {
  Result result;
  if (path.isEmpty()) {
    result.error = QStringLiteral("No media path was given.");
    return result;
  }
  QString reason;
  if (!MediaPath::isDecodable(path, &reason)) {
    result.error = reason;
    return result;
  }

  const int columns = qBound(64, options.columns, 4096);
  const bool sampled = usesSampledStrategy(durationMs, options);
  const QString variant = variantFor(options, sampled);

  const QString cached =
      PreviewCache::lookup(path, variant, QStringLiteral("png"));
  if (!cached.isEmpty()) {
    result.url = PreviewCache::toUrl(cached);
    result.columns = columns;
    result.fromCache = true;
    result.sampled = sampled;
    return result;
  }

  AudioReader reader;
  if (!reader.open(path, cancel)) {
    result.error = reader.error;
    return result;
  }

  const auto stopping = [cancel]() {
    return cancel && cancel->load(std::memory_order_acquire);
  };
  QElapsedTimer timer;
  timer.start();
  const auto outOfTime = [&]() {
    return options.timeBudgetMs > 0 && timer.elapsed() > options.timeBudgetMs;
  };

  QVector<float> peaks(columns, 0.0f);
  const qint64 span = durationMs > 0
                          ? durationMs
                          : (reader.format->duration > 0
                                 ? qint64(reader.format->duration) * 1000 /
                                       AV_TIME_BASE
                                 : 0);
  bool produced = false;

  if (!sampled) {
    // Straight through. Every decoded frame is folded into the column its
    // timestamp falls in, so the envelope is exact.
    while (!stopping() && !outOfTime()) {
      const int read = av_read_frame(reader.format, reader.packet);
      if (read < 0) {
        avcodec_send_packet(reader.codec, nullptr);
      } else if (reader.packet->stream_index != reader.streamIndex) {
        av_packet_unref(reader.packet);
        continue;
      } else {
        const int sent = avcodec_send_packet(reader.codec, reader.packet);
        av_packet_unref(reader.packet);
        if (sent < 0 && sent != AVERROR(EAGAIN))
          break;
      }
      bool eof = false;
      while (true) {
        const int received = avcodec_receive_frame(reader.codec, reader.frame);
        if (received == AVERROR(EAGAIN))
          break;
        if (received < 0) {
          eof = true;
          break;
        }
        const qint64 pts = reader.frame->best_effort_timestamp != AV_NOPTS_VALUE
                               ? reader.frame->best_effort_timestamp
                               : reader.frame->pts;
        const qint64 positionMs =
            pts == AV_NOPTS_VALUE
                ? 0
                : qint64(double(pts) * av_q2d(reader.timeBase) * 1000.0);
        const int column =
            span > 0 ? int(std::clamp<qint64>(positionMs * columns / span, 0,
                                             columns - 1))
                     : 0;
        peaks[column] = qMax(peaks.at(column), reader.peakOfFrame());
        produced = true;
        av_frame_unref(reader.frame);
      }
      if (read < 0 || eof)
        break;
    }
  } else {
    // Sampled. One seek and one short decode window per column: bounded by the
    // image width, not by the length of the source.
    const qint64 window = qMax<qint64>(20, options.windowMs);
    for (int column = 0; column < columns; ++column) {
      if (stopping()) {
        result.cancelled = true;
        break;
      }
      if (outOfTime())
        break;
      const qint64 startMs = span > 0 ? qint64(double(column) * double(span) /
                                               double(columns))
                                      : 0;
      const qint64 targetTs =
          qint64(double(startMs) / 1000.0 / av_q2d(reader.timeBase));
      if (startMs > 0 &&
          av_seek_frame(reader.format, reader.streamIndex, targetTs,
                        AVSEEK_FLAG_BACKWARD) < 0)
        continue;
      avcodec_flush_buffers(reader.codec);

      float peak = 0.0f;
      qint64 decodedMs = 0;
      while (decodedMs < window && !stopping()) {
        if (av_read_frame(reader.format, reader.packet) < 0)
          break;
        if (reader.packet->stream_index != reader.streamIndex) {
          av_packet_unref(reader.packet);
          continue;
        }
        const int sent = avcodec_send_packet(reader.codec, reader.packet);
        av_packet_unref(reader.packet);
        if (sent < 0 && sent != AVERROR(EAGAIN))
          break;
        while (true) {
          const int received = avcodec_receive_frame(reader.codec, reader.frame);
          if (received < 0)
            break;
          peak = qMax(peak, reader.peakOfFrame());
          if (reader.frame->sample_rate > 0)
            decodedMs += qint64(reader.frame->nb_samples) * 1000 /
                         reader.frame->sample_rate;
          av_frame_unref(reader.frame);
        }
      }
      peaks[column] = peak;
      produced = true;
    }
  }

  if (result.cancelled)
    return result;
  if (!produced) {
    result.error = reader.error.isEmpty()
                       ? QStringLiteral("No audio could be decoded.")
                       : reader.error;
    return result;
  }

  const QString output =
      PreviewCache::reserve(path, variant, QStringLiteral("png"));
  if (output.isEmpty()) {
    result.error = QStringLiteral("Could not write to the preview cache.");
    return result;
  }
  result.url = render(peaks, options, output);
  if (result.url.isEmpty()) {
    result.error = QStringLiteral("Could not save the waveform.");
    return result;
  }
  result.columns = columns;
  result.sampled = sampled;
  return result;
}

#endif
