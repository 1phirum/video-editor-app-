cmake_minimum_required(VERSION 3.16...3.31)

# C is enabled for the vendored Ed25519 sources (orlp/ed25519 is portable C).
project(CutPro VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep every module .dll next to the launcher .exe so they are found at run time.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Version-tolerant: any Qt 6 (no minor pinned). Build with the Qt mingw_64
# kit's bundled MinGW 13.1.0 compiler + Ninja, NOT the TDM-GCC on PATH:
#   cmake -S . -B build -G Ninja \
#     -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/mingw_64 \
#     -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe \
#     -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe \
#     -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia QuickDialogs2 Network Sql Concurrent Test ShaderTools)

# qt_standard_project_setup exists on Qt 6.3+; fall back for older Qt 6.
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
else()
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
endif()

# The Qt modules every engine library gets. Deliberately uniform, and
# deliberately without Qt6::Quick.
#
# Uniform because the alternative - a hand-picked list per library - fails at
# link time on the first file that starts using QtConcurrent, which is a build
# error that says nothing about the design and costs a full relink to discover.
# The libraries are all loaded into one process anyway, so an unused import
# table entry costs nothing measurable.
#
# Without Quick because that exclusion is load-bearing: the engine must stay
# usable from a non-GUI process (cutpro_crash_report.exe, the test binary) and
# anything that touches QQuickItem has to live in cutpro_scene or in the
# launcher. That is why the image providers and AppCursor are where they are.
set(CUTPRO_QT_MODULES
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Multimedia
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
)

# --- engine split into per-subsystem .dlls -----------------------------------
# This was one cutpro_backend.dll with a hundred sources in it. Every change to
# an effect relinked the timeline, the exporter and the preview pipeline with it,
# a crash address resolved to "cutpro_backend+0x…" which named nothing, and
# nothing stopped a low-level file from reaching up into Backend.
#
# It is now one library per subsystem, the way a large editor is normally laid
# out - Premiere ships ~200 of them beside Adobe Premiere Pro.exe. Three things
# that buys, all of them things this project has needed recently:
#
#  * a layering mistake becomes a link error naming both sides, instead of
#    compiling quietly and being found months later;
#  * a minidump frame reads cutpro_effects+0x… - the subsystem is in the crash
#    report before anyone opens the dump;
#  * editing one subsystem relinks one .dll.
#
# The dependency graph must stay acyclic: MinGW resolves an import library at
# link time, so two .dlls that include each other's headers cannot both be
# linked last. tools/module_graph.py --split derives the graph from the actual
# #include edges and prints any mutual pair; it must print "none" before this
# list is changed. tools/module_graph.py --cmake prints the file lists below, so
# a moved file cannot silently fall out of the build.
function(cutpro_module name)
    cmake_parse_arguments(CUTPRO_MOD "" "" "SOURCES;LINK" ${ARGN})
    qt_add_library(${name} SHARED ${CUTPRO_MOD_SOURCES})
    target_include_directories(${name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${name} PUBLIC ${CUTPRO_MOD_LINK} ${CUTPRO_QT_MODULES})
    # Auto-generate the DLL export table (MinGW and MSVC) so no per-class
    # __declspec macros are needed. It is also what makes GuiStallTracer's
    # symbolised frames resolve to real names without a PDB.
    set_target_properties(${name} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    # Authenticode-sign the DLL when a release build asks for it. No-op unless
    # -DCUTPRO_SIGN=ON, so a normal dev build never touches signtool.
    cutpro_authenticode_sign(${name})
endfunction()

# --- Authenticode signing (the "code-sign everything" layer) ------------------
# OFF by default: a dev build produces unsigned binaries and never calls
# signtool. A release turns it on and points it at a certificate:
#
#   -DCUTPRO_SIGN=ON -DCUTPRO_SIGN_CERT=C:/keys/cutpro.pfx \
#       -DCUTPRO_SIGN_CERT_PASSWORD=... [-DCUTPRO_SIGN_TIMESTAMP_URL=...]
#   -- or, for a cert already in the Windows store:
#   -DCUTPRO_SIGN=ON -DCUTPRO_SIGN_SHA1=<thumbprint>
#
# Signing every shipped .exe/.dll is what makes the runtime Authenticode check
# (-DCUTPRO_REQUIRE_SIGNATURE=ON) pass on our own binaries. It runs as a
# POST_BUILD step, so the integrity manifest - stamped later, over the final
# files - hashes the signed bytes. See the release procedure in the README.
option(CUTPRO_SIGN "Authenticode-sign built binaries via signtool" OFF)
set(CUTPRO_SIGNTOOL "signtool" CACHE STRING "Path to signtool.exe")
set(CUTPRO_SIGN_CERT "" CACHE STRING "Path to a .pfx code-signing certificate")
set(CUTPRO_SIGN_CERT_PASSWORD "" CACHE STRING "Password for the .pfx certificate")
set(CUTPRO_SIGN_SHA1 "" CACHE STRING "SHA1 thumbprint of an installed cert (instead of a .pfx)")
set(CUTPRO_SIGN_TIMESTAMP_URL "http://timestamp.digicert.com"
    CACHE STRING "RFC-3161 timestamp URL")

function(cutpro_authenticode_sign target)
    if(NOT CUTPRO_SIGN OR NOT WIN32)
        return()
    endif()
    if(CUTPRO_SIGN_CERT)
        set(certArgs /f "${CUTPRO_SIGN_CERT}")
        if(CUTPRO_SIGN_CERT_PASSWORD)
            list(APPEND certArgs /p "${CUTPRO_SIGN_CERT_PASSWORD}")
        endif()
    elseif(CUTPRO_SIGN_SHA1)
        set(certArgs /sha1 "${CUTPRO_SIGN_SHA1}")
    else()
        message(FATAL_ERROR
            "CUTPRO_SIGN=ON needs CUTPRO_SIGN_CERT (a .pfx) or "
            "CUTPRO_SIGN_SHA1 (an installed cert's thumbprint).")
    endif()
    # SHA-256 digest + an RFC-3161 timestamp, so signatures stay valid after the
    # cert expires. VERBATIM keeps the password/path intact through the shell.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CUTPRO_SIGNTOOL}" sign ${certArgs}
                /fd SHA256 /tr "${CUTPRO_SIGN_TIMESTAMP_URL}" /td SHA256
                "$<TARGET_FILE:${target}>"
        COMMENT "Authenticode-signing ${target}"
        VERBATIM)
endfunction()


# cutpro_core - ids, enums, version, caption style. No dependencies, by rule.
cutpro_module(cutpro_core SOURCES
    src/app/caption_style.cpp
    src/app/caption_voice_yin.cpp
    src/app/caption_word_clock.cpp
    src/app/text_effect_catalog.cpp
    src/core/ids.cpp
    src/app/caption_style.h
    src/app/caption_voice_yin.h
    src/app/caption_word_clock.h
    src/app/text_effect_catalog.h
    src/core/enums.h
    src/core/ids.h
    src/core/module_api.h
    src/core/signal.h
    src/core/version.h
)

# --- cutpro_ed25519 - vendored Ed25519 (orlp/ed25519), compiled once ----------
# A Qt-free static library. Defined up here (near the bottom of the graph) so
# cutpro_guard below can link it, and so both cutpro_guard (verify) and the
# vendor-only license_gen tool (keygen + sign) link the exact same objects with
# no duplicate-symbol risk. It bundles its own SHA-512, so no OpenSSL / libsodium
# dependency is introduced.
add_library(cutpro_ed25519 STATIC
    src/thirdparty/ed25519/fe.c
    src/thirdparty/ed25519/ge.c
    src/thirdparty/ed25519/sc.c
    src/thirdparty/ed25519/sha512.c
    src/thirdparty/ed25519/keypair.c
    src/thirdparty/ed25519/sign.c
    src/thirdparty/ed25519/verify.c
)
target_include_directories(cutpro_ed25519 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thirdparty/ed25519")
set_property(TARGET cutpro_ed25519 PROPERTY POSITION_INDEPENDENT_CODE ON)

# cutpro_guard - the anti-tamper / licence-verify floor of the graph.
#
# It owns the verify-only licence code (signing lives only in license_gen) plus
# the tamper flag, the debugger sweep, the signed-manifest binary-integrity
# check and the independent export/periodic re-verify. It sits BELOW both
# cutpro_backend and cutpro_export so each can run its own licence/integrity
# check without trusting Backend's in-memory gate: a crack that patches one site
# leaves the others live. Depends only on Ed25519 + Qt, by rule - never on
# another cutpro_* module, so it cannot create a cycle. See [[cross-dll-pmf-connect-hazard]].
cutpro_module(cutpro_guard
    LINK cutpro_ed25519
    SOURCES
    src/app/guard/tamper.cpp
    src/app/guard/anti_debug.cpp
    src/app/guard/binary_integrity.cpp
    src/app/guard/license_session.cpp
    src/app/license/license.cpp
    src/app/license/license_codec.cpp
    src/app/license/license_pubkey.cpp
    src/app/license/machine_fingerprint.cpp
    src/app/guard/tamper.h
    src/app/guard/anti_debug.h
    src/app/guard/binary_integrity.h
    src/app/guard/license_session.h
    src/app/license/license.h
    src/app/license/license_codec.h
    src/app/license/machine_fingerprint.h
)
if(WIN32)
    # WinVerifyTrust (Authenticode self-check) lives in wintrust; the cert chain
    # walk it does needs crypt32.
    target_link_libraries(cutpro_guard PRIVATE wintrust crypt32)
endif()

# Release hardening switches, off by default so a plain dev build is never
# punished for lacking a signed manifest or a code-signing cert:
#   -DCUTPRO_ENFORCE_INTEGRITY=ON  verify integrity.man at runtime (release)
#   -DCUTPRO_REQUIRE_SIGNATURE=ON  also require a valid Authenticode signature
option(CUTPRO_ENFORCE_INTEGRITY "Enforce the signed integrity manifest at runtime" OFF)
option(CUTPRO_REQUIRE_SIGNATURE "Also require a valid Authenticode signature at runtime" OFF)
if(CUTPRO_ENFORCE_INTEGRITY)
    target_compile_definitions(cutpro_guard PRIVATE CUTPRO_ENFORCE_INTEGRITY=1)
endif()
if(CUTPRO_REQUIRE_SIGNATURE)
    target_compile_definitions(cutpro_guard PRIVATE CUTPRO_REQUIRE_SIGNATURE=1)
endif()

# cutpro_performance - the primitives a twenty-thousand-clip sequence needs.
# perf_kernel is the general layer: an interval index for "what is under the
# playhead", a frame-budgeted pump so a per-cue loop yields to the event loop
# instead of freezing it, a chunked parallel_for, a byte-bounded LRU, and an
# aggregating timer to find out which of those a given stall actually wants. The
# rest replace specific linear scans that are measurably in the way - span_table
# for the timeline model's windowed projection, span_order for the (lane, time)
# order that projection's collapse rule has to be given to work at all,
# lane_neighbors for the drag seam
# and snap queries, gesture_pacer for work driven by a scroll, id_set for
# per-delegate selection tests, batch_progress for a run that reports forty
# thousand times. Depends on Qt only - deliberately not on cutpro_core, so it
# sits at the very bottom of the graph and any subsystem may link it without
# creating a cycle. Nothing here is a QObject, so no export macro and no moc.
cutpro_module(cutpro_performance SOURCES
    src/app/performance/batch_progress.cpp
    src/app/performance/batch_progress.h
    src/app/performance/gesture_pacer.cpp
    src/app/performance/gesture_pacer.h
    src/app/performance/id_set.h
    src/app/performance/lane_neighbors.cpp
    src/app/performance/lane_neighbors.h
    src/app/performance/perf_kernel.cpp
    src/app/performance/perf_kernel.h
    src/app/performance/span_order.cpp
    src/app/performance/span_order.h
    src/app/performance/span_table.cpp
    src/app/performance/span_table.h
)

# cutpro_diagnostics - the layer that explains a freeze. Depends on core only,
# because everything else depends on it: the watchdog, the GUI dispatcher, the
# stall tracer, the shared-memory crash channel, the reporter launcher and the
# Repeater-model guard. Nothing here may reach into a subsystem.
cutpro_module(cutpro_diagnostics
    LINK cutpro_core
    SOURCES
    src/app/diagnostics/crash_channel.cpp
    src/app/diagnostics/crash_reporter_host.cpp
    src/app/diagnostics/diagnostic_analyzer.cpp
    src/app/diagnostics/model_guard.cpp
    src/app/diagnostics/playback_trace.cpp
    src/app/preview/gui_dispatch.cpp
    src/app/preview/gui_stall_report.cpp
    src/app/preview/gui_stall_tracer.cpp
    src/app/preview/gui_thread_watchdog.cpp
    src/app/preview/gui_turn_monitor.cpp
    src/app/diagnostics/crash_channel.h
    src/app/diagnostics/crash_reporter_host.h
    src/app/diagnostics/diagnostic_analyzer.h
    src/app/diagnostics/model_guard.h
    src/app/diagnostics/playback_trace.h
    src/app/preview/gui_dispatch.h
    src/app/preview/gui_stall_report.h
    src/app/preview/gui_stall_tracer.h
    src/app/preview/gui_thread_watchdog.h
    src/app/preview/gui_turn_monitor.h
)
if(WIN32)
    # GuiStallTracer symbolises a suspended thread's stack; the minidump side
    # needs the same library.
    target_link_libraries(cutpro_diagnostics PRIVATE dbghelp)
endif()

cutpro_module(cutpro_settings SOURCES
    src/app/settings/app_settings.cpp
    src/app/settings/app_settings.h
)

cutpro_module(cutpro_media SOURCES
    src/app/media/noise_removal.cpp
    src/app/media/noise_removal.h
    src/app/media/ffmpeg_runtime.cpp
    src/app/media/python_runtime.cpp
    src/app/media/media_import_queue.cpp
    src/app/media/media_metadata.cpp
    src/app/media/media_path.cpp
    src/app/media/media_scan.cpp
    src/app/media/ffmpeg_runtime.h
    src/app/media/python_runtime.h
    src/app/media/media_import_queue.h
    src/app/media/media_metadata.h
    src/app/media/media_path.h
    src/app/media/media_scan.h
)

cutpro_module(cutpro_subtitles
    LINK cutpro_diagnostics cutpro_performance cutpro_media cutpro_pool
    SOURCES
    src/app/subtitles/khmer_support.cpp
    src/app/subtitles/speech_chain.cpp
    src/app/subtitles/speech_fit.cpp
    src/app/subtitles/speech_report.cpp
    src/app/subtitles/recap_generator.cpp
    src/app/subtitles/subtitle_io.cpp
    src/app/subtitles/subtitle_timeline.cpp
    src/app/subtitles/text_to_speech_engine.cpp
    src/app/subtitles/timed_speech_placer.cpp
    src/app/subtitles/transcript_translator.cpp
    src/app/subtitles/transcription_batch.cpp
    src/app/subtitles/transcription_plan.cpp
    src/app/subtitles/translation_model_probe.cpp
    src/app/subtitles/window_audio_prefetcher.cpp
    src/app/subtitles/khmer_support.h
    src/app/subtitles/speech_chain.h
    src/app/subtitles/speech_fit.h
    src/app/subtitles/speech_report.h
    src/app/subtitles/recap_generator.h
    src/app/subtitles/subtitle_io.h
    src/app/subtitles/subtitle_timeline.h
    src/app/subtitles/text_to_speech_engine.h
    src/app/subtitles/timed_speech_placer.h
    src/app/subtitles/transcript_translator.h
    src/app/subtitles/transcription_batch.h
    src/app/subtitles/transcription_plan.h
    src/app/subtitles/translation_model_probe.h
    src/app/subtitles/window_audio_prefetcher.h
)

cutpro_module(cutpro_effects SOURCES
    src/app/effects/audio_effect_pipeline.cpp
    src/app/effects/clip_effects.cpp
    src/app/effects/clip_effects_pipeline.cpp
    src/app/effects/effect_presets.cpp
    src/app/effects/effect_registry.cpp
    src/app/effects/effect_stack.cpp
    src/app/effects/video_effect_pipeline.cpp
    src/app/effects/audio_effect_pipeline.h
    src/app/effects/clip_effects.h
    src/app/effects/clip_effects_pipeline.h
    src/app/effects/effect_presets.h
    src/app/effects/effect_registry.h
    src/app/effects/effect_stack.h
    src/app/effects/video_effect_pipeline.h
)

cutpro_module(cutpro_lumetri SOURCES
    src/app/lumetri/color_settings.cpp
    src/app/lumetri/custom_blur_pipeline.cpp
    src/app/lumetri/lumetri_pipeline.cpp
    src/app/lumetri/lumetri_preview.cpp
    src/app/lumetri/color_settings.h
    src/app/lumetri/custom_blur_pipeline.h
    src/app/lumetri/lumetri_pipeline.h
    src/app/lumetri/lumetri_preview.h
)

cutpro_module(cutpro_timeline
    LINK cutpro_diagnostics cutpro_performance
    SOURCES
    src/app/timeline/keyframe_engine.cpp
    src/app/timeline/large_media_policy.cpp
    src/app/timeline/long_media_timeline_handler.cpp
    src/app/timeline/timeline_clip_binding.cpp
    src/app/timeline/timeline_clip_model.cpp
    src/app/timeline/timeline_editor.cpp
    src/app/timeline/timeline_placement.cpp
    src/app/timeline/timeline_placement_job.cpp
    src/app/timeline/keyframe_engine.h
    src/app/timeline/large_media_policy.h
    src/app/timeline/long_media_timeline_handler.h
    src/app/timeline/timeline_clip_binding.h
    src/app/timeline/timeline_clip_model.h
    src/app/timeline/timeline_editor.h
    src/app/timeline/timeline_placement.h
    src/app/timeline/timeline_placement_job.h
)

# cutpro_preview - decode, caching, filmstrips, waveforms, prefetch. The
# biggest module and the one under active investigation, which is exactly why it
# is worth having on its own: a stall trace or a dump frame inside it now says so
# by module name.
cutpro_module(cutpro_preview
    LINK cutpro_diagnostics cutpro_effects cutpro_media
    SOURCES
    src/app/preview/audio_peak_builder.cpp
    src/app/preview/audio_peak_window_service.cpp
    src/app/preview/codec_decode_traits.cpp
    src/app/preview/decode_cost_model.cpp
    src/app/preview/decode_session.cpp
    src/app/preview/decode_work_governor.cpp
    src/app/preview/effect_preview_generator.cpp
    src/app/preview/ffmpeg_preview_decoder.cpp
    src/app/preview/filmstrip_builder.cpp
    src/app/preview/frame_buffer_pool.cpp
    src/app/preview/frame_cache.cpp
    src/app/preview/keyframe_index.cpp
    src/app/preview/large_media_preview_job.cpp
    src/app/preview/media_preview_generator.cpp
    src/app/preview/media_token_registry.cpp
    src/app/preview/native_video_preview_decoder.cpp
    src/app/preview/preview_audio_sink.cpp
    src/app/preview/preview_cache.cpp
    src/app/preview/preview_decode_policy.cpp
    src/app/preview/preview_failure_registry.cpp
    src/app/preview/scrub_frame_service.cpp
    src/app/preview/scope_renderer.cpp
    src/app/preview/seek_thumbnail_extractor.cpp
    src/app/preview/startup_warmup.cpp
    src/app/preview/timeline_preview_prefetcher.cpp
    src/app/preview/timeline_thumbnail_service.cpp
    src/app/preview/timeline_tile_cache.cpp
    src/app/preview/video_preview_helper.cpp
    src/app/preview/voice_over_recorder.cpp
    src/app/preview/webcam_recorder.cpp
    src/app/preview/audio_peak_builder.h
    src/app/preview/voice_over_recorder.h
    src/app/preview/webcam_recorder.h
    src/app/preview/audio_peak_window_service.h
    src/app/preview/codec_decode_traits.h
    src/app/preview/decode_cost_model.h
    src/app/preview/decode_session.h
    src/app/preview/decode_work_governor.h
    src/app/preview/effect_preview_generator.h
    src/app/preview/ffmpeg_preview_decoder.h
    src/app/preview/filmstrip_builder.h
    src/app/preview/frame_buffer_pool.h
    src/app/preview/frame_cache.h
    src/app/preview/keyframe_index.h
    src/app/preview/large_media_preview_job.h
    src/app/preview/media_preview_generator.h
    src/app/preview/media_token_registry.h
    src/app/preview/native_video_preview_decoder.h
    src/app/preview/preview_audio_sink.h
    src/app/preview/preview_cache.h
    src/app/preview/preview_decode_policy.h
    src/app/preview/preview_failure_registry.h
    src/app/preview/scrub_frame_service.h
    src/app/preview/scope_renderer.h
    src/app/preview/seek_thumbnail_extractor.h
    src/app/preview/startup_warmup.h
    src/app/preview/swscale_colorspace.h
    src/app/preview/timeline_preview_prefetcher.h
    src/app/preview/timeline_thumbnail_service.h
    src/app/preview/timeline_tile_cache.h
    src/app/preview/video_preview_helper.h
)

# Direct FFmpeg linkage. The bundled FFmpeg 9 shared build is selected
# explicitly so the compiler never silently falls back to an older PATH install.
# Only two modules touch libav* directly - media (metadata probing) and preview
# (decode, peaks, thumbnails) - and the split makes that checkable rather than
# assumed: grep for "#include <libav" and the answer is those two directories.
set(CUTPRO_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg-9.0.1-full_build-shared" CACHE PATH "FFmpeg shared build root")
if(EXISTS "${CUTPRO_FFMPEG_ROOT}/include" AND EXISTS "${CUTPRO_FFMPEG_ROOT}/lib/libavcodec.dll.a")
    foreach(cutpro_ffmpeg_target cutpro_media cutpro_preview)
        target_include_directories(${cutpro_ffmpeg_target} PRIVATE "${CUTPRO_FFMPEG_ROOT}/include")
        target_link_directories(${cutpro_ffmpeg_target} PRIVATE "${CUTPRO_FFMPEG_ROOT}/lib")
        target_link_libraries(${cutpro_ffmpeg_target} PRIVATE avformat avcodec avutil swscale swresample)
        target_compile_definitions(${cutpro_ffmpeg_target} PRIVATE CUTPRO_HAS_NATIVE_FFMPEG=1)
    endforeach()
else()
    message(WARNING "FFmpeg 9 shared build not found at ${CUTPRO_FFMPEG_ROOT}; using process preview fallback")
endif()

cutpro_module(cutpro_export
    LINK cutpro_effects cutpro_lumetri cutpro_subtitles cutpro_guard
    SOURCES
    src/app/export/sequence_export_builder.cpp
    src/app/export/sequence_export_builder.h
    src/app/export/timeline_effect_window.cpp
    src/app/export/timeline_effect_window.h
)

# cutpro_backend - the facade. Backend itself, its selection/caption/settings
# parts, the project database and the project media actions. It is the only
# module allowed to depend on all the others, which is the point: everything
# below it stays independently linkable, and the test binary and the crash
# reporter can pull in a subsystem without dragging Backend along.
# cutpro_backend - the facade. Backend itself, its selection/caption/settings
# parts, the project database and the project media actions. It is the only
# module allowed to depend on all the others. The verify-only licence code and
# the anti-tamper checks now live in cutpro_guard (linked below); only the
# QML-facing bridge, backend_license.cpp, stays here.
cutpro_module(cutpro_pool
    LINK
    cutpro_core
    cutpro_media
    SOURCES
    src/app/core_app/pool_manager.cpp
    src/app/core_app/pool_manager.h
)

cutpro_module(cutpro_backend
    LINK
    cutpro_core
    cutpro_diagnostics
    cutpro_settings
    cutpro_media
    cutpro_pool
    cutpro_subtitles
    cutpro_effects
    cutpro_lumetri
    cutpro_timeline
    cutpro_preview
    cutpro_export
    cutpro_guard
    SOURCES
    src/app/core_app/backend.cpp
    src/app/core_app/backend_export.cpp
    src/app/core_app/backend_caption.cpp
    src/app/core_app/backend_clip_ops.cpp
    src/app/core_app/backend_fonts.cpp
    src/app/core_app/backend_recap.cpp
    src/app/core_app/backend_requirements_cache.cpp
    src/app/core_app/backend_selection.cpp
    src/app/core_app/backend_shortcuts.cpp
    src/app/core_app/backend_translation.cpp
    src/app/core_app/caption_speech_timing.cpp
    src/app/core_app/caption_speech_timing.h
    src/app/core_app/project_database.cpp
    src/app/core_app/project_save_service.cpp
    src/app/core_app/signal_coalescer.cpp
    src/app/project/backend_project_media.cpp
    src/app/core_app/backend_voiceover.cpp
    src/app/core_app/backend_voicechanger.cpp
    src/app/core_app/backend_noise_removal.cpp
    src/app/core_app/backend_quick_export.cpp
    src/app/project/project_media_actions.cpp
    src/app/settings/backend_settings.cpp
    src/app/settings/backend_scopes.cpp
    # License activation bridge (QML-facing). Verify + anti-tamper live in
    # cutpro_guard; this only wires them to Backend and the registry.
    src/app/license/backend_license.cpp
    src/app/core_app/backend.h
    src/app/core_app/project_database.h
    src/app/core_app/project_save_service.h
    src/app/core_app/signal_coalescer.h
    src/app/project/project_media_actions.h
)

# cutpro_scene - the only engine library that links Qt6::Quick.
#
# ItemTreeCensus has to walk QQuickItem::childItems() to count what is actually
# in the scene, and DiagnosticsBridge exposes it to QML. Both therefore need
# QtQuick, which cutpro_backend must not have: put them in cutpro_diagnostics and
# every module above it acquires a Quick dependency, including the ones that have
# to stay usable from a console process. Only the launcher links this.
qt_add_library(cutpro_scene SHARED
    src/app/diagnostics/diagnostics_bridge.cpp
    src/app/diagnostics/diagnostics_cli.cpp
    src/app/diagnostics/item_tree_census.cpp
    src/app/diagnostics/diagnostics_bridge.h
    src/app/diagnostics/diagnostics_cli.h
    src/app/diagnostics/item_tree_census.h
)
target_include_directories(cutpro_scene PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cutpro_scene PUBLIC
    cutpro_diagnostics
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)
set_target_properties(cutpro_scene PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# --- cutpro_crash_report.exe --------------------------------------------------
# The out-of-process hang and crash reporter, launched by cutpro.exe at startup
# and shipped beside it - the same arrangement as AdobeCrashReport.exe next to
# Adobe Premiere Pro.exe, for the same reason: a process cannot report its own
# hang (the thread that would write the report is the wedged one) and cannot be
# trusted to report its own crash (the heap may be what broke).
#
# crash_channel.cpp is compiled in here rather than linked from
# cutpro_diagnostics.dll on purpose. The reporter's whole job is to survive
# situations where the app is broken, so it must not need any of the app's .dlls
# to start; the two processes share the memory mapping, not a library. Qt6Core is
# the only Qt module it needs - the crash dialog is a plain MessageBoxW.
qt_add_executable(cutpro_crash_report
    src/tools/crash_report/main.cpp
    src/tools/crash_report/minidump_writer.cpp
    src/tools/crash_report/minidump_writer.h
    src/app/diagnostics/crash_channel.cpp
    src/app/diagnostics/crash_channel.h
)
target_include_directories(cutpro_crash_report PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cutpro_crash_report PRIVATE Qt6::Core)
if(WIN32)
    target_link_libraries(cutpro_crash_report PRIVATE dbghelp)
endif()
# No console window: the reporter is invisible unless it has something to say,
# and a flashing console at every launch would be the most visible thing about it.
set_target_properties(cutpro_crash_report PROPERTIES WIN32_EXECUTABLE ON)
cutpro_authenticode_sign(cutpro_crash_report)

# --- cutpro_whisper.exe (bundled offline transcription worker) ----------------
# Native whisper.cpp (ggml) transcription, shipped beside cutpro.exe the way
# cutpro_crash_report.exe is. It replaces tools/whisper_transcribe.py so offline
# transcription needs no Python and no pip: the exe ships ready and the user only
# downloads a GGML model file from the Requirements page. It speaks the exact
# same line-delimited-JSON protocol the Python worker did, so the prefetcher,
# windowing, job.json and mergeWindow around it are unchanged.
#
# whisper.cpp is pulled in with FetchContent at a pinned tag and built CPU-only
# (every GPU backend off) as static libs, so there is no extra runtime DLL to
# deploy - only cutpro_whisper.exe and the model the user downloads. The first
# configure needs internet to fetch the tag; a locked-down build can later vendor
# the source into third_party/ without touching this target.
include(FetchContent)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(GGML_OPENCL OFF CACHE BOOL "" FORCE)
set(GGML_BLAS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(whispercpp
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG v1.7.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(whispercpp)

qt_add_executable(cutpro_whisper
    src/tools/whisper_worker/main.cpp
    src/tools/whisper_worker/wav_reader.cpp
    src/tools/whisper_worker/wav_reader.h
    src/tools/whisper_worker/segmenter.cpp
    src/tools/whisper_worker/segmenter.h
    src/tools/whisper_worker/model_paths.h
)
target_include_directories(cutpro_whisper PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cutpro_whisper PRIVATE Qt6::Core whisper)
# No console window: the worker is launched by the app and only ever speaks JSON
# on its pipes, never to a terminal.
set_target_properties(cutpro_whisper PROPERTIES WIN32_EXECUTABLE ON)
cutpro_authenticode_sign(cutpro_whisper)

# --- license_gen.exe (vendor-only, NEVER shipped) -----------------------------
# The offline license generator. Off by default so a normal build never
# produces it; it is not a dependency of cutpro and is not copied by the deploy
# step, so the signing key material can never leak into a shipped build. It
# links only Qt6::Core plus the vendored Ed25519 static lib - no cutpro_* module,
# so it cannot drag app code (or a key) across.
option(CUTPRO_BUILD_LICENSE_TOOLS "Build the offline license generator (vendor-only)" OFF)
if(CUTPRO_BUILD_LICENSE_TOOLS)
    # A dedicated folder so the vendor has one place to run the tool from,
    # kept apart from the app's own bin/.
    set(CUTPRO_LICENSE_TOOLS_DIR "${CMAKE_BINARY_DIR}/license-tools")

    # CLI generator (scriptable).
    qt_add_executable(license_gen
        src/tools/license_gen/main.cpp
        src/app/license/license_codec.cpp
        src/app/license/license_codec.h
    )
    target_include_directories(license_gen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(license_gen PRIVATE Qt6::Core cutpro_ed25519)
    set_target_properties(license_gen PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CUTPRO_LICENSE_TOOLS_DIR}")

    # GUI generator (paste Machine ID -> Generate -> Copy). Needs Qt Widgets.
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    qt_add_executable(license_gen_gui
        src/tools/license_gen/gui_main.cpp
        src/app/license/license_codec.cpp
        src/app/license/license_codec.h
    )
    target_include_directories(license_gen_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(license_gen_gui PRIVATE Qt6::Core Qt6::Widgets cutpro_ed25519)
    set_target_properties(license_gen_gui PROPERTIES
        WIN32_EXECUTABLE ON
        RUNTIME_OUTPUT_DIRECTORY "${CUTPRO_LICENSE_TOOLS_DIR}")

    # Make the folder self-contained: copy the Qt runtime the GUI needs beside
    # it via windeployqt, so the vendor can just double-click the exe.
    if(WIN32)
        find_program(CUTPRO_WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin"
            "${Qt6_DIR}/../../../bin")
        if(CUTPRO_WINDEPLOYQT)
            add_custom_command(TARGET license_gen_gui POST_BUILD
                COMMAND "${CUTPRO_WINDEPLOYQT}" --no-translations --compiler-runtime
                        "$<TARGET_FILE:license_gen_gui>"
                COMMENT "Deploying Qt runtime beside license_gen_gui")
        endif()
    endif()
endif()

# --- Thin launcher executable -----------------------------------------------
# Owns the QML UI resources and links the engine modules.
#
# By default cutpro.exe is a GUI-subsystem (WIN32) binary, so launching it opens
# no console window: this is what a shipped/installed build must be. Qt logging
# still runs, but goes to OutputDebugString (visible in a debugger / DebugView)
# rather than to a terminal the client sees. Developers who want the live console
# with the watchdog and qDebug output configure with -DCUTPRO_CONSOLE=ON, which
# builds it as a console-subsystem binary instead. See [[cutpro-build-and-launch]].
option(CUTPRO_CONSOLE "Build cutpro.exe as a console app (dev logging window)" OFF)
qt_add_executable(cutpro
    src/main.cpp
    # Local MCP transport stays on the launcher side; it reflects an explicit
    # editing allowlist and never adds a reverse engine-module dependency.
    src/app/mcp/mcp_server.cpp
    src/app/mcp/mcp_server.h
    src/app/mcp/mcp_catalog.cpp
    src/app/mcp/mcp_catalog.h
    src/app/ai/ai_assistant.cpp
    src/app/ai/ai_assistant.h
    src/app/ai/ai_message_model.cpp
    src/app/ai/ai_message_model.h
    # The on-demand preview providers need QtQuick's async image provider, which
    # the engine modules deliberately do not link. They are UI glue, so they live
    # here.
    src/app/preview/timeline_thumbnail_provider.cpp
    src/app/preview/timeline_thumbnail_provider.h
    src/app/preview/waveform_window_provider.cpp
    src/app/preview/waveform_window_provider.h
    # Bitmap cursors attach themselves to QQuickItems, so they belong on this
    # side of the QtQuick split too.
    src/app/ui/app_cursor.cpp
    src/app/ui/app_cursor.h
    # UI language switching. Lives here (not in a .dll) because it calls
    # retranslate() on the QQmlApplicationEngine the launcher owns.
    src/app/ui/localization.cpp
    src/app/ui/localization.h
    # Self-updater (GitHub Releases). Lives on the launcher side, not in a .dll,
    # to sidestep the cross-DLL Q_OBJECT connect hazard and because it quits the
    # QCoreApplication the launcher owns. See [[cross-dll-pmf-connect-hazard]].
    src/app/update/update_checker.cpp
    src/app/update/update_checker.h
    # The executable's own icon, compiled by windres into cutpro.exe's resource
    # table. It has to be a source of this target and not of a .dll: Windows reads
    # the application icon from the .exe it is asked to draw.
    $<$<BOOL:${WIN32}>:src/cutpro.rc>
)
target_link_libraries(cutpro PRIVATE
    cutpro_backend
    cutpro_scene
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::QuickDialogs2
)
if(WIN32)
    target_link_libraries(cutpro PRIVATE crypt32)
endif()
# The reporter is useless if it is not beside the app it watches, so building the
# app builds it. CrashReporterHost looks for it in applicationDirPath().
add_dependencies(cutpro cutpro_crash_report)

# The native transcription worker ships beside the app; building the app builds
# it. The backend launches it from applicationDirPath()/cutpro_whisper.exe.
add_dependencies(cutpro cutpro_whisper)

# No console window for a shipped build: a GUI-subsystem binary launches silently.
# Flipped back to a console app only when the developer asks with -DCUTPRO_CONSOLE=ON.
if(WIN32 AND NOT CUTPRO_CONSOLE)
    set_target_properties(cutpro PROPERTIES WIN32_EXECUTABLE ON)
endif()

# Sign the launcher itself. Registered before the FFmpeg/tools copy step below so
# signtool runs on cutpro.exe as soon as it links; the copied third-party DLLs
# keep whatever signatures their vendors gave them.
cutpro_authenticode_sign(cutpro)

add_custom_command(TARGET cutpro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cutpro>/models/noise-removal"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/noise-removal"
        "$<TARGET_FILE_DIR:cutpro>/models/noise-removal"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cutpro>/ffmpeg/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CUTPRO_FFMPEG_ROOT}/bin" "$<TARGET_FILE_DIR:cutpro>/ffmpeg/bin"
    # Native FFmpeg linkage requires the versioned DLLs beside the launcher;
    # keep the command-line tools isolated under ffmpeg/bin.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CUTPRO_FFMPEG_ROOT}/bin/avcodec-63.dll"
        "${CUTPRO_FFMPEG_ROOT}/bin/avformat-63.dll"
        "${CUTPRO_FFMPEG_ROOT}/bin/avutil-61.dll"
        "${CUTPRO_FFMPEG_ROOT}/bin/swresample-7.dll"
        "${CUTPRO_FFMPEG_ROOT}/bin/swscale-10.dll"
        "$<TARGET_FILE_DIR:cutpro>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cutpro>/tools"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/whisper_transcribe.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/whisper_transcribe.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/gemini_transcribe.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/gemini_transcribe.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/translate_transcript.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/translate_transcript.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/persistent_retry.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/persistent_retry.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/pool_tool.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/pool_tool.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/pool_activity.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/pool_activity.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/demucs_separate.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/demucs_separate.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/render_khmer_subtitles.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/render_khmer_subtitles.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/text_to_speech.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/text_to_speech.py")

# Embed the untouched QML frontend + icon assets. BASE = project root so
# main.qml loads as qrc:/qml/main.qml and the QML's relative
# "../../assets/icons/x.svg" resolves to qrc:/assets/icons/x.svg unchanged.
file(GLOB_RECURSE cutpro_qml CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
file(GLOB cutpro_icons CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
# Bitmap mouse cursors, scanned at runtime by AppCursor.
file(GLOB cutpro_cursors CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/*.png")

qt_add_resources(cutpro "cutpro_frontend"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        ${cutpro_qml}
        # extension-less — not matched by *.qml; without it the Theme singleton
        # is unresolved and the whole UI fails to load.
        "${CMAKE_CURRENT_SOURCE_DIR}/qml/theme/qmldir"
        "${CMAKE_CURRENT_SOURCE_DIR}/qml/styles/CutProStyle/qmldir"
        ${cutpro_icons}
        ${cutpro_cursors}
        # The application logo, embedded so main.cpp can install it as the window
        # icon before the engine loads - a file beside the .exe would make the
        # icon depend on the deployment layout, and a missing file would silently
        # give back the Qt default.
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/image.png"
)

# Components under qml/components/* use relative paths such as
# "../../assets/icons/...".  Keep a second resource alias at qrc:/qml/assets
# so those paths resolve in the compiled resource system, while the original
# qrc:/assets location remains available to panels and views.
qt_add_resources(cutpro "cutpro_frontend_component_assets"
    PREFIX "/qml"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${cutpro_icons}
)


# The Lumetri grade, compiled to .qsb so the monitor's colour work runs in the
# fragment stage instead of on the GUI thread. qsb bakes SPIR-V plus the D3D11 /
# GL / Metal translations into one file, which is what ShaderEffect loads, so
# these have to be built rather than shipped as source.
#
# Loaded as qrc:/shaders/<name>.frag.qsb by qml/components/lumetri/LumetriGpuGrade.qml.
# PREFIX is "/" and not "/shaders": qt_add_shaders keeps each FILES path as the
# resource alias, so the source directory already supplies the "shaders/" half.
qt_add_shaders(cutpro "cutpro_lumetri_shaders"
    PREFIX "/"
    FILES
        shaders/lumetri_grade.frag
        shaders/lumetri_spatial.frag
        shaders/lumetri_blur.frag
        shaders/lumetri_vignette.frag
)

# --- UI translations ----------------------------------------------------------
# i18n/cutpro_km.ts is the editable XML translation source (the "xml file"); it
# is compiled to a binary cutpro_km.qm and embedded at qrc:/i18n/cutpro_km.qm,
# which Localization::setLanguage() loads for Khmer. English is the source
# language, so it needs no catalogue - with no translator installed the qsTr()
# source strings show through.
#
# qt_add_translations compiles (lrelease) and embeds the .qm as a build step and
# adds an `update_translations` target that runs lupdate ON DEMAND only, so a
# normal build never overwrites the hand-authored Khmer in the .ts.
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_translations(cutpro
    TS_FILES i18n/cutpro_km.ts
    RESOURCE_PREFIX "/i18n")

include(CTest)
if(BUILD_TESTING)
    set(cutpro_ai_test_sources
        src/app/ai/ai_assistant.cpp src/app/ai/ai_assistant.h
        src/app/ai/ai_message_model.cpp src/app/ai/ai_message_model.h
        src/app/mcp/mcp_server.cpp src/app/mcp/mcp_server.h
        src/app/mcp/mcp_catalog.cpp src/app/mcp/mcp_catalog.h
        tests/ai_test_pool.h)
    qt_add_executable(cutpro_ai_tests tests/ai_assistant_tests.cpp ${cutpro_ai_test_sources})
    target_include_directories(cutpro_ai_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(cutpro_ai_tests PRIVATE cutpro_settings cutpro_pool Qt6::Gui Qt6::Network Qt6::Test)
    add_test(NAME cutpro_ai_tests COMMAND cutpro_ai_tests)
    qt_add_executable(cutpro_ai_ui_tests tests/ai_assistant_ui_tests.cpp ${cutpro_ai_test_sources}
        src/app/ui/app_cursor.cpp src/app/ui/app_cursor.h)
    target_include_directories(cutpro_ai_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(cutpro_ai_ui_tests PRIVATE cutpro_backend Qt6::Test Qt6::Quick Qt6::QuickControls2)
    qt_add_resources(cutpro_ai_ui_tests "cutpro_ai_ui_resources"
        PREFIX "/" BASE "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${cutpro_qml} ${cutpro_icons} qml/theme/qmldir)
    qt_add_resources(cutpro_ai_ui_tests "cutpro_ai_component_icons"
        PREFIX "/qml" BASE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${cutpro_icons})
    add_test(NAME cutpro_ai_ui_tests COMMAND cutpro_ai_ui_tests)
    if(WIN32)
        target_link_libraries(cutpro_ai_tests PRIVATE crypt32)
        target_link_libraries(cutpro_ai_ui_tests PRIVATE crypt32)
    endif()
    qt_add_executable(cutpro_export_policy_tests tests/export_policy_tests.cpp)
    target_link_libraries(cutpro_export_policy_tests PRIVATE cutpro_backend Qt6::Test)
    add_test(NAME cutpro_export_policy_tests COMMAND cutpro_export_policy_tests)
    qt_add_executable(cutpro_export_preview_tests tests/export_preview_tests.cpp
        src/app/ui/app_cursor.cpp src/app/ui/app_cursor.h)
    target_compile_definitions(cutpro_export_preview_tests PRIVATE CUTPRO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(cutpro_export_preview_tests PRIVATE cutpro_backend Qt6::Test Qt6::Quick Qt6::QuickControls2)
    add_test(NAME cutpro_export_preview_tests COMMAND cutpro_export_preview_tests)
    qt_add_executable(cutpro_sequence_export_tests tests/sequence_export_tests.cpp)
    target_link_libraries(cutpro_sequence_export_tests PRIVATE cutpro_export cutpro_media Qt6::Gui Qt6::Test)
    add_test(NAME cutpro_sequence_export_tests COMMAND cutpro_sequence_export_tests)
    qt_add_executable(cutpro_noise_removal_tests tests/noise_removal_tests.cpp)
    target_compile_definitions(cutpro_noise_removal_tests PRIVATE CUTPRO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(cutpro_noise_removal_tests PRIVATE cutpro_backend Qt6::Test Qt6::Quick Qt6::QuickControls2)
    add_test(NAME cutpro_noise_removal_tests COMMAND cutpro_noise_removal_tests)
    qt_add_executable(cutpro_recorder_tests tests/recorder_tests.cpp)
    target_include_directories(cutpro_recorder_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(cutpro_recorder_tests PRIVATE CUTPRO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(cutpro_recorder_tests PRIVATE cutpro_backend Qt6::Test Qt6::Quick Qt6::QuickControls2)
    add_test(NAME cutpro_recorder_tests COMMAND cutpro_recorder_tests)
    qt_add_executable(cutpro_mcp_tests tests/mcp_server_tests.cpp
        src/app/mcp/mcp_server.cpp src/app/mcp/mcp_server.h
        src/app/mcp/mcp_catalog.cpp src/app/mcp/mcp_catalog.h)
    target_include_directories(cutpro_mcp_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(cutpro_mcp_tests PRIVATE cutpro_settings Qt6::Gui Qt6::Network Qt6::Test)
    if(WIN32)
        target_link_libraries(cutpro_mcp_tests PRIVATE crypt32)
    endif()
    add_test(NAME cutpro_mcp_tests COMMAND cutpro_mcp_tests)
    qt_add_executable(cutpro_mcp_ui_tests tests/mcp_ui_tests.cpp
        src/app/ui/app_cursor.cpp src/app/ui/app_cursor.h
        src/app/mcp/mcp_server.cpp src/app/mcp/mcp_server.h
        src/app/mcp/mcp_catalog.cpp src/app/mcp/mcp_catalog.h)
    target_include_directories(cutpro_mcp_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(cutpro_mcp_ui_tests PRIVATE CUTPRO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(cutpro_mcp_ui_tests PRIVATE cutpro_backend Qt6::Test Qt6::Quick Qt6::QuickControls2)
    qt_add_resources(cutpro_mcp_ui_tests "cutpro_pool_test_icons"
        PREFIX "/"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES assets/icons/window-close.svg assets/icons/check.svg)
    if(WIN32)
        target_link_libraries(cutpro_mcp_ui_tests PRIVATE crypt32)
    endif()
    add_test(NAME cutpro_mcp_ui_tests COMMAND cutpro_mcp_ui_tests)
    qt_add_executable(cutpro_transcription_batch_tests tests/transcription_batch_tests.cpp)
    target_link_libraries(cutpro_transcription_batch_tests PRIVATE cutpro_subtitles Qt6::Test)
    add_test(NAME cutpro_transcription_batch_tests COMMAND cutpro_transcription_batch_tests)
    set_tests_properties(cutpro_transcription_batch_tests PROPERTIES
        ENVIRONMENT "CUTPRO_BATCH_TEST_PYTHON=${CMAKE_SOURCE_DIR}/dist/Cut Pro 2026/python/python.exe")
    qt_add_executable(cutpro_transcript_translator_tests tests/transcript_translator_tests.cpp)
    target_link_libraries(cutpro_transcript_translator_tests PRIVATE cutpro_subtitles Qt6::Test)
    # The fake worker belongs to this isolated test executable, never the app.
    set_target_properties(cutpro_transcript_translator_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/translator-tests")
    add_test(NAME cutpro_transcript_translator_tests COMMAND cutpro_transcript_translator_tests)
    set_tests_properties(cutpro_transcript_translator_tests PROPERTIES
        ENVIRONMENT "CUTPRO_BATCH_TEST_PYTHON=${CMAKE_SOURCE_DIR}/dist/Cut Pro 2026/python/python.exe"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:cutpro>")
    qt_add_executable(cutpro_backend_tests tests/backend_tests.cpp)
    target_link_libraries(cutpro_backend_tests PRIVATE cutpro_backend Qt6::Test)
    add_test(NAME cutpro_backend_tests COMMAND cutpro_backend_tests)
    set_tests_properties(cutpro_backend_tests PROPERTIES
        ENVIRONMENT "CUTPRO_CACHE_DIR=${CMAKE_BINARY_DIR}/test-cache;CUTPRO_SETTINGS_FILE=${CMAKE_BINARY_DIR}/test-settings.ini")
    # Shared-Qt tests load QML at runtime; scanning the whole source tree for
    # every executable also rescans build/dist copies and stalls reconfiguration.
    # Static Qt still needs import scanning to link its QML plugins.
    get_target_property(cutpro_qt_core_type Qt6::Core TYPE)
    if(cutpro_qt_core_type STREQUAL "SHARED_LIBRARY")
        foreach(test_target IN ITEMS cutpro_export_policy_tests cutpro_export_preview_tests
                cutpro_sequence_export_tests cutpro_noise_removal_tests cutpro_recorder_tests
                cutpro_mcp_tests cutpro_mcp_ui_tests cutpro_transcription_batch_tests
                cutpro_transcript_translator_tests cutpro_backend_tests)
            set_target_properties(${test_target} PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
        endforeach()
    endif()
endif()
