#pragma once
#include <QString>
#include <QVariantList>

// Reading and writing the transcript as a subtitle file.
//
// SRT is the working format: it is what the transcript is exported as by
// default, what libass and FFmpeg burn in, and what everything downstream
// expects. TTML is here because it is what YouTube hands you when you download
// a video's captions, and re-typing five hours of dialogue is not a workflow.
//
// The two are not symmetric in difficulty. SRT is line-oriented and has one
// time format. TTML is XML with inherited time containers, six ways to write a
// timestamp, and - in YouTube's case - deliberately overlapping cues, because
// its player scrolls two lines at a time. readTtml deals with all of that so
// the rest of the app keeps seeing the same {start, end, text} maps it always
// has.
class SubtitleIO {
public:
  enum class Format {
    Srt,
    Ttml,
  };

  // By file extension: .ttml, .dfxp and .xml are TTML, everything else is SRT.
  // SRT is the fallback rather than an error because it is the normal case.
  static Format formatForPath(const QString &path);

  // Reads either format, choosing by extension and then, if that says SRT, by
  // sniffing the first bytes for an XML prolog or a <tt> root. A YouTube
  // download that arrived as .xml, or one a browser saved as .srt, still
  // imports - the file's contents are better evidence than its name.
  static QVariantList read(const QString &path, QString *error,
                           QString *language = nullptr);
  // Writes either format, choosing by extension. Callers that mean one
  // specific format should say so with writeSrt/writeTtml.
  static bool write(const QString &path, const QVariantList &segments,
                    QString *error);

  static QVariantList readSrt(const QString &path, QString *error);
  static bool writeSrt(const QString &path, const QVariantList &segments,
                       QString *error);

  // `language` receives the document's xml:lang when the file declares one, so
  // an imported YouTube caption track arrives already knowing what language it
  // is in and the translate step does not have to ask. Optional.
  static QVariantList readTtml(const QString &path, QString *error,
                               QString *language = nullptr);
  // Emits the same shape YouTube itself produces (the SDP-US profile, one
  // region, one style), so the result can be uploaded back to a video or opened
  // by any TTML-aware player. `language` is written as xml:lang; empty is
  // allowed and simply omits the declaration.
  static bool writeTtml(const QString &path, const QVariantList &segments,
                        QString *error, const QString &language = QString());

  // Clamps each cue's end to just before the next cue's start, and returns how
  // many cues it had to shorten.
  //
  // YouTube's automatic captions overlap on purpose: every cue runs about two
  // seconds into the next one so the player can scroll two lines at once - cue
  // n's end is literally cue n+2's start. Imported as authored, a five-hour
  // track becomes 12,724 cues that are each visible during their successor,
  // which on a timeline means two subtitle clips covering every instant and,
  // burned in, means double-printed text. The cues are already sequential prose,
  // so trimming the overlap loses nothing that was ever meant to be seen twice.
  //
  // "Just before" and not "at": a cue ending on the millisecond its successor
  // begins is still two cues laying claim to that millisecond, which is what
  // subtitle checkers report as an overlap and what left every exported pair in
  // a 12,724-cue file touching. The gap taken is one millisecond, the least
  // either format can express.
  //
  // Pairs whose starts are not strictly increasing are left alone: that is
  // genuine simultaneous dialogue - two speakers in two regions - and clamping
  // it would be destroying real content rather than an animation artefact. The
  // decision is made per pair, so one such pair cannot excuse the rest of the
  // file.
  static int deoverlap(QVariantList &segments);
};
