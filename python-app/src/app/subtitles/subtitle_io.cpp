#include "app/subtitles/subtitle_io.h"
#include "app/subtitles/khmer_support.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {
// Seconds to whole milliseconds, rounded rather than truncated. Every timestamp
// in this file is a double holding a value that was written in milliseconds, and
// most of those have no exact binary form: 4.720 is stored as 4.71999999999999975,
// so multiplying by a thousand and cutting the remainder yields 4719. Rounding is
// the only conversion that returns the number the file actually said.
qint64 msFromSeconds(double seconds) {
  return qMax<qint64>(0, qRound64(seconds * 1000.0));
}

// The distance kept between a trimmed cue and its successor. A millisecond is
// the finest either format can express, which makes it the exact rendering of a
// half-open [start, nextStart) window in a format whose end timestamp counts as
// displayed: it is the least time that can be taken back and still leave the two
// cues disjoint.
constexpr qint64 kCueGapMs = 1;

double parseTime(const QString &value) {
  static const QRegularExpression re("(\\d+):(\\d{2}):(\\d{2})[,.](\\d{3})");
  const auto m = re.match(value.trimmed());
  return m.hasMatch()
             ? m.captured(1).toInt() * 3600.0 + m.captured(2).toInt() * 60.0 +
                   m.captured(3).toInt() + m.captured(4).toInt() / 1000.0
             : -1;
}
QString formatTime(double seconds) {
  const qint64 ms = msFromSeconds(seconds);
  return QString("%1:%2:%3,%4")
      .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
      .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
      .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
      .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}
// TTML writes a time six different ways. Two of them are clock-time with a
// colon before the frames, which is why this cannot just reuse parseTime above:
// "00:00:04:15" is four seconds and fifteen frames, not four minutes.
//
// Returns -1 for anything unrecognised, so a malformed cue is skipped rather
// than silently placed at zero.
double parseTtmlTime(const QString &value, double frameRate,
                     double tickRate) {
  const QString text = value.trimmed();
  if (text.isEmpty())
    return -1;

  // offset-time: a number and a metric. Checked first because it is the cheap
  // case and cannot be confused with clock-time (no colons).
  static const QRegularExpression offset(
      QStringLiteral("^([0-9]+(?:\\.[0-9]+)?)(h|ms|m|s|f|t)$"));
  const auto offsetMatch = offset.match(text);
  if (offsetMatch.hasMatch()) {
    const double amount = offsetMatch.captured(1).toDouble();
    const QString metric = offsetMatch.captured(2);
    if (metric == QLatin1String("h"))
      return amount * 3600.0;
    if (metric == QLatin1String("m"))
      return amount * 60.0;
    if (metric == QLatin1String("s"))
      return amount;
    if (metric == QLatin1String("ms"))
      return amount / 1000.0;
    if (metric == QLatin1String("f"))
      return frameRate > 0 ? amount / frameRate : -1;
    if (metric == QLatin1String("t"))
      return tickRate > 0 ? amount / tickRate : -1;
    return -1;
  }

  // clock-time: hh:mm:ss, then either .fraction or :frames(.subframes).
  static const QRegularExpression clock(QStringLiteral(
      "^([0-9]{2,}):([0-9]{2}):([0-9]{2})(?:(\\.[0-9]+)|:([0-9]{2,})"
      "(?:\\.[0-9]+)?)?$"));
  const auto clockMatch = clock.match(text);
  if (!clockMatch.hasMatch())
    return -1;
  double seconds = clockMatch.captured(1).toDouble() * 3600.0 +
                   clockMatch.captured(2).toDouble() * 60.0 +
                   clockMatch.captured(3).toDouble();
  if (!clockMatch.captured(4).isEmpty())
    seconds += clockMatch.captured(4).toDouble();
  else if (!clockMatch.captured(5).isEmpty() && frameRate > 0)
    seconds += clockMatch.captured(5).toDouble() / frameRate;
  return seconds;
}

