#include "app/preview/audio_peak_window_service.h"

#include "app/media/media_path.h"
#include "app/preview/media_token_registry.h"
#include "app/preview/timeline_tile_cache.h"

#include <QColor>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <memory>

#if defined(CUTPRO_HAS_NATIVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
#endif

AudioPeakWindowService &AudioPeakWindowService::instance() {
  static AudioPeakWindowService service;
  return service;
}

AudioPeakWindowService::AudioPeakWindowService() = default;
AudioPeakWindowService::~AudioPeakWindowService() = default;

QString AudioPeakWindowService::tokenFor(const QString &path) {
  return MediaTokenRegistry::instance().token(path);
}

QString AudioPeakWindowService::pathForToken(const QString &token) const {
  return MediaTokenRegistry::instance().path(token);
}

QString AudioPeakWindowService::fingerprintFor(const QString &path) {
  {
    QMutexLocker locker(&m_mutex);
    const auto found = m_fingerprintByPath.constFind(path);
    if (found != m_fingerprintByPath.constEnd())
      return *found;
  }
  // One stat per source rather than one per window: a pan is dozens of lookups
  // against the same file.
  const QString fingerprint = TimelineTileCache::fingerprintFor(path);
  QMutexLocker locker(&m_mutex);
  if (m_fingerprintByPath.size() > 64)
    m_fingerprintByPath.clear();
  m_fingerprintByPath.insert(path, fingerprint);
  return fingerprint;
}

QString AudioPeakWindowService::cacheKey(const QString &fingerprint,
                                        qint64 startMs, qint64 spanMs,
                                        int columns) const {
  return QStringLiteral("%1|%2|%3|%4")
      .arg(fingerprint)
      .arg(startMs)
      .arg(spanMs)
      .arg(columns);
}

QVector<float> AudioPeakWindowService::lookup(const QString &key) {
  QMutexLocker locker(&m_mutex);
  const auto found = m_windows.find(key);
  if (found == m_windows.end())
    return {};
  found->tick = ++m_tick;
  return found->peaks;
}

void AudioPeakWindowService::store(const QString &key,
                                   const QVector<float> &peaks) {
  if (peaks.isEmpty())
    return;
  QMutexLocker locker(&m_mutex);
  if (m_windows.size() >= kMaximumWindows) {
    // Drop the oldest quarter in one pass. Evicting a single entry per insert
    // would walk the whole table on every window once the cache is full.
    QVector<QPair<quint64, QString>> byAge;
    byAge.reserve(m_windows.size());
    for (auto it = m_windows.cbegin(); it != m_windows.cend(); ++it)
      byAge.append({it.value().tick, it.key()});
    std::sort(byAge.begin(), byAge.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    const int drop = qMax(1, byAge.size() / 4);
    for (int i = 0; i < drop; ++i)
      m_windows.remove(byAge.at(i).second);
  }
  Entry entry;
  entry.peaks = peaks;
  entry.tick = ++m_tick;
  m_windows.insert(key, entry);
}

bool AudioPeakWindowService::knownSilent(const QString &path) const {
  QMutexLocker locker(&m_mutex);
  return m_silentSources.contains(path);
}

void AudioPeakWindowService::rememberSilent(const QString &path) {
  QMutexLocker locker(&m_mutex);
  if (m_silentSources.size() > 64)
    m_silentSources.clear();
  m_silentSources.insert(path);
}

QImage AudioPeakWindowService::render(const QVector<float> &peaks,
                                      const QSize &size) {
  if (peaks.isEmpty())
    return {};
  const int width =
      qBound(16, size.width() > 0 ? size.width() : kRenderWidth, 4096);
  const int height =
      qBound(8, size.height() > 0 ? size.height() : kRenderHeight, 1024);
  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull())
    return {};
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(QRgb(0xff000000u | kWaveformRgb)));
  const double columnWidth = double(width) / double(peaks.size());
  for (int index = 0; index < peaks.size(); ++index) {
    const double peak = qBound(0.0, double(peaks.at(index)), 1.0);
    // At least one pixel: a silent passage should read as a thin baseline
    // rather than as a hole in the clip.
    const int barHeight = qMax(1, int(std::lround(peak * (height - 1))));
    const double left = index * columnWidth;
    const double right = (index + 1) * columnWidth;
    painter.drawRect(QRectF(left, height - barHeight,
                            qMax(1.0, right - left), barHeight));
  }
  painter.end();
  return image;
}

