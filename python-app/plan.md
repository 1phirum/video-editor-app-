# Effects Architecture And Delivery Plan

## Implemented Milestone

The first effect-framework milestone is now implemented:

- data-driven registry with typed numeric parameters
- ordered per-clip effect instances with persistence and project-load validation
- add, remove, reorder, bypass, reset, and parameter editing with undo/redo
- searchable Effects browser with button, double-click, and drag-to-clip apply
- dynamic Effect Controls sections for every applied instance
- FFmpeg export compilation in stack order
- live Qt Quick preview for brightness/contrast/saturation, monochrome, Gaussian
  blur, and box blur
- video effects: brightness/contrast, monochrome, Gaussian blur, box blur,
  sharpen, vignette, invert, and film grain
- audio effects: denoise, high-pass, low-pass, compressor, limiter, echo, and
  stereo center-vocal reduction

The next planned milestone is keyframes and reusable masks, followed by
tracking/stabilization. Effects without an accurate Qt Quick equivalent are
currently rendered during FFmpeg export while their full interactive preview
path remains part of the next preview-controller work.

## Purpose

Build a maintainable clip-effect system for the Qt Quick editor using native
C++ for state, validation, preview coordination, and FFmpeg command generation.
The effect UI must remain data-driven so new effects do not require adding more
logic to `Backend` or one large QML file.

This plan covers video effects, audio effects, transitions, animation,
tracking, preview behavior, export behavior, testing, and staged delivery. It
does not propose loading, linking, copying, or redistributing Adobe binaries,
presets, LUTs, models, icons, or other proprietary assets.

## Local Premiere Installation Evidence

The read-only inspection of `D:\premierpro\Adobe Premiere Pro 2024` found
components whose names indicate the major subsystems that a mature editor
separates:

- Effect Controls and Effects handlers
- Color, scopes, LUT engine, LUT manager, OpenColorIO, and HSL processing
- CPU and GPU renderers plus a video-filter host
- Audio filter host, audio DSP, mixers, Essential Sound, and convolution
  reverb resources
- Optical flow
- Speech-to-text
- ML analyzer/host infrastructure and ONNX runtime

`PresetEffects.xml` contains 108 pseudo/preset effect definitions. Visible
examples include separate XYZ position and scale controls, crop edges, fades,
rotate over time, scale bounce, wiggle position/rotation/scale, opacity pulse,
dissolve/slide/wipe/iris/radial transition controls, stereo 3D controls, face
track points, and path tracing. This file describes preset parameters and
animation controls; it is not a complete list or reusable implementation of
Premiere's compiled effects.

The architectural lesson is to keep effect definitions, parameter state,
keyframes, preview, render compilation, tracking, and UI panels independent.

## Goals

- Stack multiple ordered effects on every eligible clip.
- Enable, bypass, reorder, duplicate, reset, copy, paste, and remove effects.
- Use typed parameters with ranges, units, defaults, enum choices, and UI hints.
- Keep preview and export visually consistent within documented tolerances.
- Support keyframes and interpolation without hard-coding animation in QML.
- Preserve project compatibility when effects are added or renamed.
- Keep all FFmpeg construction in C++ modules and continue using FFmpeg CLI
  only, without FFprobe.
- Make expensive effects cancellable and cacheable.

## Non-Goals And Limits

- Adobe DLLs and assets are not an SDK and will not be invoked or redistributed.
- FFmpeg does not provide every Premiere/Resolve algorithm or identical output.
- GPU acceleration depends on the installed FFmpeg build and hardware; every
  effect needs a CPU path or a clear unsupported state.
- Object/face tracking, strong stabilization, optical flow, and source
  separation require separate analysis engines or models. They should not be
  represented as complete until tested end to end.

## Core Data Model

### EffectDefinition

Immutable metadata registered at startup:

- stable id, display name, category, media kinds, version
- parameter definitions and default values
- preview support and export support flags
- FFmpeg filter requirements and capability checks
- whether masks, tracking, keyframes, or multiple inputs are supported

### EffectParameterDefinition

- id and display name
- type: bool, integer, float, color, point, rectangle, angle, enum, file, curve
- default, minimum, maximum, step, precision, unit
- enum values and translated labels
- keyframeable flag
- UI control hint: switch, slider, color swatch, point control, curve editor

### EffectInstance

Stored per clip:

- unique instance id and definition id
- enabled/bypassed state
- current parameter values
- ordered keyframe channels
- optional mask instances and tracking references
- schema version for migrations

### Effect Stack

Each clip owns an ordered list. Motion, opacity, time remapping, and volume can
remain fixed intrinsic effects at first, followed by user-added effects. Export
must compile the stack in the same order displayed in Effect Controls.

