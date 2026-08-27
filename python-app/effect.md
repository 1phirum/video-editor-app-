# Premiere Pro Effects — Complete Mapping

> This document maps every standard Adobe Premiere Pro effect category to our app's current implementation status.
> - ✅ = Implemented in our app
> - 🔶 = Partially implemented
> - ❌ = Not yet implemented
> - ⬜ = Low priority / Premiere-specific (may skip)

---

## Summary

| Area | Premiere Pro Total | Our App ✅ | Partial 🔶 | Missing ❌ |
|------|-------------------|-----------|------------|-----------|
| Video Effects | ~65+ | 14 | 2 | ~50 |
| Audio Effects | ~35+ | 14 | 1 | ~20 |
| Lumetri Color | 1 (complex) | 1 | — | — |
| Clip Properties | ~15 | 15 | — | — |

---

## 1. VIDEO EFFECTS

### 1.1 Adjust

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Brightness & Contrast | `brightness_contrast` | ✅ | `eq=brightness:contrast:saturation` | Also includes saturation |
| Levels | — | ❌ | `curves` or `colorlevels` | Input/output black & white points |
| ProcAmp | — | ❌ | `eq` | Broadcast-style controls |
| Lighting Effects | — | ❌ | `vignette` + overlay compositing | Complex spotlight simulation |
| Auto Levels | — | ❌ | `normalize` / `colorlevels` | Auto black/white point |
| Auto Contrast | — | ❌ | `normalize` | Auto contrast stretch |
| Auto Color | — | ❌ | `normalize` + `colorbalance` | Auto white balance |
| Shadow/Highlight | — | 🔶 | `eq=gamma` | Partial via Lumetri shadows/highlights |
| Extract | — | ❌ | `lutrgb` | Grayscale extraction with controls |

### 1.2 Blur & Sharpen

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Gaussian Blur | `gaussian_blur` | ✅ | `gblur=sigma` | |
| Box Blur (Fast Blur) | `box_blur` | ✅ | `boxblur` | |
| Custom Blur (Mask) | `custom_blur` | ✅ | `gblur` + mask compositing | Pen-drawn region blur |
| Sharpen | `sharpen` | ✅ | `unsharp` | |
| Unsharp Mask | — | ❌ | `unsharp=lx:ly:la:cx:cy:ca` | Per-channel sharpening |
| Camera Blur | — | ❌ | `lens_blur` (custom impl) | Depth-of-field simulation |
| Directional Blur | — | ❌ | Custom kernel / `avgblur` | Motion-direction blur |
| Radial Blur | — | ❌ | Custom / polar transform | Spin or zoom blur |
| Compound Blur | — | ❌ | Depth-map based blur | Uses luminance map |
| Channel Blur | — | ❌ | Split channels + `gblur` | Per-channel blur |
| Reduce Interlace Flicker | — | 🔶 | `smartblur` | Partial via `antiFlicker` clip property |

### 1.3 Channel

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Invert | `invert` | ✅ | `negate` | |
| Arithmetic | — | ❌ | `colorchannelmixer` | Channel math operations |
| Blend | — | ❌ | `blend` | Blend two layers/channels |
| Calculations | — | ❌ | `colorchannelmixer` | Per-channel operations |
| Set Matte | — | ⬜ | Alpha compositing | AE-style alpha control |
| Solid Composite | — | ❌ | `drawbox` / `color` overlay | Solid color overlay on clip |

### 1.4 Color Correction

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| **Lumetri Color** | `lumetri` (full system) | ✅ | Complex pipeline | Full implementation with all sections |
| Tint | — | ❌ | `colorize` / `colorchannelmixer` | Map to two colors |
| Change to Color | — | ❌ | `huesaturation` targeted | Selective hue replacement |
| Leave Color | — | ❌ | Selective desaturation | Keep one hue, desaturate rest |
| Video Limiter | — | ⬜ | `limiter` | Broadcast-safe levels |
| Fast Color Corrector | — | ⬜ | `eq` + `colorbalance` | Legacy, replaced by Lumetri |
| Three-Way Color Corrector | — | 🔶 | `colorbalance` | Covered by Lumetri color wheels |
| RGB Curves | — | 🔶 | `curves` | Covered by Lumetri RGB curves |