AudioPeakWindowService::Window
AudioPeakWindowService::cachedWindow(const QString &path, qint64 startMs,
                                     qint64 spanMs, int columns) {
  Window result;
  result.startMs = qMax<qint64>(0, startMs);
  result.spanMs = spanMs;
  if (path.isEmpty() || spanMs <= 0)
    return result;
  const int bounded = qBound(32, columns, 2048);
  const QVector<float> peaks = lookup(
      cacheKey(fingerprintFor(path), result.startMs, spanMs, bounded));
  if (peaks.isEmpty())
    return result;
  result.peaks = peaks;
  result.fromCache = true;
  m_hits.fetch_add(1, std::memory_order_relaxed);
  m_serves.fetch_add(1, std::memory_order_relaxed);
  return result;
}

void AudioPeakWindowService::forget(const QString &path) {
  if (path.isEmpty())
    return;
  const QString fingerprint = fingerprintFor(path);
  // Any reader another thread holds open on this file is now stale.
  m_generation.fetch_add(1, std::memory_order_release);
  QMutexLocker locker(&m_mutex);
  const QString prefix = fingerprint + QLatin1Char('|');
  for (auto it = m_windows.begin(); it != m_windows.end();) {
    if (it.key().startsWith(prefix))
      it = m_windows.erase(it);
    else
      ++it;
  }
  m_fingerprintByPath.remove(path);
  m_silentSources.remove(path);
}

void AudioPeakWindowService::clearMemory() {
  QMutexLocker locker(&m_mutex);
  m_windows.clear();
}

QVariantMap AudioPeakWindowService::statistics() const {
  QVariantMap stats;
  stats[QStringLiteral("waveWindowAvailable")] = available();
  stats[QStringLiteral("waveWindowServes")] =
      qulonglong(m_serves.load(std::memory_order_relaxed));
  stats[QStringLiteral("waveWindowDecodes")] =
      qulonglong(m_decodes.load(std::memory_order_relaxed));
  stats[QStringLiteral("waveWindowHits")] =
      qulonglong(m_hits.load(std::memory_order_relaxed));
  stats[QStringLiteral("waveWindowCancels")] =
      qulonglong(m_cancels.load(std::memory_order_relaxed));
  stats[QStringLiteral("waveWindowFailures")] =
      qulonglong(m_failures.load(std::memory_order_relaxed));
  stats[QStringLiteral("waveWindowOpens")] =
      qulonglong(m_opens.load(std::memory_order_relaxed));
  stats[QStringLiteral("waveWindowReaderReuse")] =
      qulonglong(m_reuse.load(std::memory_order_relaxed));
  QMutexLocker locker(&m_mutex);
  stats[QStringLiteral("waveWindowCached")] = m_windows.size();
  stats[QStringLiteral("waveWindowSilentSources")] = m_silentSources.size();
  return stats;
}

#if !defined(CUTPRO_HAS_NATIVE_FFMPEG)

bool AudioPeakWindowService::available() { return false; }

AudioPeakWindowService::Window
AudioPeakWindowService::window(const QString &path, qint64 startMs,
                               qint64 spanMs, int columns,
                               const std::atomic_bool *) {
  Window result;
  result.startMs = qMax<qint64>(0, startMs);
  result.spanMs = spanMs;
  Q_UNUSED(path)
  Q_UNUSED(columns)
  result.error = QStringLiteral("This build has no direct FFmpeg linkage.");
  return result;
}

#else

bool AudioPeakWindowService::available() { return true; }

