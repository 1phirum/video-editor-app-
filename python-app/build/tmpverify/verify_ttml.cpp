#include "app/subtitles/subtitle_io.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  out.setEncoding(QStringConverter::Utf8);
  const QString in = QString::fromLocal8Bit(argv[1]);
  QString err, lang;
  QVariantList cues = SubtitleIO::read(in, &err, &lang);
  out << "cues=" << cues.size() << " lang=" << lang << " err=" << err << "\n";
  if (cues.isEmpty()) return 1;
  auto show = [&](int i) {
    const auto m = cues[i].toMap();
    out << "  [" << i << "] " << m.value("start").toDouble() << " -> "
        << m.value("end").toDouble() << " | " << m.value("text").toString() << "\n";
  };
  for (int i = 0; i < 4; ++i) show(i);
  show(cues.size() - 1);
  int bad = 0; double prev = -1;
  for (const auto &v : cues) {
    const auto m = v.toMap();
    const double s = m.value("start").toDouble(), e = m.value("end").toDouble();
    if (e <= s || s < prev) ++bad;
    prev = e;
  }
  out << "overlaps_or_inversions=" << bad << "\n";
  const QString srt = argv[2], ttml = argv[3];
  out << "writeSrt=" << SubtitleIO::write(srt, cues, &err) << " " << err << "\n";
  err.clear();
  out << "writeTtml=" << SubtitleIO::writeTtml(ttml, cues, &err, lang) << " " << err << "\n";
  QString err2, lang2;
  QVariantList back = SubtitleIO::read(ttml, &err2, &lang2);
  out << "roundtrip cues=" << back.size() << " lang=" << lang2 << " err=" << err2 << "\n";
  int diff = 0;
  for (int i = 0; i < qMin(back.size(), cues.size()); ++i) {
    const auto a = cues[i].toMap(), b = back[i].toMap();
    if (a.value("text").toString() != b.value("text").toString()) ++diff;
    if (qAbs(a.value("start").toDouble() - b.value("start").toDouble()) > 0.0015) ++diff;
    if (qAbs(a.value("end").toDouble() - b.value("end").toDouble()) > 0.0015) ++diff;
  }
  out << "roundtrip_mismatches=" << diff << "\n";
  return 0;
}