### 1.5 Distort

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Lens Correction | `lens_correction` | ✅ | `lenscorrection=k1:k2` | Barrel/pincushion fix |
| Warp Stabilizer | — | ❌ | `vidstabdetect` + `vidstabtransform` | 2-pass stabilization |
| Corner Pin | — | ❌ | `perspective` | 4-point transform |
| Transform | — | 🔶 | `scale`, `rotate`, `overlay` | Partial via clip properties |
| Mirror | — | ❌ | `crop` + `hflip/vflip` + `hstack` | Mirror half of frame |
| Spherize | — | ❌ | `lenscorrection` extreme values | Sphere distortion |
| Turbulent Displace | — | ❌ | No direct FFmpeg equivalent | Organic noise displacement |
| Wave Warp | — | ❌ | No direct FFmpeg equivalent | Animated wave distortion |
| Twirl | — | ❌ | No direct FFmpeg equivalent | Spiral twist effect |
| Offset | — | ❌ | `scroll` / pixel shift | Pan image within frame |

### 1.6 Generate

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Lens Flare | — | ❌ | Overlay compositing | Simulated light flare |
| Lightning | — | ❌ | No direct equivalent | Generated lightning bolt |
| Four-Color Gradient | — | ❌ | `gradients` / overlay | 4-corner gradient |
| Circle | — | ❌ | `drawbox` / overlay | Generated circle shape |
| Ellipse | — | ❌ | `drawbox` + mask | Generated ellipse |
| Grid | — | ❌ | `drawgrid` | Grid overlay |
| Paint Bucket | — | ⬜ | No direct equivalent | AE-style flood fill |
| Cell Pattern | — | ⬜ | No direct equivalent | Generated cell texture |

### 1.7 Image Control

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Black & White | `monochrome` | ✅ | `hue=s=0` | |
| Sepia | `sepia` | ✅ | `colorchannelmixer` | Warm sepia matrix |
| Color Pass | — | ❌ | Selective `huesaturation` | Keep one color, desat rest |
| Color Replace | — | ❌ | `huesaturation` selective | Replace targeted hue |
| Gamma Correction | — | ❌ | `eq=gamma` | Simple gamma control |
| Color Balance (HLS) | — | 🔶 | `colorbalance` | Covered by Lumetri |

### 1.8 Keying

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Ultra Key | — | ❌ | `chromakey` / `colorkey` | Green screen removal |
| Luma Key | — | ❌ | `lumakey` | Luminance-based transparency |
| Color Key | — | ❌ | `colorkey` | Color-based transparency |
| Track Matte Key | — | ❌ | Alpha mask compositing | Use another track as matte |
| Difference Matte | — | ⬜ | Frame diff + alpha | Background subtraction |
| Non Red Key | — | ⬜ | Custom colorkey | Blue/green screen variant |
| Image Matte Key | — | ❌ | Static alpha overlay | Still image as matte |

### 1.9 Noise & Grain

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Film Grain | `film_grain` | ✅ | `noise=alls:allf=t+u` | Animated monochrome grain |
| Median | — | ❌ | `median` | Noise reduction via median |
| Dust & Scratches | — | ⬜ | `removegrain` / `hqdn3d` | Remove small artifacts |
| Noise | — | ❌ | `noise` | Add random noise |
| Noise HLS | — | ❌ | `noise` per channel | Per-channel noise addition |

### 1.10 Perspective

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Basic 3D | — | ❌ | `rotate` + `perspective` | Simple 3D tilt/swivel |
| Drop Shadow | — | ❌ | Duplicate + offset + blur + overlay | Shadow behind clip |
| Radial Shadow | — | ❌ | Complex compositing | Light-source shadow |
| Bevel Alpha | — | ⬜ | Complex compositing | 3D edge highlight |
| Bevel Edges | — | ⬜ | Complex compositing | Frame edge bevel |

