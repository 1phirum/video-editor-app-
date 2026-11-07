#pragma once

#include <QImage>
#include <QSize>
#include <QString>

// Draws Premiere-style video scopes from a graded frame. Pure QImage/QPainter, no
// Qt Quick - it lives in cutpro_preview beside the other frame producers, and the
// image provider in main.cpp turns a "image://scope/<type>?rev=N" request into a
// call here. The maths (Rec.709 luma, BT.601 chroma) is documented at each site.
namespace scope {

enum class Type {
  WaveformLuma,
  WaveformRGB,
  RGBParade,
  Vectorscope,
  HistogramLuma,
  HistogramRGB,
};

// Maps the provider id ("waveform-luma", "waveform-rgb", "parade", "vectorscope",
// "histogram-luma", "histogram-rgb") to a Type. Unknown ids fall back to luma
// waveform.
Type typeFromId(const QString &id);

// Renders the scope into an RGBA image of size `out`. `gradedSrc` may be any
// format (read as RGB); a null or tiny source yields the empty graticule so the
// panel still reads as a scope while playback is stopped.
QImage render(const QImage &gradedSrc, Type type, QSize out);

} // namespace scope