namespace {

// One audio stream, opened once and kept open on the thread that opened it.
//
// A pan asks for a run of adjacent windows on the same file, so the container is
// opened once per provider thread rather than once per window: on a 6 GB MP4
// avformat_open_input plus find_stream_info is the expensive part and the seeks
// after it are nearly free.
struct AudioWindowReader {
  AVFormatContext *format = nullptr;
  AVCodecContext *codec = nullptr;
  SwrContext *resampler = nullptr;
  AVFrame *frame = nullptr;
  AVPacket *packet = nullptr;
  int streamIndex = -1;
  AVRational timeBase{1, 1000};
  int sampleRate = 48000;
  qint64 durationMs = 0;
  QString path;
  quint64 generation = 0;
  // Re-pointed for every request; the interrupt callback reads it through the
  // reader, so a long open can be aborted by the scroll that superseded it.
  const std::atomic_bool *cancel = nullptr;
  QString error;
  QVector<float> scratch;

  ~AudioWindowReader() { close(); }

  void close() {
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
    resampler = nullptr;
    frame = nullptr;
    packet = nullptr;
    codec = nullptr;
    format = nullptr;
    streamIndex = -1;
  }

  static int interrupt(void *opaque) {
    auto *reader = static_cast<AudioWindowReader *>(opaque);
    return reader && reader->cancel &&
                   reader->cancel->load(std::memory_order_acquire)
               ? 1
               : 0;
  }

