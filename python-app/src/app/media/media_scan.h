#pragma once

#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <atomic>

// Bounded, cancellable expansion of an import selection into a media file list.
//
// The previous implementation ran a recursive QDirIterator on the GUI thread
// with no limits at all. Dropping a folder such as C:\Users or an external
// drive root froze the window for as long as the walk took, allocated a
// QStringList entry for every match, and had no way to stop - and a directory
// symlink or NTFS junction pointing at one of its own ancestors made the walk
// never finish.
//
// This scanner is breadth-first and explicitly bounded:
//  - every visited directory is recorded by canonical path, so a symlink or
//    junction loop is walked once and then skipped;
//  - depth, file count, entry count and elapsed time all have ceilings, and
//    hitting one truncates the scan instead of continuing;
//  - cancellation is checked per entry, so an abandoned import stops promptly;
//  - breadth-first order means a truncated scan still returns the files nearest
//    the folders the user actually picked, not whatever happened to sort first
//    inside the deepest branch.
//
// Nothing here touches Qt GUI state, so it is safe to run on a worker thread.
class MediaScan final {
public:
  struct Limits {
    // Deep enough for real media libraries (show/season/disc/stream), shallow
    // enough that a runaway tree cannot cost minutes.
    int maximumDepth = 8;
    // A project bin beyond this is not a usable import; the user picked the
    // wrong folder.
    int maximumFiles = 20000;
    // Directory entries examined, media or not. Bounds the cost of a folder
    // full of non-media files.
    int maximumEntries = 400000;
    int timeBudgetMs = 15000;
  };

  struct Result {
    // Absolute, cleaned, de-duplicated, in breadth-first discovery order.
    QStringList files;
    int directoriesVisited = 0;
    int entriesVisited = 0;
    int skippedUnsupported = 0;
    // Zero-byte, unreadable or otherwise un-openable files.
    int skippedUnreadable = 0;
    int skippedDuplicates = 0;
    bool hitFileLimit = false;
    bool hitEntryLimit = false;
    bool hitDepthLimit = false;
    bool hitTimeBudget = false;
    bool cancelled = false;
    // Directories that exist but could not be listed, capped at a handful so
    // an unreadable tree cannot produce a huge message.
    QStringList unreadableDirectories;

    bool truncated() const {
      return hitFileLimit || hitEntryLimit || hitDepthLimit || hitTimeBudget;
    }
    // A single user-facing sentence describing what was left out, or an empty
    // string when the scan was complete.
    QString truncationMessage() const;
  };

  // Extension classification, shared by the scanner and the media probe so the
  // two can never disagree about what is importable.
  static QString kindForSuffix(const QString &suffix);
  static QString kindForFile(const QFileInfo &info);
  static bool isSupported(const QFileInfo &info);

  // Accepts plain paths and file:// URLs, in any mix.
  static QString normalizeInput(const QString &value);

  static Result expand(const QStringList &paths, const Limits &limits,
                       const std::atomic_bool *cancel = nullptr);
  // GCC will not accept `const Limits & = {}` as a default argument for a
  // nested aggregate whose members have initialisers, so the common case gets
  // its own overload instead.
  static Result expand(const QStringList &paths) {
    return expand(paths, Limits{}, nullptr);
  }

private:
  static constexpr int kMaxReportedUnreadableDirectories = 5;
};
