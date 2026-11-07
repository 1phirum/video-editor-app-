#include "app/media/noise_removal.h"
#include "app/media/ffmpeg_runtime.h"
#include "app/media/media_path.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

NoiseRemoval::NoiseRemoval(QObject *parent) : QObject(parent) {
#ifdef Q_OS_WIN
  m_process.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
      });
#endif
  connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, &NoiseRemoval::complete);
  connect(&m_process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
              complete(-1, QProcess::CrashExit);
          });
  connect(&m_process, &QProcess::readyReadStandardOutput, this, [this]() {
    m_progressBuffer += m_process.readAllStandardOutput();
    int end = -1;
    while ((end = m_progressBuffer.indexOf('\n')) >= 0) {
      const QByteArray line = m_progressBuffer.left(end).trimmed();
      m_progressBuffer.remove(0, end + 1);
      if (line.startsWith("out_time_us=") && m_expectedMs > 0) {
        m_progress = qBound(0.0, line.mid(12).toDouble() /
                                    (m_expectedMs * 1000.0), 0.99);
        emit changed();
      }
    }
  });
}

NoiseRemoval::~NoiseRemoval() {
  m_process.disconnect(this);
  if (m_process.state() != QProcess::NotRunning) {
    m_process.kill();
    m_process.waitForFinished(1000);
  }
  if (!m_temporary.isEmpty())
    QFile::remove(m_temporary);
}

QString NoiseRemoval::modelPath() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(QStringLiteral("models/noise-removal/std.rnnn"));
}

QString NoiseRemoval::sourcePath(const QVariantMap &clip,
                                 const QVariantMap &media) {
  const QVariantMap effects = clip.value("effects").toMap();
  const QString stem = effects.value("demucsPath").toString();
  if (effects.value("vocalRemoval").toBool() && QFileInfo::exists(stem))
    return stem;
  return media.value("path").toString();
}

QString NoiseRemoval::replacementPath(const QVariantMap &clip,
                                      const QVariantMap &media) {
  const QString input = sourcePath(clip, media);
  const QVariantMap effects = clip.value("effects").toMap();
  const QString clean = effects.value("noiseRemovalPath").toString();
  // A later voice change or vocal separation must not play an obsolete render.
  if (effects.value("noiseRemoval").toBool() &&
      effects.value("noiseRemovalInputPath").toString() == input &&
      QFileInfo::exists(clean))
    return clean;
  return input == media.value("path").toString() ? QString() : input;
}