// The one form YouTube emits, and the one every TTML reader accepts.
QString formatTtmlTime(double seconds) {
  const qint64 ms = msFromSeconds(seconds);
  return QString(QStringLiteral("%1:%2:%3.%4"))
      .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
      .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
      .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
      .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace

QVariantList SubtitleIO::readSrt(const QString &path, QString *error) {
  QFile file(path);
  // No QIODevice::Text: we handle CRLF/CR stripping manually below so the
  // raw byte stream is preserved for correct UTF-8 decoding of Khmer/CJK text.
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = file.errorString();
    return {};
  }
  QString data = QString::fromUtf8(file.readAll());
  data.replace("\r\n", "\n");
  data.replace('\r', '\n');
  QVariantList result;
  QStringList textLines;
  double start = -1.0;
  double end = -1.0;
  const auto flush = [&]() {
    if (start >= 0.0 && end > start && !textLines.isEmpty()) {
      QString text = textLines.join('\n').trimmed();
      text.remove(QRegularExpression(QStringLiteral("</?[a-zA-Z][^>]*>")));
      text = text.normalized(QString::NormalizationForm_C);
      if (!text.isEmpty())
        result.append(QVariantMap{{"start", start}, {"end", end}, {"text", text}});
    }
    textLines.clear();
    start = end = -1.0;
  };
  for (QString line : data.split('\n')) {
    if (line.startsWith(QChar(0xFEFF)))
      line.remove(0, 1);
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
      flush();
      continue;
    }
    if (trimmed.contains("-->")) {
      const auto times = trimmed.split("-->");
      if (times.size() == 2) {
        start = parseTime(times[0]);
        end = parseTime(times[1]);
      }
      continue;
    }
    if (start >= 0.0)
      textLines << trimmed;
  }
  flush();
  if (result.isEmpty() && error)
    *error = QStringLiteral("No valid subtitle segments were found.");
  // Same normalisation TTML gets. A rolling caption track does not stop being
  // one because a downloader wrote it as SubRip, and this is also what makes
  // import-then-export idempotent: without it, re-importing a file this app
  // exported brought the touching boundaries back.
  deoverlap(result);
  return result;
}

bool SubtitleIO::writeSrt(const QString &path, const QVariantList &segments,
                          QString *error) {
  QSaveFile file(path);
  // Do NOT use QIODevice::Text: that flag causes CRLF translation on Windows,
  // which breaks libass / FFmpeg subtitle rendering for complex scripts such as
  // Khmer. We write the BOM and line endings explicitly instead.
  if (!file.open(QIODevice::WriteOnly)) {
    if (error)
      *error = file.errorString();
    return false;
  }
  // Write UTF-8 BOM so Windows tools and libass agree on the encoding even
  // when the very first codepoint is a non-ASCII Khmer (or other Unicode) glyph.
  if (file.write("\xEF\xBB\xBF", 3) != 3) {
    if (error)
      *error = file.errorString();
    return false;
  }
  QTextStream out(&file);
  out.setEncoding(QStringConverter::Utf8);
  for (int i = 0; i < segments.size(); ++i) {
    const auto segment = segments[i].toMap();
    // SRT spec uses CRLF between structural lines but LF after the text body
    // keeps libass happy with complex-script (Khmer, Arabic, Indic) content.
    out << i + 1 << "\r\n"
        << formatTime(segment.value("start").toDouble()) << " --> "
        << formatTime(segment.value("end").toDouble()) << "\r\n"
        << KhmerSupport::normalize(segment.value("text").toString())
        << "\r\n\r\n";
  }
  out.flush();
  if (!file.commit()) {
    if (error)
      *error = file.errorString();
    return false;
  }
  return true;
}

