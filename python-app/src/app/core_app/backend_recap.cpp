// Recap -> subtitle, kept out of backend.cpp.
//
// The recap worker (tools/recap_transcript.py) polishes the transcript one line
// per line - clearer, more natural wording, same order, same count. To *replace*
// the existing subtitle with that polished text, each polished line is dropped
// onto the timing of the original line it came from, so the result stays exactly
// in sync with the video. If the line counts ever drift (or there is no timed
// transcript to match), it falls back to cutting the prose into sentence-sized
// cues spread evenly across the transcript's full time span. Either way the result
// becomes a standalone transcript - it carries no clipId/mediaId, so, like an
// imported SRT, a later rebuildSequenceTranscript() with no sources leaves it
// alone.

#include "app/core_app/backend.h"

#include <QChar>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>
#include <QtMath>

namespace {

// Cut recap prose into sentence-sized cues. Latin terminators (. ! ? ...) only
// break when followed by whitespace or the end, so decimals and abbreviations
// stay whole; CJK/Khmer terminators break unconditionally, since they rarely sit
// mid-token and often carry no trailing space. Stray tiny fragments fold back
// into the previous cue so a cue is never a lone quote mark or "Mr.".
QStringList splitRecapIntoCues(const QString &prose) {
  QString marked = prose;
  static const QRegularExpression latin(
      QStringLiteral("([.!?\\x{2026}])(\\s+|$)"));
  marked.replace(latin, QStringLiteral("\\1\\2\n"));
  static const QRegularExpression cjk(
      QStringLiteral("([\\x{3002}\\x{FF01}\\x{FF1F}\\x{17D4}])"));
  marked.replace(cjk, QStringLiteral("\\1\n"));

  QStringList cues;
  const QStringList pieces = marked.split(QChar('\n'), Qt::SkipEmptyParts);
  for (const QString &raw : pieces) {
    const QString piece = raw.trimmed();
    if (piece.isEmpty())
      continue;
    if (!cues.isEmpty() && piece.size() < 12)
      cues.last() += QChar(' ') + piece;
    else
      cues.append(piece);
  }
  if (cues.isEmpty()) {
    const QString whole = prose.trimmed();
    if (!whole.isEmpty())
      cues.append(whole);
  }
  return cues;
}

} // namespace

QString Backend::transcriptText() const {
  QStringList lines;
  lines.reserve(m_transcript.size());
  for (const QVariant &value : m_transcript) {
    const QVariantMap segment = value.toMap();
    const QString text = segment.value("text").toString().trimmed();
    if (text.isEmpty())
      continue;
    const qint64 tenths =
        qMax<qint64>(0, qRound64(segment.value("start").toDouble() * 10.0));
    const qint64 minutes = tenths / 600;
    const qint64 seconds = (tenths / 10) % 60;
    lines.append(QStringLiteral("%1:%2.%3  %4")
                     .arg(minutes, 2, 10, QChar('0'))
                     .arg(seconds, 2, 10, QChar('0'))
                     .arg(tenths % 10)
                     .arg(text));
  }
  return lines.join(QChar('\n'));
}

bool Backend::replaceTranscriptWithRecap(const QString &recapText) {
  const QString prose = recapText.trimmed();
  if (prose.isEmpty()) {
    setError(QStringLiteral("There is no recap to replace the subtitle with."));
    return false;
  }
  if (transcriptionInProgress()) {
    setError(QStringLiteral("Wait for transcription to finish first."));
    return false;
  }

  // The polished recap is one line per original subtitle line. Split it back into
  // lines so each can be dropped onto the timing of the line it came from.
  QStringList polished;
  const QStringList rawLines = prose.split(QChar('\n'), Qt::SkipEmptyParts);
  for (const QString &raw : rawLines) {
    const QString line = raw.trimmed();
    if (!line.isEmpty())
      polished.append(line);
  }

  // The original timed lines the recap was built from: transcript segments that
  // carry text, in order, keeping their start/end.
  QVariantList timed;
  for (const QVariant &value : m_transcript) {
    const QVariantMap segment = value.toMap();
    if (!segment.value("text").toString().trimmed().isEmpty())
      timed.append(segment);
  }

  QVariantList segments;
  if (!timed.isEmpty() && polished.size() == timed.size()) {
    // Exact match: keep every original timestamp and only swap the text, so the
    // polished subtitle stays perfectly in sync with the video. wordTimes are
    // dropped on purpose - they were measured against the old wording.
    segments.reserve(timed.size());
    for (int i = 0; i < timed.size(); ++i) {
      const QVariantMap source = timed.at(i).toMap();
      segments.append(
          QVariantMap{{QStringLiteral("start"), source.value("start")},
                      {QStringLiteral("end"), source.value("end")},
                      {QStringLiteral("text"), polished.at(i)}});
    }
  } else {
    // Line counts drifted, or there was no timed transcript to match against:
    // fall back to sentence-sized cues spread evenly across the full time span.
    const QStringList cues = splitRecapIntoCues(prose);
    if (cues.isEmpty()) {
      setError(QStringLiteral("The recap has no text to place on the subtitle."));
      return false;
    }
    double spanSeconds = 0.0;
    for (const QVariant &value : m_transcript)
      spanSeconds = qMax(spanSeconds, value.toMap().value("end").toDouble());
    const double perCue = spanSeconds > 0.0 ? spanSeconds / cues.size() : 4.0;
    segments.reserve(cues.size());
    for (int i = 0; i < cues.size(); ++i)
      segments.append(QVariantMap{{QStringLiteral("start"), i * perCue},
                                  {QStringLiteral("end"), (i + 1) * perCue},
                                  {QStringLiteral("text"), cues.at(i)}});
  }

  // No mediaId/clipId/sourceIndex on the new segments on purpose: the recap IS the
  // transcript now, the same shape an imported caption has, so a rebuild with no
  // sources will not overwrite it. Drop the per-source segments and coverage so a
  // later rebuildSequenceTranscript() cannot resurrect the old lines.
  m_sourceTranscripts.clear();
  m_sourceTranscriptLanguages.clear();
  m_transcriptCoverageMs.clear();
  m_transcript = segments;
  m_transcriptionStatus =
      QStringLiteral("Replaced subtitle with recap (%1 lines)")
          .arg(segments.size());
  markDirty();
  recordAction(QStringLiteral("subtitle-recap-replace"),
               QVariantMap{{"segments", segments.size()}});
  emit transcriptChanged();

  // Push the recap onto the subtitle track, replacing every existing cue. This
  // clears the error banner and republishes the timeline on success.
  addTranscriptToTimeline();
  return true;
}