### 1.11 Stylize

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Mosaic (Pixelate) | `pixelate` | ✅ | `pixelize` | Block pixelation |
| Find Edges | `edge_detect` | ✅ | `edgedetect=mode=colormix` | Edge detection |
| Vignette | `vignette` | ✅ | `vignette` | Edge darkening |
| Color Temperature | `color_temperature` | ✅ | `colortemperature` | Warm/cool adjustment |
| Alpha Glow | — | ❌ | Glow via blur + blend | Glow around alpha edges |
| Brush Strokes | — | ⬜ | No direct equivalent | Painterly effect |
| Emboss | — | ❌ | `convolution` custom kernel | 3D-look emboss |
| Posterize | — | ❌ | `posterize` | Reduce color depth |
| Replicate | — | ❌ | `tile` | Tile/replicate frame |
| Roughen Edges | — | ⬜ | No direct equivalent | Rough alpha edges |
| Solarize | — | ❌ | `curves` with custom points | Partial color inversion |
| Strobe Light | — | ❌ | `tblend` / `select` | Flash/strobe simulation |
| Threshold | — | ❌ | `threshold` | Binary black/white |
| Write-on | — | ⬜ | `drawtext` animated | Animated brush stroke |

### 1.12 Time

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Echo (Time) | — | ❌ | `tblend` / `tmix` | Ghosting/motion trails |
| Posterize Time | — | ❌ | `fps` / `select` | Reduce frame rate for effect |

### 1.13 Transform

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Crop | clip property | ✅ | `crop` | Clip properties: cropL/R/T/B |
| Horizontal Flip | clip property | ✅ | `hflip` | Clip property: horizontalFlip |
| Vertical Flip | clip property | ✅ | `vflip` | Clip property: verticalFlip |
| Auto Reframe | — | ❌ | AI-based cropping | Smart subject tracking crop |
| Edge Feather | — | ❌ | Alpha + blur | Soft clip edges |

### 1.14 Video

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Deinterlace | `deinterlace` | ✅ | `yadif` | Interlace to progressive |
| Timecode | — | ❌ | `drawtext` with timecode | Burn-in timecode overlay |

---

## 2. AUDIO EFFECTS

### 2.1 Amplitude and Compression

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Compressor | `compressor` | ✅ | `acompressor` | Threshold, ratio, makeup |
| Hard Limiter | `limiter` | ✅ | `alimiter` | Peak ceiling |
| Loudness Normalize | `loudness_normalize` | ✅ | `loudnorm` | EBU R128 target |
| Noise Gate | `noise_gate` | ✅ | `agate` | Threshold-based gating |
| Amplify | — | ❌ | `volume` | Simple gain adjustment |
| Dynamics | — | ❌ | `acompressor` + `agate` + `alimiter` | Combined dynamics processing |
| Multiband Compressor | — | ❌ | `crossfeed` + per-band `acompressor` | Frequency-band compression |
| Tube-Modeled Compressor | — | ❌ | No direct equivalent | Analog warmth emulation |
| Single-Band Compressor | — | 🔶 | `acompressor` | Our compressor is single-band |

### 2.2 Delay and Echo

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Echo | `echo` | ✅ | `aecho` | Delay + decay |
| Delay | — | ❌ | `adelay` | Simple time delay |
| Analog Delay | — | ❌ | `aecho` with feedback | Warm tape-style delay |

### 2.3 Filter and EQ

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| High-Pass Filter | `high_pass` | ✅ | `highpass` | Remove below cutoff |
| Low-Pass Filter | `low_pass` | ✅ | `lowpass` | Remove above cutoff |
| Bass | `bass` | ✅ | `bass` | Low-freq boost/cut |
| Treble | `treble` | ✅ | `treble` | High-freq boost/cut |
| De-Esser | `deesser` | ✅ | `deesser` | Sibilance reduction |
| Parametric Equalizer | — | ❌ | `equalizer` (multi-band) | Full parametric EQ |
| Notch Filter | — | ❌ | `bandreject` | Remove narrow frequency band |
| Band Pass | — | ❌ | `bandpass` | Keep only a frequency band |
| FFT Filter | — | ❌ | `afftfilt` | Frequency-domain filtering |
| Scientific Filter | — | ⬜ | `biquad` | Butterworth/Bessel/Chebyshev |

### 2.4 Modulation

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Chorus | `chorus` | ✅ | `chorus` | Modulated delay voices |
| Flanger | `flanger` | ✅ | `flanger` | Sweeping short delay |
| Phaser | — | ❌ | `aphaser` | Phase-shift modulation |