QVariantList SubtitleIO::readTtml(const QString &path, QString *error,
                                  QString *language) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = file.errorString();
    return {};
  }

  QXmlStreamReader xml(&file);
  QVariantList result;
  // TTML's own defaults, overridden by ttp:frameRate / ttp:tickRate on <tt> if
  // the document bothers to state them. Only frame- and tick-based timestamps
  // depend on these, which YouTube never emits - but other exporters do.
  double frameRate = 30.0;
  double tickRate = 1.0;
  // Ancestor <body>/<div> begin times. TTML's default time container is "par",
  // so a child's times are relative to its parent's begin; a flat YouTube file
  // simply keeps this at zero the whole way down.
  QList<double> offsets{0.0};

  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.hasError())
      break;

    if (xml.isStartElement()) {
      const QStringView name = xml.name();
      const QXmlStreamAttributes attributes = xml.attributes();

      if (name == QLatin1String("tt")) {
        if (language && attributes.hasAttribute(QStringLiteral("xml:lang")))
          *language = attributes.value(QStringLiteral("xml:lang")).toString();
        bool ok = false;
        const double declaredFps =
            attributes.value(QStringLiteral("ttp:frameRate")).toDouble(&ok);
        if (ok && declaredFps > 0)
          frameRate = declaredFps;
        const double declaredTicks =
            attributes.value(QStringLiteral("ttp:tickRate")).toDouble(&ok);
        if (ok && declaredTicks > 0)
          tickRate = declaredTicks;
        continue;
      }

      if (name == QLatin1String("body") || name == QLatin1String("div")) {
        const double begin = parseTtmlTime(
            attributes.value(QStringLiteral("begin")).toString(), frameRate,
            tickRate);
        offsets.append(offsets.last() + (begin > 0 ? begin : 0.0));
        continue;
      }

      if (name != QLatin1String("p"))
        continue;

      const double base = offsets.last();
      const double begin = parseTtmlTime(
          attributes.value(QStringLiteral("begin")).toString(), frameRate,
          tickRate);
      double end = parseTtmlTime(
          attributes.value(QStringLiteral("end")).toString(), frameRate,
          tickRate);
      if (end < 0) {
        // dur is the other half of the spec, and the half that exporters
        // targeting live captioning tend to use.
        const double duration = parseTtmlTime(
            attributes.value(QStringLiteral("dur")).toString(), frameRate,
            tickRate);
        if (begin >= 0 && duration > 0)
          end = begin + duration;
      }

      // The cue's text, gathered across everything nested inside it: <span> for
      // word-level timing and colour, <br/> for a hard line break. Any timing on
      // those children is deliberately ignored - the transcript's unit is the
      // cue, and a per-word cue list would be unusable in the editor.
      QString text;
      int depth = 1;
      while (depth > 0 && !xml.atEnd()) {
        xml.readNext();
        if (xml.hasError())
          break;
        if (xml.isStartElement()) {
          ++depth;
          if (xml.name() == QLatin1String("br"))
            text.append(QLatin1Char('\n'));
        } else if (xml.isEndElement()) {
          --depth;
        } else if (xml.isCharacters() && !xml.isWhitespace()) {
          text.append(xml.text());
        }
      }

      if (begin < 0 || end <= begin)
        continue;
      // Collapse the runs of spaces and newlines XML indentation leaves behind,
      // but keep the hard breaks <br/> asked for.
      text.replace(QRegularExpression(QStringLiteral("[ \\t]+")),
                   QStringLiteral(" "));
      text = text.trimmed().normalized(QString::NormalizationForm_C);
      if (text.isEmpty())
        continue;
      result.append(QVariantMap{{"start", base + begin},
                                {"end", base + end},
                                {"text", text}});
      continue;
    }

    if (xml.isEndElement() &&
        (xml.name() == QLatin1String("body") ||
         xml.name() == QLatin1String("div")) &&
        offsets.size() > 1)
      offsets.removeLast();
  }

  if (xml.hasError() && result.isEmpty()) {
    if (error)
      *error = QStringLiteral("Line %1: %2")
                   .arg(xml.lineNumber())
                   .arg(xml.errorString());
    return {};
  }
  if (result.isEmpty()) {
    if (error)
      *error = QStringLiteral("No subtitle cues were found in this TTML file.");
    return {};
  }
  deoverlap(result);
  return result;
}

