cmake_minimum_required(VERSION 3.16...3.31)

project(CutPro VERSION 0.1.2 LANGUAGES CXX)

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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia QuickDialogs2 Network Sql Concurrent Test)

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
endfunction()

# cutpro_core - ids, enums, version, caption style. No dependencies, by rule.
cutpro_module(cutpro_core SOURCES
    src/app/caption_style.cpp
    src/core/ids.cpp
    src/app/caption_style.h
    src/core/enums.h
    src/core/ids.h
    src/core/module_api.h
    src/core/signal.h
    src/core/version.h
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
    src/app/diagnostics/model_guard.cpp
    src/app/diagnostics/playback_trace.cpp
    src/app/preview/gui_dispatch.cpp
    src/app/preview/gui_stall_report.cpp
    src/app/preview/gui_stall_tracer.cpp
    src/app/preview/gui_thread_watchdog.cpp
    src/app/diagnostics/crash_channel.h
    src/app/diagnostics/crash_reporter_host.h
    src/app/diagnostics/model_guard.h
    src/app/diagnostics/playback_trace.h
    src/app/preview/gui_dispatch.h
    src/app/preview/gui_stall_report.h
    src/app/preview/gui_stall_tracer.h
    src/app/preview/gui_thread_watchdog.h
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
    src/app/media/ffmpeg_runtime.cpp
    src/app/media/media_import_queue.cpp
    src/app/media/media_metadata.cpp
    src/app/media/media_path.cpp
    src/app/media/media_scan.cpp
    src/app/media/ffmpeg_runtime.h
    src/app/media/media_import_queue.h
    src/app/media/media_metadata.h
    src/app/media/media_path.h
    src/app/media/media_scan.h
)

cutpro_module(cutpro_subtitles SOURCES
    src/app/subtitles/khmer_support.cpp
    src/app/subtitles/subtitle_io.cpp
    src/app/subtitles/subtitle_timeline.cpp
    src/app/subtitles/text_to_speech_engine.cpp
    src/app/subtitles/transcript_translator.cpp
    src/app/subtitles/transcription_plan.cpp
    src/app/subtitles/khmer_support.h
    src/app/subtitles/subtitle_io.h
    src/app/subtitles/subtitle_timeline.h
    src/app/subtitles/text_to_speech_engine.h
    src/app/subtitles/transcript_translator.h
    src/app/subtitles/transcription_plan.h
)

cutpro_module(cutpro_effects SOURCES
    src/app/effects/audio_effect_pipeline.cpp
    src/app/effects/clip_effects.cpp
    src/app/effects/clip_effects_pipeline.cpp
    src/app/effects/effect_registry.cpp
    src/app/effects/effect_stack.cpp
    src/app/effects/video_effect_pipeline.cpp
    src/app/effects/audio_effect_pipeline.h
    src/app/effects/clip_effects.h
    src/app/effects/clip_effects_pipeline.h
    src/app/effects/effect_registry.h
    src/app/effects/effect_stack.h
    src/app/effects/video_effect_pipeline.h
)

cutpro_module(cutpro_lumetri SOURCES
    src/app/lumetri/color_settings.cpp
    src/app/lumetri/custom_blur_pipeline.cpp
    src/app/lumetri/lumetri_pipeline.cpp
    src/app/lumetri/color_settings.h
    src/app/lumetri/custom_blur_pipeline.h
    src/app/lumetri/lumetri_pipeline.h
)

cutpro_module(cutpro_timeline SOURCES
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
    src/app/preview/seek_thumbnail_extractor.cpp
    src/app/preview/startup_warmup.cpp
    src/app/preview/timeline_preview_prefetcher.cpp
    src/app/preview/timeline_thumbnail_service.cpp
    src/app/preview/timeline_tile_cache.cpp
    src/app/preview/video_preview_helper.cpp
    src/app/preview/audio_peak_builder.h
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
    LINK cutpro_effects cutpro_lumetri cutpro_subtitles
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
cutpro_module(cutpro_backend
    LINK
    cutpro_core
    cutpro_diagnostics
    cutpro_settings
    cutpro_media
    cutpro_subtitles
    cutpro_effects
    cutpro_lumetri
    cutpro_timeline
    cutpro_preview
    cutpro_export
    SOURCES
    src/app/core_app/backend.cpp
    src/app/core_app/backend_caption.cpp
    src/app/core_app/backend_selection.cpp
    src/app/core_app/project_database.cpp
    src/app/core_app/signal_coalescer.cpp
    src/app/project/backend_project_media.cpp
    src/app/project/project_media_actions.cpp
    src/app/settings/backend_settings.cpp
    src/app/core_app/backend.h
    src/app/core_app/project_database.h
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
    src/app/diagnostics/item_tree_census.cpp
    src/app/diagnostics/diagnostics_bridge.h
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

# --- Thin launcher executable -----------------------------------------------
# Owns the QML UI resources and links the engine modules.
qt_add_executable(cutpro
    src/main.cpp
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
)
target_link_libraries(cutpro PRIVATE
    cutpro_backend
    cutpro_scene
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::QuickDialogs2
)
# The reporter is useless if it is not beside the app it watches, so building the
# app builds it. CrashReporterHost looks for it in applicationDirPath().
add_dependencies(cutpro cutpro_crash_report)

add_custom_command(TARGET cutpro POST_BUILD
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
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/translate_transcript.py"
        "$<TARGET_FILE_DIR:cutpro>/tools/translate_transcript.py"
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
        ${cutpro_icons}
        ${cutpro_cursors}
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

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(cutpro_backend_tests tests/backend_tests.cpp)
    target_link_libraries(cutpro_backend_tests PRIVATE cutpro_backend Qt6::Test)
    add_test(NAME cutpro_backend_tests COMMAND cutpro_backend_tests)
    set_tests_properties(cutpro_backend_tests PROPERTIES
        ENVIRONMENT "CUTPRO_CACHE_DIR=${CMAKE_BINARY_DIR}/test-cache;CUTPRO_SETTINGS_FILE=${CMAKE_BINARY_DIR}/test-settings.ini")
endif()
