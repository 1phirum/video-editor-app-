#pragma once

// Cross-.dll visibility for the engine modules.
//
// This exists because of a real regression, and the mechanism is worth writing
// down: splitting cutpro_backend.dll into one .dll per subsystem silently broke
// every new-style connect that crossed a module boundary.
//
//     QObject::connect(MediaImportQueue, Backend): signal not found
//
// A PMF connect - connect(&queue, &MediaImportQueue::progressChanged, ...) -
// does not look the signal up by name. moc generates an IndexOfMethod branch
// that *compares member-function pointers* against &Class::signal for each
// signal, and returns the index of whichever matches. On Windows, taking the
// address of a function that lives in another .dll gives the address of the
// local import thunk unless the declaration says __declspec(dllimport); the
// comparison inside the owning .dll then sees the real address, the two differ,
// IndexOfMethod returns -1, and connect() logs the line above and hands back a
// null connection. Nothing throws, nothing fails to link: the signal simply
// never arrives. Media import spun at 0% forever because all four of its
// connects to Backend were dead this way.
//
// WINDOWS_EXPORT_ALL_SYMBOLS already writes each .dll's export table, so the
// export half was never the problem - only the import half was missing. That is
// why the macro expands to dllimport when consuming and to *nothing* when
// building the owning module: adding dllexport on top of the CMake-generated .def
// is what provokes duplicate-export errors from ld, and it would buy nothing the
// .def has not already done.
//
// CMake defines <target>_EXPORTS while compiling a SHARED library's own sources,
// which is what lets one header serve both sides without the consumer needing to
// know which module owns a class.
//
// Rule: every class with Q_OBJECT that lives in a module .dll gets its module's
// macro. Anything whose signals are only ever connected by name from QML would
// survive without it, but a PMF connect added later would fail the same silent
// way, so the annotation goes on the class and not on the connect site.

#if defined(_WIN32)
#define CUTPRO_IMPORTED __declspec(dllimport)
#else
// ELF has no thunk problem - a default-visibility symbol has one address across
// the whole process - so there is nothing to correct here.
#define CUTPRO_IMPORTED
#endif

#if defined(cutpro_core_EXPORTS)
#define CUTPRO_CORE_API
#else
#define CUTPRO_CORE_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_diagnostics_EXPORTS)
#define CUTPRO_DIAGNOSTICS_API
#else
#define CUTPRO_DIAGNOSTICS_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_settings_EXPORTS)
#define CUTPRO_SETTINGS_API
#else
#define CUTPRO_SETTINGS_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_media_EXPORTS)
#define CUTPRO_MEDIA_API
#else
#define CUTPRO_MEDIA_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_subtitles_EXPORTS)
#define CUTPRO_SUBTITLES_API
#else
#define CUTPRO_SUBTITLES_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_effects_EXPORTS)
#define CUTPRO_EFFECTS_API
#else
#define CUTPRO_EFFECTS_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_lumetri_EXPORTS)
#define CUTPRO_LUMETRI_API
#else
#define CUTPRO_LUMETRI_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_timeline_EXPORTS)
#define CUTPRO_TIMELINE_API
#else
#define CUTPRO_TIMELINE_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_preview_EXPORTS)
#define CUTPRO_PREVIEW_API
#else
#define CUTPRO_PREVIEW_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_export_EXPORTS)
#define CUTPRO_EXPORT_API
#else
#define CUTPRO_EXPORT_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_backend_EXPORTS)
#define CUTPRO_BACKEND_API
#else
#define CUTPRO_BACKEND_API CUTPRO_IMPORTED
#endif

#if defined(cutpro_scene_EXPORTS)
#define CUTPRO_SCENE_API
#else
#define CUTPRO_SCENE_API CUTPRO_IMPORTED
#endif
