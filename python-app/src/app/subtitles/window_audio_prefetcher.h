#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include "app/subtitles/transcription_plan.h"
#include "core/module_api.h"

class QProcess;

// Extracts the next few transcription windows to disk while Whisper is still
// working on the current one.
//
// The windowed worker used to do this itself, in line: extract a five minute WAV
// with ffmpeg, transcribe it, delete it, extract the next one. Extraction and
// inference never overlapped, so on a long source every window paid a seek and a
// decode with the CPU otherwise idle, and the very first window also waited for
// the model to load before ffmpeg was even started.
//
// Here the extractions run ahead of the worker, two at a time, a bounded number
// of windows in front of it. The argument list is character for character the one
// the Python side uses, so the WAV the model sees is the same file it would have
// produced itself - this buys wall clock and cannot change a transcript. If an
// extraction fails, a marker file tells the worker to fall back to extracting
// that window itself, so a prefetch problem can only cost time.
class CUTPRO_SUBTITLES_API WindowAudioPrefetcher : public QObject {
  Q_OBJECT
public:
  explicit WindowAudioPrefetcher(QObject *parent = nullptr);
  ~WindowAudioPrefetcher() override;

  // `directory` is the worker's scratch folder; the file names are the ones the
  // worker looks for.
  void start(const QString &ffmpeg, const QString &source,
             const QString &directory,
             const QVector<TranscriptionWindow> &windows);
  // The worker has moved on to `index`, so the horizon moves with it.
  void noteWindowStarted(int index);
  // The window's audio is no longer needed by anything.
  void releaseWindow(int index);
  void stop();
  bool active() const { return !m_windows.isEmpty(); }

  QString wavPath(int index) const;

signals:
  void windowReady(int index, const QString &path);

private:
  void pump();
  void extract(int index);

  QString m_ffmpeg;
  QString m_source;
  QString m_directory;
  QVector<TranscriptionWindow> m_windows;
  QHash<int, QProcess *> m_running;
  QSet<int> m_ready;
  int m_next = 0;
  int m_workerWindow = 0;
};
