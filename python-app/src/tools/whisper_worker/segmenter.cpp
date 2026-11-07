#include "segmenter.h"

#include <QByteArray>
#include <QChar>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

#include <cmath>

using Segmenter::Limits;
using Segmenter::RawSegment;
using Segmenter::Word;

namespace {

// A line ends here no matter what. Khmer khan/bariyoosan and the CJK full stops
// are included because Whisper writes them and they carry a period's weight.
const QString kSentenceEndings = QStringLiteral(".?!…។៕"
                                                "。！？");
// Worth taking only when the line is already long. A comma is a breath.
const QString kClauseEndings =
    QStringLiteral(",;:៖、，；：");

// re.sub(r"[ \t]+", " ", "".join(words)).strip()
QString joinedText(const QVector<Word> &group) {
  QString joined;
  for (const Word &word : group)
    joined += word.text;
  QString collapsed;
  collapsed.reserve(joined.size());
  bool inRun = false;
  for (const QChar ch : joined) {
    if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t')) {
      if (!inRun) {
        collapsed.append(QLatin1Char(' '));
        inRun = true;
      }
    } else {
      collapsed.append(ch);
      inRun = false;
    }
  }
  return collapsed.trimmed();
}

struct Piece {
  double start = 0.0;
  double end = 0.0;
  QString text;
  QVector<Word> words;
};

// make_piece: skip a group whose collapsed text is empty (all whitespace).
bool makePiece(const QVector<Word> &group, Piece *out) {
  const QString text = joinedText(group);
  if (text.isEmpty())
    return false;
  out->start = group.first().start;
  out->end = group.last().end;
  out->text = text;
  out->words = group;
  return true;
}

// should_cut: the ordered rule set from whisper_transcribe.py, decided on the
// group as it stands plus the gap to the next word.
bool shouldCut(const QVector<Word> &group, double gapMs, const Limits &limits) {
  const QString text = joinedText(group);
  if (text.isEmpty())
    return false;
  const double durationMs = (group.last().end - group.first().start) * 1000.0;
  if (gapMs >= double(limits.hardPauseMs))
    return true;
  if (durationMs >= double(limits.maxMs) || text.length() >= limits.maxChars)
    return true;
  // Below both floors: too short to stand alone, so never cut here yet.
  if (durationMs < double(limits.minMs) && text.length() < limits.minChars)
    return false;
  const QChar last = text.at(text.length() - 1);
  if (kSentenceEndings.contains(last))
    return true;
  if (gapMs >= double(limits.pauseMs))
    return true;
  if (kClauseEndings.contains(last) &&
      double(text.length()) >= limits.maxChars * 0.6)
    return true;
  return false;
}

// merge_fragments: glue a runt line onto the previous one unless a real pause
// separates them, so long as the join stays within a stretched size budget.
QVector<Piece> mergeFragments(const QVector<Piece> &pieces,
                              const Limits &limits) {
  QVector<Piece> merged;
  for (const Piece &piece : pieces) {
    if (merged.isEmpty()) {
      merged.append(piece);
      continue;
    }
    Piece &previous = merged.last();
    const double durationMs = (piece.end - piece.start) * 1000.0;
    const double gapMs = (piece.start - previous.end) * 1000.0;
    const bool fragment =
        durationMs < double(limits.minMs) && piece.text.length() < limits.minChars;
    const double combinedMs = (piece.end - previous.start) * 1000.0;
    const bool fits =
        combinedMs <= limits.maxMs * 1.35 &&
        double(previous.text.length() + piece.text.length() + 1) <=
            limits.maxChars * 1.25;
    if (fragment && gapMs < double(limits.hardPauseMs) && fits) {
      // No space before a clause mark, a space before anything else.
      const QString separator =
          (!piece.text.isEmpty() && kClauseEndings.contains(piece.text.at(0)))
              ? QString()
              : QStringLiteral(" ");
      previous.text = (previous.text + separator + piece.text).trimmed();
      previous.end = piece.end;
      previous.words += piece.words;
      continue;
    }
    merged.append(piece);
  }
  return merged;
}

// split_stream: walk the word stream, cut where should_cut fires, then let
// merge_fragments reabsorb the runts.
QVector<Piece> splitStream(const QVector<Word> &words, const Limits &limits) {
  QVector<Piece> pieces;
  if (words.isEmpty())
    return pieces;
  QVector<Word> group;
  group.append(words.first());
  for (int i = 1; i < words.size(); ++i) {
    const Word &previous = words.at(i - 1);
    const Word &word = words.at(i);
    const double gapMs = (word.start - previous.end) * 1000.0;
    if (shouldCut(group, gapMs, limits)) {
      Piece piece;
      if (makePiece(group, &piece))
        pieces.append(piece);
      group.clear();
    }
    group.append(word);
  }
  Piece piece;
  if (makePiece(group, &piece))
    pieces.append(piece);
  return mergeFragments(pieces, limits);
}

