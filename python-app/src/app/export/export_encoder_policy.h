#pragma once

#include <QStringList>
#include <QVariantMap>

// Pure encoder policy. Capability is established by an asynchronous encode of
// these exact options before Backend starts the first render window.
namespace ExportEncoderPolicy {
inline QString format(const QVariantMap &settings) {
  const QString value = settings.value("videoFormat", "h264").toString().toLower();
  return QStringList{"h264", "hevc", "hevc422", "av1", "rle"}.contains(value)
             ? value : QStringLiteral("h264");
}
inline QString software(const QVariantMap &settings) {
  const QString value = format(settings);
  if (value == "hevc" || value == "hevc422") return QStringLiteral("libx265");
  if (value == "av1") return QStringLiteral("libsvtav1");
  if (value == "rle") return QStringLiteral("qtrle");
  return QStringLiteral("libx264");
}
inline QString hardware(const QVariantMap &settings) {
  const QString value = format(settings);
  if (value == "h264") return QStringLiteral("h264_qsv");
  if (value == "hevc") return QStringLiteral("hevc_qsv");
  return {};
}
inline QStringList hardwareArguments(const QVariantMap &settings) {
  const QString codec = hardware(settings);
  if (codec.isEmpty()) return {};
  QString preset = settings.value("encodingSpeed", "medium").toString();
  if (preset == "ultrafast") preset = "veryfast";
  if (!QStringList{"veryfast", "fast", "medium", "slow"}.contains(preset)) preset = "medium";
  const QString quality = settings.value("quality", "high").toString();
  QStringList args{"-c:v", codec, "-pix_fmt", "nv12", "-preset", preset,
                   "-global_quality", quality == "low" ? "28" : quality == "medium" ? "23" : "18"};
  if (codec == "h264_qsv") {
    QString profile = settings.value("profile", "high").toString().toLower();
    if (!QStringList{"baseline", "main", "high"}.contains(profile)) profile = "high";
    args << "-profile:v" << profile;
    if (profile == "baseline") args << "-bf" << "0";
  } else {
    args << "-profile:v" << "main" << "-tag:v" << "hvc1";
  }
  const double ceiling = qBound(0.0, settings.value("videoBitrateMbps", 0).toDouble(), 200.0);
  if (ceiling > 0) {
    // QVBR uses a quality target with a bitrate bound; ICQ (no bitrate) is
    // used when no ceiling was selected. x264's CRF is never sent to QSV.
    const QString rate = QString::number(ceiling, 'f', 2) + "M";
    // Equal target/max rates select CBR in FFmpeg, overriding the quality
    // target and padding low-detail footage to the ceiling.
    args << "-b:v" << QString::number(ceiling * 0.5, 'f', 2) + "M" << "-maxrate" << rate << "-bufsize"
         << QString::number(ceiling * 2, 'f', 2) + "M";
  }
  return args;
}
}
