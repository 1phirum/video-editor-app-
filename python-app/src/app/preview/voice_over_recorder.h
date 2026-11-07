#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioBuffer>
#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QThread>
#include <QMetaType>
#include <atomic>

#include "core/module_api.h"

class QAudioSource;
class QFile;

// Microphone capture for Voice-Over Record, written straight to .wav files.
//
// Two modes, both driven by the same continuous capture:
//
//  * ONE TAKE - everything between Record and Stop becomes a single clip.
//
//  * HANDS-FREE - the microphone stays open and listening. Each burst of speech
//    becomes its own clip, committed to the timeline as soon as you stop
//    talking, and then it listens again. You never touch the button between
//    lines.
//
// Three decisions worth stating, because the obvious implementation of each is
// wrong here:
//
// 1. PUSH, NOT POLL. QAudioSource offers start() -> QIODevice you read from, and
//    start(QIODevice*) which writes into a device you own. Qt's own docs warn
//    that if you read the first one too slowly its ring buffer fills, the state
//    drops to Idle and "this state change will drop audio data". The default
//    buffer is 250 ms, and this application's GUI thread is documented to stall
//    for whole seconds (see preview_audio_sink.h). So capture pushes into the
//    writer below instead: Qt hands us every buffer as it arrives and there is
//    no read cadence to miss. A dropout in a take you cannot repeat is not a
//    recoverable error.
//
// 2. OFF THE GUI THREAD. QAudioSink::start() was measured at 2038 ms on the GUI
//    thread; opening an input client is the same synchronous RPC into the
//    Windows audio service. The source and the files live on their own thread,
//    exactly as the playback sink does.
//
// 3. WE WRITE THE RIFF HEADER. Qt captures raw PCM and has no container writer -
//    start(&file) yields headerless bytes that nothing will open. The header is
//    laid down with placeholder sizes and patched as each segment closes.
//
// Device enumeration stays with the caller, for the reason PreviewAudioSink
// documents: the platform device manager is a GUI-thread singleton, while
// QAudioDevice is a copyable value that crosses threads safely.

// How a session decides what to keep.
struct VoiceOverCaptureConfig {
  quint64 requestId = 0;
  // Amplitude (0..32767) at or above which a buffer counts as speech. 0 turns
  // voice activity off entirely, which is what one-take mode uses.
  int silenceFloor = 0;
  // Hands-free only: how long the signal must stay below the floor before the
  // current segment is closed and committed.
  qint64 hangoverMs = 1200;
  // Hands-free only: how much audio from *before* the floor was crossed to keep.
  // Without this every segment opens on a clipped consonant, because the
  // decision to start can only be made after the sound has already arrived.
  qint64 lookbackMs = 300;
  // Hands-free only: a segment shorter than this is a cough or a chair creak,
  // not a line, and is thrown away rather than put on the timeline.
  qint64 minSegmentMs = 350;
  // Keep this much of the trailing silence so a word does not end abruptly; the
  // rest of the hangover is trimmed off.
  qint64 tailPaddingMs = 180;
  bool handsFree = false;
  // Open the microphone and measure it without writing anything. This is what
  // makes "speak to check your level" true before a take rather than after one:
  // the meter reads the capture buffer, so without an open input there is
  // nothing to show.
  bool meterOnly = false;
  bool webcam = false;
};

// Crosses a thread boundary as a Q_ARG, so it needs a metatype.
Q_DECLARE_METATYPE(VoiceOverCaptureConfig)

// The capture destination: a QIODevice that Qt writes captured PCM into, which
// meters it, tracks voice activity, and writes segments out as .wav files.
class CUTPRO_PREVIEW_API VoiceOverWavWriter final : public QIODevice {
  Q_OBJECT

public:
  explicit VoiceOverWavWriter(QObject *parent = nullptr);
  ~VoiceOverWavWriter() override;

  // `pathPattern` must contain a single %1, substituted with the segment index.
  // One-take mode only ever produces index 1.
  bool beginSession(const QString &pathPattern, const QAudioFormat &format,
                    const VoiceOverCaptureConfig &config);
  // Closes any open segment (committing it if it is long enough) and ends the
  // session. Returns false when nothing was kept.
  bool endSession();
  // Ends the session and deletes whatever the open segment had written.
  void abandonSession();

  // Milliseconds of audio in the segment currently being written, or 0 while
  // listening.
  qint64 segmentMs() const;
  // Milliseconds since the session started, which is what positions a segment on
  // the timeline.
  qint64 sessionMs() const;
  bool capturing() const { return m_capturing.load(std::memory_order_acquire); }

  // Peak of each channel (0..1 linear) since the last read; reading clears it, so
  // a caller polling on a timer sees the peak over its own interval and the meter
  // falls to zero on silence. This mirrors PreviewAudioSink's meter, but measures
  // the *capture* buffer - the playback meter is post-fader and reads zero while
  // monitoring is muted, which is precisely when a record meter has to work.
  void peakLevels(float *left, float *right);

