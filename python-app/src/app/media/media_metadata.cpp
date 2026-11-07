#include "app/media/media_metadata.h"

#include "app/media/media_path.h"

#include <QFileInfo>

#include <cmath>

#if defined(CUTPRO_HAS_NATIVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}
#endif

QSize MediaMetadata::Info::displaySize() const {
  if (!video.valid || video.width <= 0 || video.height <= 0)
    return {};
  const int rotation = ((video.rotationDegrees % 360) + 360) % 360;
  if (rotation == 90 || rotation == 270)
    return {video.height, video.width};
  return {video.width, video.height};
}

void MediaMetadata::Info::applyTo(QVariantMap *media) const {
  if (!media || !valid)
    return;
  QVariantMap &m = *media;
  if (durationMs > 0)
    m["durationMs"] = durationMs;
  if (sizeBytes > 0)
    m["sizeBytes"] = sizeBytes;
  if (video.valid) {
    // Rotation-corrected: everything downstream (preview scaling, export
    // geometry, the Info panel) wants the size the user sees.
    const QSize display = displaySize();
    m["width"] = display.width();
    m["height"] = display.height();
    m["frameRate"] = video.frameRate;
    m["rotationDegrees"] = video.rotationDegrees;
    m["videoCodec"] = video.codec;
    m["pixelFormat"] = video.pixelFormat;
    m["videoProfile"] = video.profile;
    m["hasBFrames"] = video.hasBFrames;
    if (video.bitRate > 0)
      m["videoBitRate"] = video.bitRate;
  }
  if (audio.valid) {
    m["sampleRate"] = audio.sampleRate;
    m["channels"] = audio.channels;
    m["audioCodec"] = audio.codec;
    m["sampleFormat"] = audio.sampleFormat;
    if (audio.bitRate > 0)
      m["audioBitRate"] = audio.bitRate;
  }
  m["formatName"] = formatName;
  m["bitRate"] = bitRate;
  m["videoStreamCount"] = videoStreams;
  m["audioStreamCount"] = audioStreams;
  m["subtitleStreamCount"] = subtitleStreams;
  m["seekable"] = seekable;
}

#if !defined(CUTPRO_HAS_NATIVE_FFMPEG)

bool MediaMetadata::available() { return false; }

MediaMetadata::Info MediaMetadata::probe(const QString &) {
  Info info;
  info.error = QStringLiteral("This build has no direct FFmpeg linkage.");
  return info;
}

#else

namespace {

double rationalRate(AVRational rate) {
  if (rate.den <= 0 || rate.num <= 0)
    return 0.0;
  const double value = av_q2d(rate);
  // Guard against the absurd values a broken header can carry; a 1000 fps
  // "frame rate" propagated into the pacing loop would spin the decode thread.
  return (value > 0.0 && value < 1000.0) ? value : 0.0;
}

int rotationFromStream(const AVStream *stream) {
  const AVCodecParameters *codec = stream ? stream->codecpar : nullptr;
  if (!codec)
    return 0;
  // FFmpeg 7 removed av_stream_get_side_data(); the display matrix now lives in
  // the codec parameters' coded side data.
  const AVPacketSideData *side = av_packet_side_data_get(
      codec->coded_side_data, codec->nb_coded_side_data,
      AV_PKT_DATA_DISPLAYMATRIX);
  if (!side || side->size < 9 * sizeof(int32_t))
    return 0;
  // av_display_rotation_get returns counter-clockwise degrees; the rest of the
  // app thinks in clockwise, which is also how the containers describe it.
  const double theta = -av_display_rotation_get(
      reinterpret_cast<const int32_t *>(side->data));
  if (std::isnan(theta))
    return 0;
  int degrees = int(std::lround(theta / 90.0)) * 90;
  degrees %= 360;
  if (degrees < 0)
    degrees += 360;
  return degrees;
}

QString codecName(AVCodecID id) {
  const char *name = avcodec_get_name(id);
  return name ? QString::fromUtf8(name) : QString();
}

} // namespace

bool MediaMetadata::available() { return true; }