int SubtitleIO::deoverlap(QVariantList &segments) {
  int trimmed = 0;
  for (int i = 0; i + 1 < segments.size(); ++i) {
    QVariantMap cue = segments[i].toMap();
    const qint64 startMs = msFromSeconds(cue.value(QStringLiteral("start")).toDouble());
    const qint64 endMs = msFromSeconds(cue.value(QStringLiteral("end")).toDouble());
    const qint64 nextMs = msFromSeconds(
        segments[i + 1].toMap().value(QStringLiteral("start")).toDouble());

    // Not strictly later: the next cue begins with this one or before it, which
    // is genuine simultaneous dialogue - two speakers in two regions - rather
    // than the next line of a scroll. Shortening this one would delete a
    // speaker.
    //
    // Judged per pair. This used to be a precondition on the whole list, so a
    // single such pair anywhere abandoned the entire pass: a five-hour track
    // with one repeated timestamp in it imported with all 12,000 of its
    // overlaps intact, and nothing said why.
    if (nextMs <= startMs)
      continue;

    // Ends where the successor begins, or later: both are visible at the
    // boundary millisecond. Every pair in a YouTube track is in this state, and
    // it is what makes an imported file double-print its text and lay two clips
    // over the same instant of the timeline.
    const qint64 limit = nextMs - kCueGapMs;
    if (endMs <= limit)
      continue;
    // Never shorter than a millisecond: readSrt requires end > start, so a cue
    // squeezed flat here would disappear on the next import rather than merely
    // being brief. Only reachable when two cues start a millisecond apart.
    cue[QStringLiteral("end")] = qMax(startMs + 1, limit) / 1000.0;
    segments[i] = cue;
    ++trimmed;
  }
  return trimmed;
}