### 2.5 Noise Reduction / Restoration

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Denoise | `audio_denoise` | ✅ | `afftdn` | Broadband noise removal |
| DeReverb | — | ❌ | No direct FFmpeg equivalent | Reduce room reverb |
| DeHum | — | ❌ | `bandreject=f=50` (+ harmonics) | Remove 50/60 Hz hum |
| Adaptive Noise Reduction | — | ❌ | `afftdn` with adaptive params | Auto-adapting denoise |
| Automatic Click Remover | — | ❌ | `adeclick` | Remove clicks/pops |
| Hiss Reduction | — | ❌ | `afftdn` highfreq focused | Tape hiss removal |

### 2.6 Reverb

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Studio Reverb | — | ❌ | `aecho` chain / IR convolution | Simulated room reverb |
| Convolution Reverb | — | ❌ | `afir` (impulse response) | Impulse-response reverb |
| Surround Reverb | — | ⬜ | `afir` + panning | Multi-channel reverb |

### 2.7 Special

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Center Vocal Reduction | `vocal_reducer` | ✅ | `pan=stereo\|c0=c0-c1\|c1=c1-c0` | Reduce centered content |
| Pitch Shifter | — | ❌ | `asetrate` + `atempo` | Change pitch without speed |
| Guitar Suite | — | ⬜ | No direct equivalent | Guitar amp simulation |
| Distortion | — | ❌ | `aeval` clipping | Intentional audio clipping |

### 2.8 Stereo Imagery

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Stereo Widener | `stereo_widener` | ✅ | `stereowiden` | Increase stereo width |
| Channel Mixer | — | ❌ | `pan` with custom matrix | Remix L/R channels |

### 2.9 Time and Pitch

| Premiere Pro Effect | Our Effect ID | Status | FFmpeg Filter | Notes |
|---|---|---|---|---|
| Pitch Shift | — | ❌ | `rubberband` / `asetrate` + `atempo` | Change pitch |
| Time Stretch | — | ❌ | `atempo` | Change speed without pitch |

---

## 3. CLIP PROPERTIES (Effect Controls — Fixed Effects)

These are per-clip properties that Premiere always shows in Effect Controls.

| Premiere Pro Property | Our Property Key | Status | Notes |
|---|---|---|---|
| Position X | `positionX` | ✅ | -100 to 100 (% of frame) |
| Position Y | `positionY` | ✅ | -100 to 100 (% of frame) |
| Scale | `scale` | ✅ | 10-400% (uniform) |
| Scale Width | `scaleWidth` | ✅ | Non-uniform scale |
| Scale Height | `scaleHeight` | ✅ | Non-uniform scale |
| Uniform Scale | `uniformScale` | ✅ | Toggle |
| Rotation | `rotation` | ✅ | -180 to 180 degrees |
| Anchor Point X | `anchorPointX` | ✅ | 0.0-1.0 |
| Anchor Point Y | `anchorPointY` | ✅ | 0.0-1.0 |
| Opacity | `opacity` | ✅ | 0-100% |
| Blend Mode | `blendMode` | ✅ | 27 modes (normal to luminosity) |
| Anti-Flicker | `antiFlicker` | ✅ | 0.0-1.0 |
| Speed | `speed` | ✅ | 1-10000% |
| Volume (dB) | `volumeDb` | ✅ | -60 to +12 dB |
| Balance / Pan | `balance` / `pan` | ✅ | Stereo positioning |

---

## 4. LUMETRI COLOR (Standalone Section)

Our app has a full Lumetri Color implementation across all 6 sections:

| Lumetri Section | Status | Key Parameters |
|---|---|---|
| Basic Correction | ✅ | Exposure, Contrast, Highlights, Shadows, Whites, Blacks, Temperature, Tint, Saturation, HDR Specular |
| Creative | ✅ | Look presets (Warm/Cool/Faded Film), Vibrance, Fade, Sharpen, Saturation, Shadow/Highlight Tint, Tint Balance |
| Curves | ✅ | Master RGB curves, per-channel (R/G/B), Hue vs Hue, Hue vs Sat, Hue vs Luma, Luma vs Sat, Sat vs Sat |
| Color Wheels & Match | ✅ | Shadow/Midtone/Highlight wheels (X/Y + Luma + Tint) |
| HSL Secondary | ✅ | Hue center/width, Saturation/Luma min-max, Correction (Hue/Sat/Luma), Denoise, Blur |
| Vignette | ✅ | Amount, Midpoint, Roundness, Feather |