  bool isSequential() const override { return true; }

signals:
  void audioBufferReady(const QAudioBuffer &buffer);
  // A finished segment, ready to be placed. `offsetMs` is where it begins
  // relative to the start of the session.
  void segmentReady(const QString &path, qint64 durationMs, qint64 offsetMs);
  void writeFailed(const QString &message);

protected:
  qint64 readData(char *data, qint64 maxSize) override;
  qint64 writeData(const char *data, qint64 maxSize) override;

private:
  bool openSegment(qint64 sessionBytesAtStart);
  // Finalises the open segment. Returns true when it was long enough to keep.
  bool closeSegment(bool keep);
  qint64 bytesToMs(qint64 bytes) const;
  qint64 msToBytes(qint64 ms) const;
  void appendLookback(const char *data, qint64 size);

  QFile *m_file = nullptr;
  QString m_pathPattern;
  QString m_segmentPath;
  QAudioFormat m_format;
  VoiceOverCaptureConfig m_config;
  int m_segmentIndex = 0;

  // Session-wide byte counter: the clock everything is positioned against.
  std::atomic<qint64> m_sessionBytes{0};
  std::atomic<qint64> m_segmentBytes{0};
  std::atomic<bool> m_capturing{false};
  // Byte offset into the session at which the open segment begins.
  qint64 m_segmentStartBytes = 0;
  // Bytes of consecutive below-floor audio, and the last above-floor position
  // within the open segment, for trimming the tail.
  qint64 m_silenceRunBytes = 0;
  qint64 m_lastSpeechBytes = 0;
  // Rolling pre-roll so a segment can include the instant before it was
  // recognised as speech.
  QByteArray m_lookback;

  std::atomic<int> m_peakL{0};
  std::atomic<int> m_peakR{0};
  std::atomic<qint64> m_sessionDurationUs{0};
  std::atomic<qint64> m_segmentDurationMs{0};
};

class CUTPRO_PREVIEW_API VoiceOverRecorderWorker final : public QObject {
  Q_OBJECT

public:
  explicit VoiceOverRecorderWorker(QObject *parent = nullptr);
  ~VoiceOverRecorderWorker() override;

  // Safe from any thread: all of it reads atomics on the writer.
  void peakLevels(float *left, float *right);
  qint64 segmentMs() const;
  qint64 sessionMs() const;
  bool capturing() const;

public slots:
  // Opens the input client and immediately stops it, so the first real take does
  // not pay the audio-service round trip. Same trick as PreviewAudioSink::warm().
  void warm(const QAudioDevice &device, int sampleRate, int channels);
  void begin(const QAudioDevice &device, int sampleRate, int channels,
             const QString &pathPattern, VoiceOverCaptureConfig config);
  void finish();
  void discard();
  void teardown();

signals:
  void audioFormatReady(const QAudioFormat &format);
  void audioBufferReady(const QAudioBuffer &buffer);
  void beginFailed(const QString &message, quint64 requestId);
  void beginSucceeded(bool meterOnly, quint64 requestId);
  void segmentReady(const QString &path, qint64 durationMs, qint64 offsetMs);
  void sessionEnded(bool keptAnything, quint64 requestId);
  void discarded(bool recording, quint64 requestId);
  void captureFailed(const QString &message);

private:
  QAudioFormat resolveFormat(const QAudioDevice &device, int sampleRate,
                             int channels, QString *error) const;
  void releaseSource();

  QAudioSource *m_source = nullptr;
  VoiceOverWavWriter *m_writer = nullptr;
  QAudioFormat m_format;
  bool m_recording = false;
  quint64 m_requestId = 0;
};

// GUI-thread facade. Owns the capture thread.
class CUTPRO_PREVIEW_API VoiceOverRecorder final : public QObject {
  Q_OBJECT

public:
  explicit VoiceOverRecorder(QObject *parent = nullptr);
  ~VoiceOverRecorder() override;

  bool sessionActive() const { return m_sessionActive; }
  // True while the input is open purely to drive the level meter.
  bool monitoring() const { return m_monitoring; }
  bool starting() const { return m_starting; }
  void warm(const QAudioDevice &device, int sampleRate, int channels);
  void begin(const QAudioDevice &device, int sampleRate, int channels,
             const QString &pathPattern, const VoiceOverCaptureConfig &config);
  void finish();
  void discard();

  void peakLevels(float *left, float *right);
  qint64 segmentMs() const;
  qint64 sessionMs() const;
  bool capturing() const;

signals:
  void audioFormatReady(const QAudioFormat &format);
  void audioBufferReady(const QAudioBuffer &buffer);
  void started();
  void failed(const QString &message);
  void captureFailed(const QString &message);
  void segmentReady(const QString &path, qint64 durationMs, qint64 offsetMs);
  void sessionEnded(bool keptAnything);
  void cancelled();

private:
  QThread m_thread;
  VoiceOverRecorderWorker *m_worker = nullptr;
  bool m_sessionActive = false;
  bool m_monitoring = false;
  bool m_starting = false;
  bool m_cancelling = false;
  quint64 m_requestId = 0;
};