bool SubtitleIO::writeTtml(const QString &path, const QVariantList &segments,
                           QString *error, const QString &language) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error)
      *error = file.errorString();
    return false;
  }

  QXmlStreamWriter xml(&file);
  // No BOM here, unlike the SRT path: TTML is XML, the prolog below declares
  // UTF-8, and a BOM in front of it confuses stricter parsers.
  xml.setAutoFormatting(true);
  xml.setAutoFormattingIndent(0);
  xml.writeStartDocument(QStringLiteral("1.0"));

  xml.writeStartElement(QStringLiteral("tt"));
  if (!language.trimmed().isEmpty())
    xml.writeAttribute(QStringLiteral("xml:lang"), language.trimmed());
  xml.writeAttribute(QStringLiteral("xmlns"),
                     QStringLiteral("http://www.w3.org/ns/ttml"));
  xml.writeAttribute(QStringLiteral("xmlns:ttm"),
                     QStringLiteral("http://www.w3.org/ns/ttml#metadata"));
  xml.writeAttribute(QStringLiteral("xmlns:tts"),
                     QStringLiteral("http://www.w3.org/ns/ttml#styling"));
  xml.writeAttribute(QStringLiteral("xmlns:ttp"),
                     QStringLiteral("http://www.w3.org/ns/ttml#parameter"));
  // The profile YouTube itself declares, which is what makes the result
  // acceptable as a caption upload rather than merely well-formed XML.
  xml.writeAttribute(QStringLiteral("ttp:profile"),
                     QStringLiteral("http://www.w3.org/TR/profile/sdp-us"));

  xml.writeStartElement(QStringLiteral("head"));
  xml.writeStartElement(QStringLiteral("styling"));
  xml.writeStartElement(QStringLiteral("style"));
  xml.writeAttribute(QStringLiteral("xml:id"), QStringLiteral("s1"));
  xml.writeAttribute(QStringLiteral("tts:textAlign"),
                     QStringLiteral("center"));
  xml.writeAttribute(QStringLiteral("tts:extent"), QStringLiteral("90% 90%"));
  xml.writeAttribute(QStringLiteral("tts:origin"), QStringLiteral("5% 5%"));
  xml.writeAttribute(QStringLiteral("tts:displayAlign"),
                     QStringLiteral("after"));
  xml.writeEndElement();
  xml.writeStartElement(QStringLiteral("style"));
  xml.writeAttribute(QStringLiteral("xml:id"), QStringLiteral("s2"));
  xml.writeAttribute(QStringLiteral("tts:fontSize"), QStringLiteral(".72c"));
  xml.writeAttribute(QStringLiteral("tts:backgroundColor"),
                     QStringLiteral("black"));
  xml.writeAttribute(QStringLiteral("tts:color"), QStringLiteral("white"));
  xml.writeEndElement();
  xml.writeEndElement(); // styling
  xml.writeStartElement(QStringLiteral("layout"));
  xml.writeStartElement(QStringLiteral("region"));
  xml.writeAttribute(QStringLiteral("xml:id"), QStringLiteral("r1"));
  xml.writeAttribute(QStringLiteral("style"), QStringLiteral("s1"));
  xml.writeEndElement();
  xml.writeEndElement(); // layout
  xml.writeEndElement(); // head

  xml.writeStartElement(QStringLiteral("body"));
  xml.writeAttribute(QStringLiteral("region"), QStringLiteral("r1"));
  xml.writeStartElement(QStringLiteral("div"));
  for (const QVariant &value : segments) {
    const QVariantMap cue = value.toMap();
    const double start = cue.value(QStringLiteral("start")).toDouble();
    const double end = cue.value(QStringLiteral("end")).toDouble();
    const QString text =
        KhmerSupport::normalize(cue.value(QStringLiteral("text")).toString());
    if (text.trimmed().isEmpty())
      continue;
    xml.writeStartElement(QStringLiteral("p"));
    xml.writeAttribute(QStringLiteral("begin"), formatTtmlTime(start));
    xml.writeAttribute(QStringLiteral("end"), formatTtmlTime(qMax(end, start)));
    xml.writeAttribute(QStringLiteral("style"), QStringLiteral("s2"));
    // A multi-line cue is <br/>-separated in TTML; writeCharacters would emit a
    // literal newline, which every renderer collapses to a space.
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
      if (i > 0)
        xml.writeEmptyElement(QStringLiteral("br"));
      xml.writeCharacters(lines.at(i));
    }
    xml.writeEndElement(); // p
  }
  xml.writeEndElement(); // div
  xml.writeEndElement(); // body
  xml.writeEndElement(); // tt
  xml.writeEndDocument();

  if (xml.hasError()) {
    if (error)
      *error = QStringLiteral("Could not write the TTML document.");
    return false;
  }
  if (!file.commit()) {
    if (error)
      *error = file.errorString();
    return false;
  }
  return true;
}

SubtitleIO::Format SubtitleIO::formatForPath(const QString &path) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (suffix == QLatin1String("ttml") || suffix == QLatin1String("dfxp") ||
      suffix == QLatin1String("xml"))
    return Format::Ttml;
  return Format::Srt;
}

QVariantList SubtitleIO::read(const QString &path, QString *error,
                              QString *language) {
  if (formatForPath(path) == Format::Ttml)
    return readTtml(path, error, language);

  // The extension says SRT. Before believing it, look: browsers save YouTube
  // caption downloads under whatever name the page suggested, and an SRT parser
  // fed XML reports "no valid subtitle segments" rather than the truth.
  QFile probe(path);
  if (probe.open(QIODevice::ReadOnly)) {
    const QString head = QString::fromUtf8(probe.read(512)).trimmed();
    probe.close();
    if (head.startsWith(QLatin1String("<?xml")) ||
        head.startsWith(QLatin1String("<tt")))
      return readTtml(path, error, language);
  }
  return readSrt(path, error);
}

bool SubtitleIO::write(const QString &path, const QVariantList &segments,
                       QString *error) {
  return formatForPath(path) == Format::Ttml
             ? writeTtml(path, segments, error)
             : writeSrt(path, segments, error);
}
