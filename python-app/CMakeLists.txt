cmake_minimum_required(VERSION 3.16...3.31)

project(CutPro VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep the backend .dll next to the launcher .exe so it is found at run time.
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

# --- Backend engine as a shared library (.dll) ------------------------------
# The framework-independent core + the thin Qt bridge. Reusable, unit-testable,
# and versioned independently of the UI launcher.
qt_add_library(cutpro_subtitles SHARED
    src/app/subtitles/khmer_support.cpp
    src/app/subtitles/khmer_support.h
)
target_include_directories(cutpro_subtitles PUBLIC src)
target_link_libraries(cutpro_subtitles PUBLIC Qt6::Core Qt6::Gui)
set_target_properties(cutpro_subtitles PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

qt_add_library(cutpro_backend SHARED
    src/app/media/ffmpeg_runtime.cpp
    src/app/media/media_import_queue.cpp
    src/app/media/media_metadata.cpp
    src/app/media/media_path.cpp
    src/app/media/media_scan.cpp
    src/app/settings/app_settings.cpp
    src/app/caption_style.cpp
    src/app/effects/audio_effect_pipeline.cpp
    src/app/effects/clip_effects.cpp
    src/app/effects/clip_effects_pipeline.cpp
    src/app/effects/effect_registry.cpp
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
    src/app/preview/gui_dispatch.cpp
    src/app/preview/gui_stall_tracer.cpp
    src/app/preview/gui_thread_watchdog.cpp
    src/app/preview/keyframe_index.cpp
    src/app/preview/media_token_registry.cpp
    src/app/preview/native_video_preview_decoder.cpp
    src/app/preview/preview_cache.cpp
    src/app/preview/preview_decode_policy.cpp
    src/app/preview/preview_failure_registry.cpp
    src/app/preview/scrub_frame_service.cpp
    src/app/preview/seek_thumbnail_extractor.cpp
    src/app/preview/startup_warmup.cpp
    src/app/preview/gui_stall_report.cpp
    src/app/preview/timeline_tile_cache.cpp
    src/app/preview/timeline_thumbnail_service.cpp
    src/app/effects/effect_stack.cpp
    src/app/timeline/keyframe_engine.cpp
    src/app/timeline/large_media_policy.cpp
    src/app/timeline/long_media_timeline_handler.cpp
    src/app/timeline/timeline_clip_model.cpp
    src/app/lumetri/color_settings.cpp
    src/app/lumetri/custom_blur_pipeline.cpp
    src/app/lumetri/lumetri_pipeline.cpp
    src/core/ids.cpp
    src/app/core_app/backend.cpp
    src/app/core_app/backend_caption.cpp
    src/app/core_app/backend_selection.cpp
    src/app/core_app/signal_coalescer.cpp
    src/app/settings/backend_settings.cpp
    src/app/project/backend_project_media.cpp
    src/app/project/project_media_actions.cpp
    src/app/preview/media_preview_generator.cpp
    src/app/preview/large_media_preview_job.cpp
    src/app/core_app/project_database.cpp
    src/app/export/sequence_export_builder.cpp
    src/app/subtitles/subtitle_io.cpp
    src/app/subtitles/subtitle_timeline.cpp
    src/app/subtitles/text_to_speech_engine.cpp
    src/app/timeline/timeline_editor.cpp
    src/app/timeline/timeline_clip_binding.cpp
    src/app/timeline/timeline_placement.cpp
    src/app/timeline/timeline_placement_job.cpp
    src/app/effects/video_effect_pipeline.cpp
    src/app/preview/video_preview_helper.cpp
    src/app/subtitles/transcript_translator.cpp
    src/app/subtitles/transcription_plan.cpp
    src/core/version.h
    src/core/ids.h
    src/core/enums.h
    src/core/signal.h
    src/app/core_app/backend.h
    src/app/core_app/signal_coalescer.h
    src/app/project/project_media_actions.h
    src/app/core_app/project_database.h
    src/app/settings/app_settings.h
    src/app/media/ffmpeg_runtime.h
    src/app/media/media_import_queue.h
    src/app/media/media_metadata.h
    src/app/media/media_path.h
    src/app/media/media_scan.h
    src/app/effects/audio_effect_pipeline.h
    src/app/caption_style.h
    src/app/effects/clip_effects.h
    src/app/effects/clip_effects_pipeline.h
    src/app/effects/effect_registry.h
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
    src/app/preview/gui_dispatch.h
    src/app/preview/gui_stall_tracer.h
    src/app/preview/gui_thread_watchdog.h
    src/app/preview/keyframe_index.h
    src/app/preview/media_token_registry.h
    src/app/preview/native_video_preview_decoder.h
    src/app/preview/preview_cache.h
    src/app/preview/preview_decode_policy.h
    src/app/preview/preview_failure_registry.h
    src/app/preview/scrub_frame_service.h
    src/app/preview/seek_thumbnail_extractor.h
    src/app/preview/startup_warmup.h
    src/app/preview/gui_stall_report.h
    src/app/preview/swscale_colorspace.h
    src/app/preview/timeline_tile_cache.h
    src/app/preview/timeline_thumbnail_service.h
    src/app/effects/effect_stack.h
    src/app/timeline/keyframe_engine.h
    src/app/timeline/large_media_policy.h
    src/app/timeline/long_media_timeline_handler.h
    src/app/timeline/timeline_clip_model.h
    src/app/lumetri/color_settings.h
    src/app/lumetri/custom_blur_pipeline.h
    src/app/lumetri/lumetri_pipeline.h
    src/app/preview/media_preview_generator.h
    src/app/preview/large_media_preview_job.h
    src/app/export/sequence_export_builder.h
    src/app/subtitles/subtitle_io.h
    src/app/subtitles/subtitle_timeline.h
    src/app/subtitles/text_to_speech_engine.h
    src/app/timeline/timeline_editor.h
    src/app/timeline/timeline_clip_binding.h
    src/app/timeline/timeline_placement.h
    src/app/timeline/timeline_placement_job.h
    src/app/effects/video_effect_pipeline.h   
    src/app/preview/video_preview_helper.h
    src/app/subtitles/transcript_translator.h
    src/app/subtitles/transcription_plan.h
)
target_include_directories(cutpro_backend PUBLIC src)
target_link_libraries(cutpro_backend PUBLIC
    cutpro_subtitles
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Multimedia
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
)

# Direct FFmpeg linkage is used by the native preview backend. The bundled
# FFmpeg 9 shared build is selected explicitly so the compiler never silently
# falls back to an older PATH installation.
set(CUTPRO_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg-9.0.1-full_build-shared" CACHE PATH "FFmpeg shared build root")
if(EXISTS "${CUTPRO_FFMPEG_ROOT}/include" AND EXISTS "${CUTPRO_FFMPEG_ROOT}/lib/libavcodec.dll.a")
    target_include_directories(cutpro_backend PRIVATE "${CUTPRO_FFMPEG_ROOT}/include")
    target_link_directories(cutpro_backend PRIVATE "${CUTPRO_FFMPEG_ROOT}/lib")
    target_link_libraries(cutpro_backend PRIVATE avformat avcodec avutil swscale swresample)
    target_compile_definitions(cutpro_backend PRIVATE CUTPRO_HAS_NATIVE_FFMPEG=1)
else()
    message(WARNING "FFmpeg 9 shared build not found at ${CUTPRO_FFMPEG_ROOT}; using process preview fallback")
endif()
# Auto-generate the DLL export table (works for MinGW and MSVC) so no per-class
# __declspec macros are needed.
set_target_properties(cutpro_backend PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# GuiStallTracer symbolises the GUI thread's stack when the window freezes. The
# export table above is what makes those frames resolve to real names without a
# PDB, so the two go together.
if(WIN32)
    target_link_libraries(cutpro_backend PRIVATE dbghelp)
endif()

# --- Thin launcher executable -----------------------------------------------
# Owns the (untouched) QML UI resources and links the backend .dll.
qt_add_executable(cutpro
    src/main.cpp
    # The on-demand preview providers need QtQuick's async image provider, which
    # the backend .dll deliberately does not link. They are UI glue, so they live
    # here.
    src/app/preview/timeline_thumbnail_provider.cpp
    src/app/preview/timeline_thumbnail_provider.h
    src/app/preview/waveform_window_provider.cpp
    src/app/preview/waveform_window_provider.h
)
target_link_libraries(cutpro PRIVATE
    cutpro_backend
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::QuickDialogs2
)
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

qt_add_resources(cutpro "cutpro_frontend"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        ${cutpro_qml}
        # extension-less — not matched by *.qml; without it the Theme singleton
        # is unresolved and the whole UI fails to load.
        "${CMAKE_CURRENT_SOURCE_DIR}/qml/theme/qmldir"
        ${cutpro_icons}
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
