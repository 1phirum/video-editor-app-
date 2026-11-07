#pragma once

#include <QString>

// What a codec costs to decode, and what may be skipped while decoding it.
//
// The preview paths used to treat every source the same: two slice threads and
// AVDISCARD_NONKEY, which throws away every packet that is not flagged as a
// keyframe so a still costs one frame of work regardless of GOP length. That is
// correct for H.264 and it is why scrubbing a long MP4 is fast.
//
// It is wrong for AV1. An AV1 packet is a temporal unit made of OBUs, and the
// frames a container flags as keyframes are not a self-contained stream once the
// intermediate units are dropped: dav1d then reports "Error parsing OBU data"
// and returns nothing, which is exactly the repeated
// "No frame could be decoded." the filmstrip logged for every visible cell. Each
// of those failures still cost a seek, a read and the whole per-tile time budget,
// on two provider threads, over a multi-gigabyte file - the CPU and disk storm
// behind the freeze.
//
// So the discard policy is a property of the codec, decided here once, rather
// than a constant compiled into the extractor. The cost weights feed
// DecodeCostModel, which is what keeps preview resolution honest on a codec that
// software-decodes several times slower than H.264 at the same frame size.
class CodecDecodeTraits final {
public:
  enum class Family {
    Unknown,
    H264,
    Hevc,
    Av1,
    Vp9,
    Vp8,
    Mpeg2,
    Mpeg4,
    ProRes,
    Dnx,
    Mjpeg,
    Raw,
  };

  struct Traits {
    Family family = Family::Unknown;
    QString name;
    // Decode cost of one pixel relative to H.264 at the same size, software.
    double costWeight = 1.0;
    // False when dropping non-keyframe packets leaves the decoder unable to
    // parse the ones that are kept.
    bool keyframeOnlyDiscardSafe = true;
    // Frame threading buffers one frame per thread before the first one comes
    // out, which a single still pays in full - but dav1d is so much faster with
    // threads that the trade is worth it there.
    bool frameThreadingForStills = false;
    // Every frame is a keyframe, so seeking never needs a catch-up decode.
    bool allIntra = false;
    int stillThreadCount = 2;

    bool expensive() const { return costWeight >= 1.75; }
  };

  // Canonical FFmpeg codec names ("av1", "hevc", "h264", ...). Empty or unknown
  // names get the neutral H.264-like profile.
  static Traits fromCodecName(const QString &codecName);
  // AVCodecID as an int, so this header stays usable in translation units that
  // do not include libavcodec.
  static Traits fromCodecId(int avCodecId);

  static double costWeightForName(const QString &codecName);
  static bool expensiveName(const QString &codecName);
};
