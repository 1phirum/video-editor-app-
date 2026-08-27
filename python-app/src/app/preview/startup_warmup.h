#pragma once

#include <QStringList>

// Pays, on a worker thread, the one-time costs that Qt would otherwise charge to
// the GUI thread at the worst possible moment.
//
// This file exists because of a measurement, not a hunch. The stall tracer
// caught the GUI thread here for 2049 ms during startup:
//
//   QQuickControlPrivate::executeContentItem
//     -> QQmlObjectCreator::finalize
//       -> QQuickIconLabel::componentComplete
//         -> QQuickIconImage::load
//           -> QQuickPixmap::load
//             -> QImageReader::supportedImageFormats
//               -> QFactoryLoader::instance
//                 -> QLibrary::resolve -> LoadLibraryExW -> LdrLoadDll (Qt6Svg)
//
// The first icon anywhere in the UI asks QImageReader which formats it can read.
// Answering that means scanning the imageformats plugin directory and calling
// LoadLibraryExW on every plugin in it - cold disk reads, under the loader lock,
// inside a synchronous QML component completion. Nothing about it depends on the
// GUI thread; it only happens there because that is who asked first.
//
// So we ask first, earlier, somewhere else. The registry is process-wide and
// guarded by its own mutex, so whichever thread populates it, everyone else gets
// the finished answer. Worst case the warm-up has not finished when QML gets
// there and the GUI thread waits on the mutex for the remainder - still no worse
// than today, and in practice the head start is the whole cost.
//
// Deliberately not warmed here: QFontDatabase (GUI-thread affine in Qt 6) and
// QMediaDevices (its Windows backend is COM-apartment affine, so priming it on a
// thread that then exits would leave the GUI thread holding a dead object). The
// audio device open is dealt with in FfmpegPreviewDecoder instead, where it can
// stay on the GUI thread and simply be paid once.
class StartupWarmup final {
public:
  // Returns immediately. Call once from main(), after QGuiApplication exists and
  // before the QML engine loads - that gap is the head start.
  static void begin();

  // Blocks until the warm-up thread has finished, then lets it go. Called at
  // shutdown so the process does not exit underneath a thread that is still
  // inside LdrLoadDll.
  static void finish();

  // What was primed and how long each step took, for the debug overlay. Merged
  // into Backend::previewDecodeStatistics next to the watchdog's own numbers so
  // one panel answers "was startup slow, and was it this?".
  static QStringList report();
};
