#pragma once

#include <QImage>
#include <QList>
#include <QVariantMap>

#include <array>
#include <memory>

// CPU version of the Lumetri chain, for the program monitor.
//
// LumetriPipeline builds an FFmpeg filter string, and that string only exists at
// export time. The monitor draws a decoded still, so everything past a handful
// of tone sliders - curves, colour wheels, HSL secondary, vignette, the input
// LUT - was invisible until the file had been written. This applies the same
// chain to that still, in the same order and from the same numbers, so the panel
// and the picture agree.
//
// Three places are deliberate approximations, each marked at its site: eq's tone
// table is applied per channel instead of to luma alone, the HSL secondary key is
// a real hue/saturation/luma window rather than FFmpeg's six colour flags, and
// hqdn3d's temporal denoise becomes a spatial smooth. HDR tone mapping (zscale +
// tonemap) is not mirrored at all, so an HLG/PQ source previews un-mapped. Export
// is untouched by all of this - LumetriPipeline remains the only thing that
// writes a file.
class LumetriPreview {
public:
  // A parsed .cube, shared between corrections so a LUT is read once.
  struct Lut3d {
    int size = 0;
    QList<float> data; // size^3 * 3, red index fastest, as .cube stores it
  };

  struct Correction {
    bool identity = true;

    std::shared_ptr<const Lut3d> lut; // source clip's input LUT
    bool lutTetrahedral = true;       // project's lutInterpolation

    std::array<quint8, 256> tone{};   // eq brightness/contrast/gamma
    std::array<quint8, 256> curveR{}; // curves master + channel + presets
    std::array<quint8, 256> curveG{};
    std::array<quint8, 256> curveB{};
    bool toneActive = false;
    bool curvesActive = false;

    float saturation = 1.0f; // eq saturation
    float hueDegrees = 0.0f; // hue=h
    float lightness = 0.0f;  // huesaturation intensity (Hue Vs Luma)

    bool balanceActive = false; // colorbalance
    float shadow[3]{};
    float midtone[3]{};
    float highlight[3]{};

    bool hslActive = false; // HSL secondary
    float hslHueCenter = 0.0f;
    float hslHueWidth = 30.0f;
    float hslSatMin = 0.0f;
    float hslSatMax = 1.0f;
    float hslLumaMin = 0.0f;
    float hslLumaMax = 1.0f;
    float hslHue = 0.0f;
    float hslSaturation = 0.0f;
    float hslLuma = 0.0f;

    float sharpen = 0.0f;   // unsharp
    float denoise = 0.0f;   // hqdn3d
    float blurSigma = 0.0f; // gblur

    float vignette = 0.0f; // vignette
    float vignetteAngle = 0.0f;
    float vignetteAspect = 1.0f;
    bool vignetteBackward = false;
  };

  static Correction build(const QVariantMap &clipLumetri,
                         const QVariantMap &mediaColor,
                         const QVariantMap &projectColor);
  static void apply(QImage *image, const Correction &correction);
};