## Backend File Layout

- `effect_definition.*`: typed metadata structures and JSON conversion
- `effect_registry.*`: built-in registration, lookup, search, and capability
  reporting
- `effect_stack.*`: add/remove/reorder/bypass/reset/copy/paste operations
- `effect_parameter_validator.*`: type conversion, ranges, enum validation,
  and migrations
- `effect_keyframes.*`: keyframe CRUD and interpolation evaluation
- `effect_presets.*`: application-owned preset import/export
- `effect_mask.*`: ellipse, rectangle, polygon, feather, expansion, inversion
- `effect_tracking.*`: analysis jobs, tracking samples, cancellation, and cache
- `video_effect_pipeline.*`: video filter graph compilation
- `audio_effect_pipeline.*`: audio filter graph compilation
- `transition_pipeline.*`: two-input transition compilation
- `effect_preview_controller.*`: debouncing, proxy quality, cache invalidation
- `timeline_placement.*`: track naming, compatibility, and vertical movement

`Backend` should expose orchestration methods and signals, while these modules
own validation and algorithms. QML panels should bind to models supplied by the
registry rather than containing effect-specific business rules.

## QML Layout

- `EffectsBrowser.qml`: searchable categories, favorites, and drag source
- `EffectControlsPanel.qml`: ordered stack for the selected clip
- `EffectSection.qml`: shared header, bypass, reset, remove, and keyframe state
- `EffectParameterControl.qml`: loader for typed parameter controls
- `KeyframeControl.qml`: previous/add-remove/next keyframe controls
- `MaskControls.qml`: mask creation and mask properties
- `ProgramMonitorOverlay.qml`: motion handles, mask paths, and tracked regions
- `TransitionControls.qml`: duration, alignment, direction, border, and easing

Effect browser drag-and-drop should add an effect to the clip under the pointer
or the currently selected clip. It must never modify the source browser item.

## Preview And Export Pipeline

1. Resolve clip source, source-in, duration, speed, and timeline time.
2. Evaluate parameter keyframes at the requested frame time.
3. Compile intrinsic transform/crop/opacity/time effects.
4. Compile ordered video and audio effect stacks.
5. Apply masks or tracked regions where supported.
6. Composite tracks and transitions.
7. Apply captions and program-level output transforms.
8. Encode using the chosen export settings.

Interactive edits should debounce preview regeneration and request a lower-cost
proxy frame while a slider or overlay is moving. On release, request a full
quality frame. Cache keys must include source identity, source time, effect
stack hash, output size, and quality mode.

## Delivery Phases

### Phase 1: Effect Framework

- Implement registry, typed parameters, instance persistence, stack editing,
  undo/redo commands, and data-driven Effect Controls.
- Migrate existing transform, crop, rotation, opacity, blur, audio gain, pan,
  filters, compression, denoise, and center-vocal removal into effect instances.
- Add FFmpeg capability detection using `ffmpeg -filters` and `ffmpeg -version`.
- Acceptance: projects round-trip, invalid values are rejected, effect order is
  deterministic, and preview/export smoke tests pass.

### Phase 2: Core Video Effects

- Gaussian/box blur, directional blur, sharpen, unsharp mask
- brightness/contrast, exposure, saturation, temperature/tint approximation
- channel mixer, monochrome, invert, posterize, threshold
- crop, mirror, flip, rotate, corner pin approximation, lens correction
- chroma/luma key, spill suppression, matte cleanup
- blend modes and opacity
- vignette, noise/grain, glow approximation, drop shadow

Prioritize filters that the installed FFmpeg build reports as available. Each
effect needs a small reference export test and an unsupported-filter message.

### Phase 3: Audio Effects

- gain, channel volume, pan, balance, mute, channel mapping
- parametric EQ, high-pass, low-pass, notch, de-esser approximation
- compressor, limiter, gate/expander, normalization
- denoise, dehum, dereverb approximation, delay, echo, reverb
- pitch and tempo controls with documented quality limits
- center-vocal reduction using stereo center cancellation

True vocal isolation should be a separate optional source-separation job, not
presented as the same feature as center cancellation.

### Phase 4: Transitions

- cross dissolve, dip to black/white, additive dissolve
- push, slide, wipe, band wipe, radial wipe, iris
- zoom, blur dissolve, page/cube approximations where feasible
- audio constant gain, constant power, and exponential fades

Transitions need explicit overlap validation, A/B input assignment, duration,
alignment, and handles. They should be timeline objects, not ordinary effects
silently attached to one clip.

### Phase 5: Keyframes And Curves

