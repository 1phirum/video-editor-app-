#pragma once

#include <QString>
#include <QStringList>

#include "core/module_api.h"

class DiagnosticsBridge;
class QObject;

// The diagnostics layer without a scene.
//
// There was a GUI for this - an F12 overlay and a Ctrl+Shift+D report window -
// and both were measured slowing down the app they were measuring: the overlay
// polled three C++ reports into ~40 live Text items every 700 ms, and the report
// window cost a 400 ms GUI-thread stall re-laying out its text on every resize.
// A frame-rate instrument that eats frames cannot be trusted, and the numbers it
// was showing were the numbers it was changing.
//
// So the same evidence goes to the console and to a file instead. Nothing is in
// the scene, nothing polls, and the output is greppable - which is what it was
// actually being used for.
//
// Three entry points, in the order they get used:
//
//  * handleCommandLine() - `cutpro.exe --diagnose` reads the newest report from
//    disk and prints it, without starting the editor. This is how a report gets
//    read after the run that produced it has ended;
//  * printReport() - Ctrl+Shift+D in the running app: the whole report to the
//    console, and the same text to a file;
//  * startTicker() - CUTPRO_DIAGNOSE_MS=n prints the one-line verdict every n
//    ms, so a session can be watched from the terminal with no keypresses. The
//    tick reads counters only and never walks the scene, so leaving it on does
//    not repeat the mistake above.
class CUTPRO_SCENE_API DiagnosticsCli final {
public:
  // True when this process was a CLI invocation: main() should return *exitCode
  // and start nothing. Must be called after the application name is set, since
  // the report directory is derived from it.
  static bool handleCommandLine(const QStringList &arguments, int *exitCode);

  // Prints the full report to stdout and writes the identical text next to the
  // crash reports. Returns the file path, empty if the file could not be written
  // - the console half still happened in that case, which is the half that
  // matters when a disk is full.
  static QString printReport(DiagnosticsBridge *bridge, const QString &note);

  // Starts the CUTPRO_DIAGNOSE_MS ticker if the variable is set. Owner parents
  // the timer, so it dies with the engine.
  static void startTicker(DiagnosticsBridge *bridge, QObject *owner);

  // One line to stdout, flushed. Flushing matters: the interesting case is a run
  // that ends in a freeze, and a buffered line is a line that never arrives.
  static void print(const QString &text);

  // Below this the ticker prints more often than the app draws.
  static constexpr int kMinTickMs = 500;
};