MediaMetadata::Info MediaMetadata::probe(const QString &path) {
  Info info;
  QString reason;
  if (!MediaPath::isDecodable(path, &reason)) {
    info.error = reason;
    return info;
  }
  info.sizeBytes = QFileInfo(path).size();

  AVFormatContext *format = nullptr;
  AVDictionary *options = nullptr;
  // A header read, not a decode: cap what libav is allowed to consume before it
  // answers. Without these it will happily read tens of megabytes looking for
  // stream parameters on a long MP4, which is exactly the stall the old ffprobe
  // timeout was papering over.
  av_dict_set(&options, "probesize", "8000000", 0);
  av_dict_set(&options, "analyzeduration", "4000000", 0);
  // Header-only sources are the norm here; skipping the byte-scan fallback keeps
  // a damaged file from turning the probe into a full read.
  av_dict_set(&options, "fpsprobesize", "20", 0);

  const int opened = avformat_open_input(
      &format, MediaPath::toFfmpegUrl(path).constData(), nullptr, &options);
  av_dict_free(&options);
  if (opened < 0 || !format) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(opened, buffer, sizeof(buffer));
    info.error = QString::fromUtf8(buffer);
    return info;
  }

  if (avformat_find_stream_info(format, nullptr) < 0) {
    // Not fatal: the container-level duration and the stream list are usually
    // still usable, and a partial answer beats the zeros the killed ffprobe
    // produced.
    info.error = QStringLiteral("Stream details are incomplete.");
  }

  info.formatName =
      format->iformat ? QString::fromUtf8(format->iformat->name) : QString();
  if (format->duration > 0)
    info.durationMs = qint64(format->duration) * 1000 / AV_TIME_BASE;
  info.bitRate = format->bit_rate;
  // AVFMTCTX_UNSEEKABLE is set for pipes and some fragmented streams; the seek
  // functions are also absent when the demuxer cannot do it at all.
  info.seekable = !(format->ctx_flags & AVFMTCTX_UNSEEKABLE) &&
                  (format->pb == nullptr || format->pb->seekable != 0);

  for (unsigned int i = 0; i < format->nb_streams; ++i) {
    const AVStream *stream = format->streams[i];
    const AVCodecParameters *codec = stream ? stream->codecpar : nullptr;
    if (!codec)
      continue;
    switch (codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO: {
      // Cover art is stored as a single-frame video stream; treating it as the
      // video track would report an MP3's album art as the clip's resolution.
      if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC)
        continue;
      ++info.videoStreams;
      if (info.video.valid)
        break;
      info.video.valid = true;
      info.video.width = codec->width;
      info.video.height = codec->height;
      info.video.codec = codecName(codec->codec_id);
      info.video.bitRate = codec->bit_rate;
      info.video.hasBFrames = codec->video_delay > 0;
      const AVPixFmtDescriptor *pixelFormat =
          av_pix_fmt_desc_get(AVPixelFormat(codec->format));
      if (pixelFormat)
        info.video.pixelFormat = QString::fromUtf8(pixelFormat->name);
      const char *profile =
          avcodec_profile_name(codec->codec_id, codec->profile);
      if (profile)
        info.video.profile = QString::fromUtf8(profile);
      info.video.rotationDegrees = rotationFromStream(stream);
      // avg_frame_rate is what the file actually contains; r_frame_rate is the
      // container's nominal rate and is wrong for VFR sources.
      info.video.frameRate = rationalRate(stream->avg_frame_rate);
      if (info.video.frameRate <= 0.0)
        info.video.frameRate = rationalRate(stream->r_frame_rate);
      if (info.durationMs <= 0 && stream->duration > 0)
        info.durationMs =
            qint64(stream->duration * av_q2d(stream->time_base) * 1000.0);
      break;
    }
    case AVMEDIA_TYPE_AUDIO: {
      ++info.audioStreams;
      if (info.audio.valid)
        break;
      info.audio.valid = true;
      info.audio.sampleRate = codec->sample_rate;
      info.audio.channels = codec->ch_layout.nb_channels;
      info.audio.codec = codecName(codec->codec_id);
      info.audio.bitRate = codec->bit_rate;
      const char *sampleFormat =
          av_get_sample_fmt_name(AVSampleFormat(codec->format));
      if (sampleFormat)
        info.audio.sampleFormat = QString::fromUtf8(sampleFormat);
      break;
    }
    case AVMEDIA_TYPE_SUBTITLE:
      ++info.subtitleStreams;
      break;
    default:
      break;
    }
  }

  avformat_close_input(&format);
  info.valid = info.durationMs > 0 || info.video.valid || info.audio.valid;
  if (info.valid)
    info.error.clear();
  else if (info.error.isEmpty())
    info.error = QStringLiteral("No media streams were found.");
  return info;
}

#endif