  bool open(const QString &source, const std::atomic_bool *cancelToken) {
    path = source;
    cancel = cancelToken;
    format = avformat_alloc_context();
    if (!format) {
      error = QStringLiteral("Out of memory opening the audio stream.");
      return false;
    }
    format->interrupt_callback.callback = interrupt;
    format->interrupt_callback.opaque = this;
    if (avformat_open_input(&format, MediaPath::toFfmpegUrl(source).constData(),
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
    if (stream->duration > 0)
      durationMs = qint64(double(stream->duration) * av_q2d(timeBase) * 1000.0);
    else if (format->duration > 0)
      durationMs = qint64(format->duration) * 1000 / AV_TIME_BASE;

    codec = avcodec_alloc_context3(decoder);
    if (!codec || avcodec_parameters_to_context(codec, stream->codecpar) < 0) {
      error = QStringLiteral("Could not prepare the audio decoder.");
      return false;
    }
    // One thread: several of these run at once and the work per window is a
    // fraction of a second of audio.
    codec->thread_count = 1;
    if (avcodec_open2(codec, decoder, nullptr) < 0) {
      error = QStringLiteral("Could not open the audio decoder.");
      return false;
    }
    sampleRate = codec->sample_rate > 0 ? codec->sample_rate : 48000;

    // Mono float is all an envelope needs; letting swresample do the downmix
    // keeps this independent of the source's channel layout.
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

  bool valid() const { return format && codec && resampler && frame && packet; }

  bool seekTo(qint64 positionMs) {
    const qint64 target =
        qint64(double(qMax<qint64>(0, positionMs)) / 1000.0 / av_q2d(timeBase));
    if (av_seek_frame(format, streamIndex, target, AVSEEK_FLAG_BACKWARD) < 0 &&
        avformat_seek_file(format, streamIndex, INT64_MIN, target, INT64_MAX,
                           0) < 0)
      return false;
    avcodec_flush_buffers(codec);
    return true;
  }

  qint64 frameStartMs() const {
    const qint64 pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                           ? frame->best_effort_timestamp
                           : frame->pts;
    if (pts == AV_NOPTS_VALUE)
      return -1;
    return qint64(double(pts) * av_q2d(timeBase) * 1000.0);
  }

  // Mono float samples of the frame currently held, into `scratch`.
  int convert() {
    const int maximumOut = swr_get_out_samples(resampler, frame->nb_samples) + 256;
    if (maximumOut <= 0)
      return 0;
    if (scratch.size() < maximumOut)
      scratch.resize(maximumOut);
    uint8_t *output[1] = {reinterpret_cast<uint8_t *>(scratch.data())};
    const int converted = swr_convert(
        resampler, output, maximumOut,
        const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
    return qMax(0, converted);
  }
};

// One reader per provider thread. The thumbnail provider's pool is small and
// long-lived, so this amounts to a couple of warm containers for the whole
// session.
thread_local std::unique_ptr<AudioWindowReader> t_reader;

} // namespace

// Gives the service access to the thread-local reader without exposing FFmpeg
// types in the header.
struct AudioWindowReaderAccess {
  static AudioWindowReader *acquire(AudioPeakWindowService &service,
                                    const QString &path,
                                    const std::atomic_bool *cancel,
                                    QString *error) {
    const quint64 generation =
        service.m_generation.load(std::memory_order_acquire);
    if (t_reader && t_reader->valid() && t_reader->path == path &&
        t_reader->generation == generation) {
      t_reader->cancel = cancel;
      service.m_reuse.fetch_add(1, std::memory_order_relaxed);
      return t_reader.get();
    }
    auto reader = std::make_unique<AudioWindowReader>();
    reader->generation = generation;
    if (!reader->open(path, cancel)) {
      if (error)
        *error = reader->error;
      // A file with no audio at all is remembered, so a video-only source does
      // not pay an open per visible window forever.
      if (reader->streamIndex < 0)
        service.rememberSilent(path);
      t_reader.reset();
      return nullptr;
    }
    service.m_opens.fetch_add(1, std::memory_order_relaxed);
    t_reader = std::move(reader);
    return t_reader.get();
  }
};

AudioPeakWindowService::Window
AudioPeakWindowService::window(const QString &path, qint64 startMs,
                               qint64 spanMs, int columns,
                               const std::atomic_bool *cancel) {
  Window result;
  result.startMs = qMax<qint64>(0, startMs);
  result.spanMs = spanMs;
  if (path.isEmpty()) {
    result.error = QStringLiteral("No media path was given.");
    return result;
  }
  if (spanMs <= 0) {
    result.error = QStringLiteral("The window has no duration.");
    return result;
  }
  const int bounded = qBound(32, columns, 2048);
  const auto aborted = [cancel]() {
    return cancel && cancel->load(std::memory_order_acquire);
  };

  const QString fingerprint = fingerprintFor(path);
  const QString key = cacheKey(fingerprint, result.startMs, spanMs, bounded);
  const QVector<float> cached = lookup(key);
  if (!cached.isEmpty()) {
    result.peaks = cached;
    result.fromCache = true;
    m_hits.fetch_add(1, std::memory_order_relaxed);
    m_serves.fetch_add(1, std::memory_order_relaxed);
    return result;
  }
  if (knownSilent(path)) {
    result.error = QStringLiteral("The file has no audio stream.");
    return result;
  }
  if (aborted()) {
    result.cancelled = true;
    m_cancels.fetch_add(1, std::memory_order_relaxed);
    return result;
  }

  QString openError;
  AudioWindowReader *reader =
      AudioWindowReaderAccess::acquire(*this, path, cancel, &openError);
  if (!reader) {
    if (aborted()) {
      result.cancelled = true;
      m_cancels.fetch_add(1, std::memory_order_relaxed);
    } else {
      m_failures.fetch_add(1, std::memory_order_relaxed);
      result.error = openError.isEmpty()
                         ? QStringLiteral("Could not open %1 for waveforms.")
                               .arg(path)
                         : openError;
    }
    return result;
  }

  QElapsedTimer timer;
  timer.start();
  const auto outOfTime = [&]() { return timer.elapsed() > kTimeBudgetMs; };

  QVector<float> peaks(bounded, 0.0f);
  const qint64 endMs = result.startMs + spanMs;
  const bool sampled = spanMs > kFullDecodeLimitMs;
  result.sampled = sampled;
  bool produced = false;

  if (!sampled) {
    // Straight through. Each converted sample is folded into the column its own
    // timestamp falls in, so a transient two pixels wide is not averaged away.
    if (!reader->seekTo(result.startMs)) {
      m_failures.fetch_add(1, std::memory_order_relaxed);
      result.error = QStringLiteral("Could not seek to the window.");
      return result;
    }
    while (!aborted() && !outOfTime()) {
      if (av_read_frame(reader->format, reader->packet) < 0) {
        avcodec_send_packet(reader->codec, nullptr);
      } else if (reader->packet->stream_index != reader->streamIndex) {
        av_packet_unref(reader->packet);
        continue;
      } else {
        const int sent = avcodec_send_packet(reader->codec, reader->packet);
        av_packet_unref(reader->packet);
        if (sent < 0 && sent != AVERROR(EAGAIN))
          break;
      }
      bool past = false;
      bool drained = false;
      while (true) {
        const int received = avcodec_receive_frame(reader->codec, reader->frame);
        if (received == AVERROR(EAGAIN))
          break;
        if (received < 0) {
          drained = true;
          break;
        }
        const qint64 frameMs = reader->frameStartMs();
        const int converted = reader->convert();
        if (frameMs >= 0 && converted > 0) {
          const double msPerSample = 1000.0 / double(reader->sampleRate);
          for (int i = 0; i < converted; ++i) {
            const double sampleMs = double(frameMs) + double(i) * msPerSample;
            if (sampleMs < double(result.startMs))
              continue;
            if (sampleMs >= double(endMs)) {
              past = true;
              break;
            }
            const int column = int(std::clamp<qint64>(
                qint64((sampleMs - double(result.startMs)) * bounded /
                       double(spanMs)),
                0, bounded - 1));
            const float magnitude = std::fabs(reader->scratch.at(i));
            if (magnitude > peaks.at(column))
              peaks[column] = qMin(1.0f, magnitude);
            produced = true;
          }
        }
        av_frame_unref(reader->frame);
        if (past)
          break;
      }
      if (past || drained)
        break;
    }
  } else {
    // Sampled. One seek and one brief probe per column, so a window covering an
    // hour costs the same as one covering a minute.
    const qint64 probe = kProbeWindowMs;
    for (int column = 0; column < bounded; ++column) {
      if (aborted()) {
        result.cancelled = true;
        break;
      }
      if (outOfTime())
        break;
      const qint64 columnStart =
          result.startMs + qint64(double(column) * double(spanMs) /
                                  double(bounded));
      if (reader->durationMs > 0 && columnStart >= reader->durationMs)
        break;
      if (!reader->seekTo(columnStart))
        continue;

      float peak = 0.0f;
      qint64 decodedMs = 0;
      while (decodedMs < probe && !aborted()) {
        if (av_read_frame(reader->format, reader->packet) < 0)
          break;
        if (reader->packet->stream_index != reader->streamIndex) {
          av_packet_unref(reader->packet);
          continue;
        }
        const int sent = avcodec_send_packet(reader->codec, reader->packet);
        av_packet_unref(reader->packet);
        if (sent < 0 && sent != AVERROR(EAGAIN))
          break;
        bool decoded = false;
        while (true) {
          const int received =
              avcodec_receive_frame(reader->codec, reader->frame);
          if (received < 0)
            break;
          const int converted = reader->convert();
          for (int i = 0; i < converted; ++i)
            peak = qMax(peak, std::fabs(reader->scratch.at(i)));
          if (reader->frame->sample_rate > 0)
            decodedMs += qint64(reader->frame->nb_samples) * 1000 /
                         reader->frame->sample_rate;
          decoded = true;
          av_frame_unref(reader->frame);
        }
        if (!decoded && decodedMs == 0 && outOfTime())
          break;
      }
      peaks[column] = qMin(1.0f, peak);
      produced = true;
    }
  }

  if (result.cancelled) {
    m_cancels.fetch_add(1, std::memory_order_relaxed);
    return result;
  }
  if (aborted()) {
    result.cancelled = true;
    m_cancels.fetch_add(1, std::memory_order_relaxed);
    return result;
  }
  if (!produced) {
    // Past the end of the audio, or a window that decoded nothing: cached as
    // silence so scrolling back over it does not decode again.
    store(key, QVector<float>(bounded, 0.0f));
    result.peaks = QVector<float>(bounded, 0.0f);
    m_serves.fetch_add(1, std::memory_order_relaxed);
    return result;
  }

  store(key, peaks);
  result.peaks = peaks;
  m_decodes.fetch_add(1, std::memory_order_relaxed);
  m_serves.fetch_add(1, std::memory_order_relaxed);
  return result;
}

#endif
