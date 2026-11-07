#pragma once

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <functional>

class QObject;

// Word timings read off the clip's own audio, for cues that came without any.
//
// This is tier two of the caption word clock. A transcribed cue carries the
// timings Whisper measured and needs nothing from here; an imported .srt, a
// translated line or a caption the user typed by hand carries none, and an even
// split of the cue is visibly wrong the moment the speaker pauses. So the cue's
// span of audio is decoded to an envelope and CaptionWordClock::fromEnvelope()
// fits the words to where the voice actually is.
//
// Decoding is not something the GUI thread may do - a cue is a couple of seconds
// of audio out of a multi-hour source, which means a seek and a decode - so
// timingFor() never blocks. It answers with what it already knows, starts one
// analysis for what it does not, and calls the ready callback when that lands;
// the monitor then asks again and gets the real timing. Until then the caller
// falls back to the weighted split, which is tier three.
class CaptionSpeechTiming final {
public:
  // owner is the context the ready callback is delivered on, so a Backend that
  // outlives this object hears about a landed analysis on the GUI thread.
  explicit CaptionSpeechTiming(QObject *owner);
  ~CaptionSpeechTiming();

  CaptionSpeechTiming(const CaptionSpeechTiming &) = delete;
  CaptionSpeechTiming &operator=(const CaptionSpeechTiming &) = delete;

  void setReadyCallback(std::function<void()> callback);

  // The packed timing for this cue, or empty while it is not known yet. Safe on
  // the GUI thread: a miss queues the work and returns immediately.
  QString timingFor(const QString &source, qint64 audioStartMs, qint64 cueMs,
                    const QStringList &words);
  // The same answer, decoded here and now. For the export, which has no second
  // chance to ask: an empty return means the audio could not be read and the
  // caller should let the weighted split stand.
  QString timingNow(const QString &source, qint64 audioStartMs, qint64 cueMs,
                    const QStringList &words);

  // A new project, or media whose peaks are no longer trustworthy.
  void clear();

private:
  friend class CaptionSpeechTimingJob;

  struct Request {
    QString source;
    qint64 audioStartMs = 0;
    qint64 cueMs = 0;
    QStringList words;
    QString key;
  };

  // Empty when this cue can never be analysed: no audio, too short to fit words
  // into, or long enough that decoding it would cost more than the highlight is
  // worth.
  static Request requestFor(const QString &source, qint64 audioStartMs,
                            qint64 cueMs, const QStringList &words);
  static QString analyse(const Request &request,
                         const std::atomic_bool *cancel);
  void store(const QString &key, const QString &packed);
  void notifyReady();

  QObject *m_owner = nullptr;
  std::function<void()> m_ready;
  mutable QMutex m_mutex;
  // Packed timings by cue key. An entry is kept even when the analysis came back
  // empty, so an unreadable or silent stretch is asked about once rather than on
  // every pass of the playhead.
  QHash<QString, QString> m_done;
  QSet<QString> m_running;
  QThreadPool m_pool;
  std::atomic_bool m_shutdown{false};
};
