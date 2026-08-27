#pragma once

#include <QByteArray>
#include <QString>

// Path encoding for the in-process FFmpeg (libav*) calls.
//
// QFile::encodeName() runs the string through the local 8-bit codec, which on
// Windows is the ANSI code page. libavformat's file protocol converts the byte
// string it receives with utf8towchar() before opening it, so an ANSI-encoded
// name loses every character outside the code page: a folder such as
// "D:/វីដេអូ/clip.mp4" or "D:/Видео/clip.mp4" reaches libav as "D:/???/clip.mp4"
// and avformat_open_input() fails with "No such file or directory" for a file
// that plainly exists. The decoder then reports an error or publishes no frame
// at all, which is the folder-name failure this class removes.
//
// Only libav needs this. QProcess passes wide-character arguments on Windows and
// is unaffected.
class MediaPath {
public:
  // Windows stops resolving classic paths near MAX_PATH. Anything at or beyond
  // this length is handed to libav in extended-length form instead.
  static constexpr int kLongPathThreshold = 240;

  // UTF-8 bytes for avformat_open_input(). The result carries an explicit
  // "file:" protocol so a name containing ':' or starting with '-' can never be
  // parsed as a protocol or as an FFmpeg option.
  static QByteArray toFfmpegUrl(const QString &path);

  // UTF-8 bytes without the protocol prefix, for APIs that want a plain file
  // name rather than a URL.
  static QByteArray toFfmpegFileName(const QString &path);

  // Windows extended-length form ("\\?\D:\...") for long paths. Extended paths
  // are only valid with native separators and no "." / ".." segments, so the
  // input is resolved before the prefix is applied. Non-Windows builds and
  // short paths are returned with forward slashes and no prefix.
  static QString nativeDecodePath(const QString &path);

  // True when libav can be expected to open the file: it exists, is a readable
  // regular file, is not empty, and survives the UTF-8 round trip. Import and
  // preview skip anything that fails here instead of handing libav a name it
  // cannot open and treating the failure as a decode error.
  static bool isDecodable(const QString &path, QString *reason = nullptr);

  // Case-folded canonical key for duplicate detection. Windows paths are
  // compared case-insensitively; symlinks and "." / ".." are resolved so the
  // same file reached through two different spellings collapses to one entry.
  static QString duplicateKey(const QString &path);
};