Additional: Input LUT (.cube), HDR tone mapping (PQ/HLG to Rec.709), LUT interpolation (Tetrahedral/Trilinear).

---

## 5. PRIORITY ROADMAP — Recommended Next Effects

### High Priority (Professional essentials)

| # | Effect | Category | Why | Complexity |
|---|---|---|---|---|
| 1 | Ultra Key (Chroma Key) | Keying | Green screen is fundamental | Medium — `chromakey` |
| 2 | Warp Stabilizer | Distort | Shaky footage fix is #1 user need | Hard — 2-pass `vidstab` |
| 3 | Drop Shadow | Perspective | Essential for overlays/titles | Easy — blur + offset overlay |
| 4 | Posterize | Stylize | Popular creative effect | Easy — `posterize` filter |
| 5 | Levels | Adjust | Standard color correction tool | Easy — `colorlevels` |
| 6 | Parametric EQ | Audio / EQ | Pro audio mixing essential | Medium — multi `equalizer` |
| 7 | Reverb | Audio / Reverb | Room simulation is key for audio | Medium — `afir` or `aecho` chain |

### Medium Priority (Creative and polish)

| # | Effect | Category | Why | Complexity |
|---|---|---|---|---|
| 8 | Tint | Color Correction | Quick 2-color look | Easy |
| 9 | Emboss | Stylize | Popular stylize effect | Easy — convolution kernel |
| 10 | Threshold | Stylize | Black and white cutoff | Easy — `threshold` |
| 11 | Posterize Time | Time | Frame rate reduction effect | Easy — `fps` |
| 12 | Color Pass / Leave Color | Image Control | Selective color pop | Medium |
| 13 | Luma Key | Keying | Luminance compositing | Easy — `lumakey` |
| 14 | Radial Blur | Blur & Sharpen | Zoom/spin blur | Hard |
| 15 | DeReverb | Audio | Clean room echo | Hard — no direct FFmpeg |
| 16 | Phaser | Audio / Modulation | Creative audio modulation | Easy — `aphaser` |
| 17 | Pitch Shifter | Audio / Special | Change voice pitch | Medium — `rubberband` |

### Low Priority (Niche / complex)

| # | Effect | Category | Notes |
|---|---|---|---|
| 18 | Corner Pin | Distort | 4-point perspective transform |
| 19 | Mirror | Distort | Mirrored half-frame |
| 20 | Grid | Generate | Overlay grid |
| 21 | Timecode | Video | Burn-in TC overlay |
| 22 | Basic 3D | Perspective | Simple 3D tilt |
| 23 | Auto Reframe | Transform | AI-based — very complex |
| 24 | Convolution Reverb | Audio | Requires IR library |

---

## 6. ARCHITECTURE REFERENCE

### Current File Structure

```
src/app/
  effect_registry.cpp/.h          # Effect definitions catalog (id, name, category, params)
  effect_stack.cpp/.h              # Per-clip effect stack (add/remove/reorder/toggle)
  clip_effects.cpp/.h              # Fixed clip properties (position, scale, opacity, etc.)
  clip_effects_pipeline.cpp/.h     # Fixed properties -> FFmpeg filter strings
  video_effect_pipeline.cpp/.h     # Video effect stack -> FFmpeg filter strings
  audio_effect_pipeline.cpp/.h     # Audio effect stack -> FFmpeg filter strings
  lumetri_pipeline.cpp/.h          # Lumetri Color -> FFmpeg filter strings
  effect_preview_generator.cpp/.h  # Generate preview thumbnails for effects
  custom_blur_pipeline.cpp/.h      # Custom blur region mask processing
  keyframe_engine.cpp/.h           # Keyframe interpolation for animated parameters
```

### Adding a New Effect (Checklist)

1. **Register** — Add `effect(...)` entry in `effect_registry.cpp`
2. **Pipeline** — Add `else if (effectId == ...)` branch in `video_effect_pipeline.cpp` or `audio_effect_pipeline.cpp`
3. **Preview** (optional) — Update `effect_preview_generator.cpp` if real-time preview is wanted
4. **QML** — No changes needed (generic `EffectsBrowser.qml` + `EffectParameterControl.qml` auto-render parameters)
