#include "app/subtitles/window_audio_prefetcher.h"

#include <QDir>
#include <QFile>
#include <QProcess>

namespace {

// Two extractions at a time: enough to hide a slow container seek, few enough
// that the disk and the cores stay available to the model.
constexpr int kParallelExtractions = 2;
// How far in front of the worker the prefetch is allowed to run. Three five
// minute windows is about 30 MB of scratch WAV.
constexpr int kMaxWindowsAhead = 3;

QString windowFileName(int index) {
  return QStringLiteral("w%1.wav").arg(index, 4, 10, QLatin1Char('0'));
}

QString failureFileName(int index) {
  return QStringLiteral("w%1.failed").arg(index, 4, 10, QLatin1Char('0'));
}

} // namespace

WindowAudioPrefetcher::WindowAudioPrefetcher(QObject *parent)
    : QObject(parent) {}

WindowAudioPrefetcher::~WindowAudioPrefetcher() { stop(); }

QString WindowAudioPrefetcher::wavPath(int index) const {
  if (m_directory.isEmpty())
    return {};
  return QDir(m_directory).filePath(windowFileName(index));
}

void WindowAudioPrefetcher::start(const QString &ffmpeg, const QString &source,
                                  const QString &directory,
                                  const QVector<TranscriptionWindow> &windows) {
  stop();
  m_ffmpeg = ffmpeg.isEmpty() ? QStringLiteral("ffmpeg") : ffmpeg;
  m_source = source;
  m_directory = directory;
  m_windows = windows;
  m_next = 0;
  m_workerWindow = 0;
  pump();
}

void WindowAudioPrefetcher::noteWindowStarted(int index) {
  m_workerWindow = qMax(m_workerWindow, index);
  pump();
}

void WindowAudioPrefetcher::releaseWindow(int index) {
  if (!m_ready.remove(index))
    return;
  QFile::remove(wavPath(index));
  pump();
}

void WindowAudioPrefetcher::stop() {
  const QList<QProcess *> running = m_running.values();
  m_running.clear();
  for (QProcess *process : running) {
    if (!process)
      continue;
    process->disconnect();
    process->kill();
    process->waitForFinished(1500);
    process->deleteLater();
  }
  m_windows.clear();
  m_ready.clear();
  m_next = 0;
  m_workerWindow = 0;
  m_directory.clear();
}

void WindowAudioPrefetcher::pump() {
  while (m_running.size() < kParallelExtractions && m_next < m_windows.size() &&
         m_next <= m_workerWindow + kMaxWindowsAhead) {
    const int index = m_next++;
    extract(index);
  }
}

void WindowAudioPrefetcher::extract(int index) {
  const TranscriptionWindow window = m_windows.at(index);
  const QString target = wavPath(index);
  const QString partial = target + QStringLiteral(".part");
  QFile::remove(partial);
  QFile::remove(target);

  // Byte for byte the command tools/whisper_transcribe.py builds. Both sides
  // must stay identical: the point of the prefetch is that the model cannot tell
  // which process decoded its audio.
  const QStringList arguments{
      QStringLiteral("-nostdin"),
      QStringLiteral("-hide_banner"),
      QStringLiteral("-loglevel"),
      QStringLiteral("error"),
      QStringLiteral("-y"),
      QStringLiteral("-ss"),
      QString::asprintf("%.3f", qMax<qint64>(0, window.audioStartMs()) / 1000.0),
      QStringLiteral("-t"),
      QString::asprintf("%.3f", qMax<qint64>(1, window.audioLengthMs()) / 1000.0),
      QStringLiteral("-i"),
      m_source,
      QStringLiteral("-map"),
      QStringLiteral("0:a:0"),
      QStringLiteral("-vn"),
      QStringLiteral("-sn"),
      QStringLiteral("-ac"),
      QStringLiteral("1"),
      QStringLiteral("-ar"),
      QStringLiteral("16000"),
      QStringLiteral("-c:a"),
      QStringLiteral("pcm_s16le"),
      QStringLiteral("-f"),
      QStringLiteral("wav"),
      partial};

  auto *process = new QProcess(this);
  process->setProcessChannelMode(QProcess::MergedChannels);
  m_running.insert(index, process);
  connect(process, &QProcess::finished, this,
          [this, index, process, target, partial](int code,
                                                 QProcess::ExitStatus status) {
            m_running.remove(index);
            const bool ok = status == QProcess::NormalExit && code == 0 &&
                            QFile::exists(partial) &&
                            QFile(partial).size() > 44;
            if (ok && QFile::rename(partial, target)) {
              m_ready.insert(index);
              emit windowReady(index, target);
            } else {
              // The worker extracts this one itself. An empty marker is enough:
              // it only has to arrive before the worker starts waiting.
              QFile::remove(partial);
              QFile marker(QDir(m_directory).filePath(failureFileName(index)));
              if (marker.open(QIODevice::WriteOnly))
                marker.close();
            }
            process->deleteLater();
            pump();
          });
  connect(process, &QProcess::errorOccurred, this,
          [this, index, process](QProcess::ProcessError error) {
            // A process that never started emits no finished(), so the slot
            // above would never run and the queue would stall behind it.
            if (error != QProcess::FailedToStart ||
                m_running.value(index) != process)
              return;
            m_running.remove(index);
            if (!m_directory.isEmpty()) {
              QFile marker(QDir(m_directory).filePath(failureFileName(index)));
              if (marker.open(QIODevice::WriteOnly))
                marker.close();
            }
            process->deleteLater();
            pump();
          });
  process->start(m_ffmpeg, arguments);
}
