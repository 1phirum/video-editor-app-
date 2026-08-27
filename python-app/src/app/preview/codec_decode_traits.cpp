#include "app/preview/codec_decode_traits.h"

#include <QThread>
#include <QtGlobal>

namespace {

// AVCodecID values, spelled out so this file does not have to include
// libavcodec: the extractor passes the id it already has, and the ids are part
// of FFmpeg's public ABI.
constexpr int kIdMpeg2Video = 2;
constexpr int kIdMpeg4 = 12;
constexpr int kIdMjpeg = 7;
constexpr int kIdH264 = 27;
constexpr int kIdVp8 = 139;
constexpr int kIdProRes = 147;
constexpr int kIdVp9 = 167;
constexpr int kIdHevc = 173;
constexpr int kIdAv1 = 226;

int stillThreadsForFamily(CodecDecodeTraits::Family family) {
  const int ideal = qMax(2, QThread::idealThreadCount());
  switch (family) {
  case CodecDecodeTraits::Family::Av1:
    // dav1d scales almost linearly and is the slowest software decoder here, so
    // a still is worth real threads even at the cost of a frame of latency.
    return qBound(4, ideal / 2, 8);
  case CodecDecodeTraits::Family::Hevc:
  case CodecDecodeTraits::Family::Vp9:
    return qBound(3, ideal / 3, 6);
  default:
    return 2;
  }
}

CodecDecodeTraits::Traits build(CodecDecodeTraits::Family family,
                                const QString &name) {
  CodecDecodeTraits::Traits traits;
  traits.family = family;
  traits.name = name;
  traits.stillThreadCount = stillThreadsForFamily(family);

  switch (family) {
  case CodecDecodeTraits::Family::Av1:
    traits.costWeight = 3.0;
    // The reason this class exists.
    traits.keyframeOnlyDiscardSafe = false;
    traits.frameThreadingForStills = true;
    break;
  case CodecDecodeTraits::Family::Hevc:
    traits.costWeight = 1.8;
    traits.frameThreadingForStills = true;
    break;
  case CodecDecodeTraits::Family::Vp9:
    // A VP9 superframe carries several frames in one packet and show-existing
    // frames refer back to units a keyframe-only filter would remove.
    traits.costWeight = 2.0;
    traits.keyframeOnlyDiscardSafe = false;
    traits.frameThreadingForStills = true;
    break;
  case CodecDecodeTraits::Family::Vp8:
    traits.costWeight = 1.1;
    break;
  case CodecDecodeTraits::Family::H264:
    traits.costWeight = 1.0;
    break;
  case CodecDecodeTraits::Family::Mpeg2:
  case CodecDecodeTraits::Family::Mpeg4:
    traits.costWeight = 0.7;
    break;
  case CodecDecodeTraits::Family::ProRes:
  case CodecDecodeTraits::Family::Dnx:
    traits.costWeight = 0.8;
    traits.allIntra = true;
    break;
  case CodecDecodeTraits::Family::Mjpeg:
    traits.costWeight = 0.5;
    traits.allIntra = true;
    break;
  case CodecDecodeTraits::Family::Raw:
    traits.costWeight = 0.3;
    traits.allIntra = true;
    break;
  case CodecDecodeTraits::Family::Unknown:
    break;
  }
  return traits;
}

} // namespace

CodecDecodeTraits::Traits
CodecDecodeTraits::fromCodecName(const QString &codecName) {
  const QString name = codecName.trimmed().toLower();
  if (name.isEmpty())
    return build(Family::Unknown, name);
  if (name.contains(QStringLiteral("av1")))
    return build(Family::Av1, name);
  if (name.contains(QStringLiteral("hevc")) ||
      name.contains(QStringLiteral("h265")) ||
      name.contains(QStringLiteral("h.265")))
    return build(Family::Hevc, name);
  if (name.contains(QStringLiteral("vp9")))
    return build(Family::Vp9, name);
  if (name.contains(QStringLiteral("vp8")))
    return build(Family::Vp8, name);
  if (name.contains(QStringLiteral("h264")) ||
      name.contains(QStringLiteral("h.264")) ||
      name.contains(QStringLiteral("avc")))
    return build(Family::H264, name);
  if (name.contains(QStringLiteral("prores")))
    return build(Family::ProRes, name);
  if (name.contains(QStringLiteral("dnx")))
    return build(Family::Dnx, name);
  if (name.contains(QStringLiteral("mjpeg")) ||
      name.contains(QStringLiteral("jpeg")))
    return build(Family::Mjpeg, name);
  if (name.contains(QStringLiteral("mpeg2")))
    return build(Family::Mpeg2, name);
  if (name.contains(QStringLiteral("mpeg4")) ||
      name.contains(QStringLiteral("xvid")) ||
      name.contains(QStringLiteral("divx")))
    return build(Family::Mpeg4, name);
  if (name.startsWith(QStringLiteral("rawvideo")) ||
      name.contains(QStringLiteral("v210")))
    return build(Family::Raw, name);
  return build(Family::Unknown, name);
}

CodecDecodeTraits::Traits CodecDecodeTraits::fromCodecId(int avCodecId) {
  switch (avCodecId) {
  case kIdAv1:
    return build(Family::Av1, QStringLiteral("av1"));
  case kIdHevc:
    return build(Family::Hevc, QStringLiteral("hevc"));
  case kIdVp9:
    return build(Family::Vp9, QStringLiteral("vp9"));
  case kIdVp8:
    return build(Family::Vp8, QStringLiteral("vp8"));
  case kIdH264:
    return build(Family::H264, QStringLiteral("h264"));
  case kIdProRes:
    return build(Family::ProRes, QStringLiteral("prores"));
  case kIdMjpeg:
    return build(Family::Mjpeg, QStringLiteral("mjpeg"));
  case kIdMpeg2Video:
    return build(Family::Mpeg2, QStringLiteral("mpeg2video"));
  case kIdMpeg4:
    return build(Family::Mpeg4, QStringLiteral("mpeg4"));
  default:
    return build(Family::Unknown, QString());
  }
}

double CodecDecodeTraits::costWeightForName(const QString &codecName) {
  return fromCodecName(codecName).costWeight;
}

bool CodecDecodeTraits::expensiveName(const QString &codecName) {
  return fromCodecName(codecName).expensive();
}