- keyframes for numeric, point, rectangle, angle, color, and curve parameters
- hold, linear, Bezier, ease-in, ease-out, and continuous Bezier interpolation
- temporal navigation and add/remove keyframe controls
- spatial motion paths with draggable handles in the Program Monitor
- copy/paste and time scaling for selected keyframes
- preset animations such as fade, bounce, pulse, oscillate, and wiggle

Store keyframes in clip-local time. Export compilation converts them to FFmpeg
expressions or segmented filter commands; unsupported expressions fall back to
frame-segment rendering rather than silently dropping animation.

### Phase 6: Masks, Tracking, And Stabilization

- rectangle, ellipse, and polygon masks with invert, feather, expansion,
  opacity, and multiple-mask combine modes
- draggable/resizable Program Monitor overlays
- forward/backward tracking jobs with progress and cancellation
- point, position/scale/rotation, and planar-region tracking modes
- attach tracked data to blur, mosaic, opacity, color correction, or transform
- stabilization analysis with crop/scale controls and rolling-shutter limits

Start with OpenCV-based point/region tracking or another proven library. Store
analysis samples separately from effect parameters so they can be invalidated
when source-in, speed, mask, or frame size changes. FFmpeg-only tracking is not
a realistic requirement for this phase.

### Phase 7: Speed And Optical Flow

- constant speed, reverse, freeze frame, frame hold, and duration changes
- time-remapping keyframes
- frame sampling and frame blending
- optical-flow interpolation through a proven engine when available
- motion-aware cache invalidation and render-progress reporting

### Phase 8: Advanced Color And Scopes

- input color-space override and output color management
- LUT import for user-owned `.cube` files
- wheels, RGB curves, hue-vs-hue/sat/luma curves, channel mixer
- secondary HSL selection with mask preview and denoise/blur controls
- waveform, RGB parade, vectorscope, and histogram
- tone mapping and HDR controls only after color-management tests exist

Scopes should run on downscaled preview frames in a worker thread and never
block timeline playback.

### Phase 9: Optional ML Features

- source separation/vocal isolation
- speech enhancement and dialogue classification
- scene-cut detection and subject masking
- face/object tracking assist
- transcription alignment improvements

Models must be application-owned or user-supplied with clear licenses. Jobs
need device selection, progress, cancellation, cache locations, and CPU fallback.

## Effect Backlog By Category

- Adjust: levels, curves, proc amp, channel mixer, extract, lighting
- Blur and sharpen: Gaussian, box, directional, radial, camera blur, sharpen
- Color: Lumetri-style basic correction, creative look, wheels, curves, HSL
- Distort: lens correction, transform, corner pin, wave, twirl, turbulence
- Generate: gradient, color matte, grid, noise, timecode
- Keying: chroma key, luma key, difference matte, spill suppressor
- Noise and grain: denoise, median, dust/scratch approximation, film grain
- Perspective: basic 3D, drop shadow, bevel/edge approximation
- Stylize: glow, mosaic, posterize, threshold, tint, emboss
- Time: echo, trails, freeze, posterize time, remapping
- Utility: crop, safe margins, metadata/timecode overlays

## Undo, Persistence, And Compatibility

- Every stack operation and parameter edit is one undoable command.
- Slider drags coalesce into a single undo command from press to release.
- Save definition id, instance id, version, enabled state, parameters,
  keyframes, masks, and tracking-cache references.
- Unknown effects remain in the project as disabled placeholders so data is not
  destroyed when a plugin/filter is unavailable.
- Migrations are per definition and covered by old-project fixtures.

## Testing And Acceptance

- Unit tests: parameter validation, track placement, interpolation, stack order,
  JSON migration, mask math, and FFmpeg escaping
- Integration tests: generate real short media, apply one effect per category,
  export, and validate duration/streams plus selected frame/audio properties
- QML tests: effect drag-and-drop, stack reorder, multiselect behavior, overlay
  drag/resize, keyboard focus, hover, disabled states, and text containment
- Visual checks: desktop and minimum supported window size; no overlapping UI
- Performance: no UI-thread FFmpeg calls; cancellation completes promptly;
  preview requests are debounced; caches have size limits and cleanup
- Failure behavior: missing FFmpeg filters, invalid files, interrupted analysis,
  and failed exports produce actionable errors without corrupting the project

## Immediate Next Milestone

1. Land the effect registry and typed parameter schema.
2. Convert current clip effects into ordered persisted instances.
3. Build the reusable Effect Controls QML components.
4. Add effect-stack undo/redo and copy/paste.
5. Compile the stack through the existing FFmpeg export path.
6. Add real export tests for stack order and bypass behavior.
7. Then implement masks and keyframes before adding more effect categories.
