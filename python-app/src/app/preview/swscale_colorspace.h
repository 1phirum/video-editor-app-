#pragma once

// Colour handling shared by every swscale user in the preview path.
//
// swscale converts YUV with BT.601 coefficients and limited-range input unless
// it is told otherwise, and it is never told otherwise by sws_getCachedContext:
// the pixel format alone carries no colour metadata. Every HD source is BT.709,
// so the default is wrong for almost all of them - greens shift and saturation
// climbs, which is half of why a decoded preview can look worse than the file
// played anywhere else. The frame carries the right answer; this applies it.
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace PreviewColor {

// SWS_CS_* and AVCOL_SPC_* share their numbering by design, so a frame's tag can
// be handed to sws_getCoefficients directly. An untagged frame is assumed to
// follow the convention its resolution implies.
inline int colorspaceOf(const AVFrame *frame) {
  if (!frame)
    return SWS_CS_ITU709;
  int space = frame->colorspace;
  if (space == AVCOL_SPC_UNSPECIFIED || space == AVCOL_SPC_RESERVED)
    space = frame->height >= 720 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
  return space;
}

// Returns false when the scaler has no matrix to set - an RGB or paletted
// source, for instance - which is not an error.
inline bool applyFrameColorspace(SwsContext *scaler, const AVFrame *frame) {
  if (!scaler || !frame)
    return false;
  int *inverse = nullptr;
  int *forward = nullptr;
  int sourceRange = 0;
  int destinationRange = 0;
  int brightness = 0;
  int contrast = 0;
  int saturation = 0;
  if (sws_getColorspaceDetails(scaler, &inverse, &sourceRange, &forward,
                               &destinationRange, &brightness, &contrast,
                               &saturation) < 0)
    return false;
  // RGB output is full range; the source is limited unless it says otherwise.
  const int sourceFullRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
  return sws_setColorspaceDetails(scaler,
                                  sws_getCoefficients(colorspaceOf(frame)),
                                  sourceFullRange,
                                  sws_getCoefficients(SWS_CS_DEFAULT), 1,
                                  brightness, contrast, saturation) >= 0;
}

} // namespace PreviewColor
