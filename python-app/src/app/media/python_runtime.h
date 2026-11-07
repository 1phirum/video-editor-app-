#pragma once

#include <QString>

// Resolves the Python interpreter Cut Pro's worker scripts run under. A shipped
// build carries an embeddable interpreter beside the app (python/pythonw.exe)
// with the cloud-feature packages (edge-tts) already installed, so a client
// needs no system Python. pythonw.exe is preferred over python.exe so launching
// a worker from the GUI never flashes a console window; stdout/stderr are still
// captured through QProcess pipes.
//
// A user who points "Python executable" in Settings at their own interpreter
// (e.g. one with Whisper/torch for offline transcription) overrides the bundle.
// Keeping the lookup here stops every module from carrying its own copy.
class PythonRuntime final {
public:
  // The interpreter to launch. `configured` is the app's "pythonExecutable"
  // setting; empty or "python" means "no explicit choice", which prefers the
  // bundle. Returns an absolute path when one can be resolved, otherwise the
  // best-effort name to hand to QProcess.
  static QString executable(const QString &configured = QString());

  // The interpreter for heavy/offline engines (Whisper, Demucs). These need
  // torch and are never in the bundle, so this deliberately ignores it: an
  // explicit configured interpreter wins, otherwise a system "python" on PATH.
  // This is the interpreter the "install" action pip-installs those packages
  // into, and the one their workers run under.
  static QString systemExecutable(const QString &configured = QString());

  // Absolute path to the bundled interpreter, or empty when none ships beside
  // the app. Prefers pythonw.exe, falls back to python.exe.
  static QString bundledExecutable();

  // True when an interpreter ships beside the app.
  static bool isBundled();
};