bool NoiseRemoval::start(const QString &input, int strength,
                         qint64 sourceInMs, qint64 durationMs,
                         qint64 sourceDurationMs) {
  if (m_busy || strength < 0 || strength > 100 || sourceInMs < 0 ||
      durationMs < 0 || !MediaPath::isDecodable(input))
    return false;
  if (strength > 0 && !QFileInfo::exists(modelPath()))
    return false;
  const QFileInfo source(input);
  // Version includes the model and delay compensation. Source modification
  // invalidates the cache even when a file is replaced at the same path.
  const QByteArray key = QStringLiteral("rnnoise-std-v4/%1/%2/%3/%4/%5/%6")
      .arg(MediaPath::duplicateKey(input)).arg(source.size())
      .arg(source.lastModified().toMSecsSinceEpoch()).arg(strength)
      .arg(sourceInMs).arg(durationMs).toUtf8();
  const QString root = QDir(QStandardPaths::writableLocation(
      QStandardPaths::AppDataLocation)).filePath("noise-removal");
  if (!QDir().mkpath(root))
    return false;
  m_output = QDir(root).filePath(QString::fromLatin1(
      QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex()) + ".wav");
  m_busy = true;
  m_cancelled = false;
  m_progress = 0;
  m_expectedMs = durationMs > 0 ? durationMs : sourceDurationMs;
  m_progressBuffer.clear();
  m_temporary.clear();
  emit changed();
  if (QFileInfo(m_output).size() > 44) {
    // Cache completion is asynchronous too, so cancellation and queue pumping
    // have the same ordering as a real render.
    QTimer::singleShot(0, this, [this]() { complete(0, QProcess::NormalExit); });
    return true;
  }
  m_temporary = QDir(root).filePath(
      QUuid::createUuid().toString(QUuid::WithoutBraces) + ".partial.wav");
  const qint64 preRollMs = durationMs > 0 ? qMin<qint64>(sourceInMs, 500) : 0;
  QStringList args{"-hide_banner", "-loglevel", "error", "-nostdin", "-nostats",
                   "-progress", "pipe:1", "-y"};
  if (durationMs > 0)
    args << "-ss" << QString::number((sourceInMs - preRollMs) / 1000.0, 'f', 3)
         << "-t" << QString::number((durationMs + preRollMs) / 1000.0, 'f', 3);
  args << "-i" << input << "-vn" << "-sn";
  QString filter = QStringLiteral("aresample=48000");
  if (strength > 0) {
    // arnndn adds one 480-sample frame of latency. Pad its tail and discard
    // that latency so consonant starts, the last syllable and A/V sync survive.
    filter += QStringLiteral(
        ",apad=pad_len=480,asetnsamples=n=480:p=1,"
        "arnndn=m=std.rnnn:mix=%1,atrim=start_sample=%2")
        .arg(strength / 100.0, 0, 'f', 2).arg(480 + preRollMs * 48);
  } else if (preRollMs > 0) {
    filter += QStringLiteral(",atrim=start_sample=%1").arg(preRollMs * 48);
  }
  filter += QStringLiteral(",asetpts=PTS-STARTPTS");
  if (strength > 0) {
    // arnndn reads a full 480-sample block even on a shorter final input frame;
    // asetnsamples pads that frame to prevent out-of-bounds reads and corrupt
    // audio. A silent
    // reference branch bounds the processed output to the exact decoded input
    // length, including fractional milliseconds at 44.1 kHz.
    filter.remove(0, QStringLiteral("aresample=48000,").size());
    const QString graph = QStringLiteral(
        "[0:a:0]aresample=48000,asplit=2[nr_duration][nr_input];"
        "[nr_duration]atrim=start_sample=%1,asetpts=PTS-STARTPTS[nr_reference];"
        "[nr_input]%2[nr_clean];"
        "[nr_reference][nr_clean]amix=inputs=2:duration=first:"
        "dropout_transition=0:weights='0 1':normalize=0[nr_out]")
        .arg(preRollMs * 48).arg(filter);
    args << "-filter_complex" << graph << "-map" << "[nr_out]";
  } else {
    args << "-map" << "0:a:0" << "-af" << filter;
  }
  args << "-c:a" << "pcm_s24le" << "-rf64" << "auto" << m_temporary;
  // A fixed model basename avoids FFmpeg filter escaping for Windows drive
  // letters, spaces, quotes and non-Latin application folders.
  m_process.setWorkingDirectory(strength > 0 ? QFileInfo(modelPath()).absolutePath()
                                             : QFileInfo(input).absolutePath());
  m_process.start(FfmpegRuntime::executable(), args);
  return true;
}

void NoiseRemoval::cancel() {
  if (!m_busy)
    return;
  m_cancelled = true;
  if (m_process.state() != QProcess::NotRunning)
    m_process.kill();
}

void NoiseRemoval::complete(int code, QProcess::ExitStatus status) {
  if (!m_busy)
    return;
  QString error;
  if (m_cancelled)
    error = tr("Noise removal cancelled.");
  else if (code != 0 || status != QProcess::NormalExit) {
    QString detail = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    if (detail.isEmpty())
      detail = tr("%1 (exit %2)").arg(m_process.errorString()).arg(code);
    error = tr("Could not clean this audio: %1").arg(detail.right(1200));
  }
  else if (!m_temporary.isEmpty()) {
    if (QFileInfo(m_temporary).size() <= 44)
      error = tr("The source has no usable audio.");
    else if (!QFile::rename(m_temporary, m_output) && QFileInfo(m_output).size() <= 44)
      error = tr("Could not save the cleaned audio.");
  }
  if (!m_temporary.isEmpty())
    QFile::remove(m_temporary);
  m_temporary.clear();
  m_busy = false;
  m_progress = error.isEmpty() ? 1.0 : 0.0;
  emit changed();
  emit finished(error.isEmpty() ? m_output : QString(), error);
}