// pack_word_times: "begin,finish begin,finish ..." in ms relative to the
// line's start, clamped inside the line. Dropped unless there is exactly one
// entry per whitespace word, because the app lights words by split() index.
QString packWordTimes(const Piece &piece) {
  if (piece.words.isEmpty())
    return QString();
  const double start = piece.start;
  const qint64 limit =
      qMax<qint64>(1, qint64(std::llround((piece.end - start) * 1000.0)));
  QStringList packed;
  packed.reserve(piece.words.size());
  for (const Word &word : piece.words) {
    qint64 begin = qint64(std::llround((word.start - start) * 1000.0));
    begin = qBound<qint64>(0, begin, limit);
    qint64 finish = qint64(std::llround((word.end - start) * 1000.0));
    finish = qBound<qint64>(begin, finish, limit);
    packed.append(QStringLiteral("%1,%2").arg(begin).arg(finish));
  }
  const int wordCount = piece.text
                            .split(QRegularExpression(QStringLiteral("\\s+")),
                                   Qt::SkipEmptyParts)
                            .size();
  if (packed.size() != wordCount)
    return QString();
  return packed.join(QLatin1Char(' '));
}

} // namespace

// segmentation_limits(): DEFAULT_LIMITS, then the environment overrides, then
// the job overrides (which win). int(float(x)) truncates toward zero, matching
// Python; a bad or empty value leaves the current one in place.
Limits Segmenter::limitsFromEnvironment(qint64 jobMaxMs, qint64 jobPauseMs) {
  Limits limits;
  const auto envInt = [](const char *name, qint64 current) -> qint64 {
    const QByteArray raw = qgetenv(name).trimmed();
    if (raw.isEmpty())
      return current;
    bool ok = false;
    const double value = QString::fromUtf8(raw).toDouble(&ok);
    if (!ok)
      return current;
    return qMax<qint64>(1, qint64(value));
  };
  limits.maxMs = envInt("CUTPRO_SEGMENT_MAX_MS", limits.maxMs);
  limits.pauseMs = envInt("CUTPRO_SEGMENT_PAUSE_MS", limits.pauseMs);
  if (jobMaxMs > 0)
    limits.maxMs = qMax<qint64>(1, jobMaxMs);
  if (jobPauseMs > 0)
    limits.pauseMs = qMax<qint64>(1, jobPauseMs);
  limits.hardPauseMs = qMax(limits.hardPauseMs, limits.pauseMs);
  return limits;
}

// shape_segments: stream every timed word through split_stream; a wordless
// segment flushes the stream first, then survives on its own raw text. Finally
// clamp adjacent overlaps and pack per-word times.
QVariantList Segmenter::shapeSegments(const QVector<RawSegment> &rawSegments,
                                      const Limits &limits) {
  QVector<Piece> segments;
  QVector<Word> stream;
  const auto flush = [&]() {
    if (stream.isEmpty())
      return;
    segments += splitStream(stream, limits);
    stream.clear();
  };
  for (const RawSegment &raw : rawSegments) {
    if (!raw.words.isEmpty()) {
      stream += raw.words;
      continue;
    }
    flush();
    const QString text = raw.text.trimmed();
    if (!text.isEmpty() && raw.end > raw.start) {
      Piece piece;
      piece.start = raw.start;
      piece.end = raw.end;
      piece.text = text;
      segments.append(piece);
    }
  }
  flush();

  // Never let one subtitle's end run past the next one's start once the times
  // are shown to the millisecond.
  for (int i = 0; i + 1 < segments.size(); ++i) {
    const double nextStart = segments[i + 1].start;
    if (segments[i].end > nextStart)
      segments[i].end = qMax(segments[i].start + 0.001, nextStart);
  }

  QVariantList out;
  out.reserve(segments.size());
  for (const Piece &piece : segments) {
    QVariantMap map;
    map.insert(QStringLiteral("start"), piece.start);
    map.insert(QStringLiteral("end"), piece.end);
    map.insert(QStringLiteral("text"), piece.text);
    const QString wordTimes = packWordTimes(piece);
    if (!wordTimes.isEmpty())
      map.insert(QStringLiteral("wordTimes"), wordTimes);
    out.append(map);
  }
  return out;
}
