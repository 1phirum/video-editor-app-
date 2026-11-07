#include "app/preview/voice_over_recorder.h"

#include <QAudioSource>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>
#include <cstdlib>

Q_LOGGING_CATEGORY(lcVoiceOver, "cutpro.voiceover")

namespace {

// Little-endian scalar writers. QDataStream would do, but the header is 44 bytes
// of fixed layout and spelling it out keeps the field order readable against the
// RIFF spec.
void putU32(QByteArray *out, quint32 value) {
  const char bytes[4] = {char(value & 0xff), char((value >> 8) & 0xff),
                         char((value >> 16) & 0xff), char((value >> 24) & 0xff)};
  out->append(bytes, 4);
}

void putU16(QByteArray *out, quint16 value) {
  const char bytes[2] = {char(value & 0xff), char((value >> 8) & 0xff)};
  out->append(bytes, 2);
}

constexpr qint64 kRiffSizeOffset = 4;  // "RIFF" <here> "WAVE"
constexpr qint64 kDataSizeOffset = 40; // after "data"
constexpr int kBits = 16;              // the only sample format this writer emits

} // namespace

// ---------------------------------------------------------------- wav writer

VoiceOverWavWriter::VoiceOverWavWriter(QObject *parent) : QIODevice(parent) {}

VoiceOverWavWriter::~VoiceOverWavWriter() { abandonSession(); }

qint64 VoiceOverWavWriter::bytesToMs(qint64 bytes) const {
  if (!m_format.isValid() || bytes <= 0)
    return 0;
  return m_format.durationForBytes(bytes) / 1000;
}

qint64 VoiceOverWavWriter::msToBytes(qint64 ms) const {
  if (!m_format.isValid() || ms <= 0)
    return 0;
  // Round down to a whole frame: a byte offset mid-frame would swap the channels
  // for the rest of the segment.
  const qint64 align = qMax(1, m_format.bytesPerFrame());
  return (m_format.bytesForDuration(ms * 1000) / align) * align;
}

bool VoiceOverWavWriter::beginSession(const QString &pathPattern,
                                      const QAudioFormat &format,
                                      const VoiceOverCaptureConfig &config) {
  abandonSession();
  m_pathPattern = pathPattern;
  m_format = format;
  m_config = config;
  m_segmentIndex = 0;
  m_sessionBytes.store(0, std::memory_order_release);
  m_segmentBytes.store(0, std::memory_order_release);
  m_sessionDurationUs.store(0, std::memory_order_release);
  m_segmentDurationMs.store(0, std::memory_order_release);
  m_capturing.store(false, std::memory_order_release);
  m_segmentStartBytes = 0;
  m_silenceRunBytes = 0;
  m_lastSpeechBytes = 0;
  m_lookback.clear();
  m_peakL.store(0, std::memory_order_release);
  m_peakR.store(0, std::memory_order_release);

  if (!QIODevice::open(QIODevice::WriteOnly))
    return false;
  // One-take mode writes continuously from the moment the session opens; there
  // is nothing to wait for.
  if (m_config.meterOnly)
    return true;
  if (!m_config.handsFree)
    return openSegment(0);
  qCInfo(lcVoiceOver) << "listening: floor" << m_config.silenceFloor
                      << "hangover" << m_config.hangoverMs << "ms";
  return true;
}

bool VoiceOverWavWriter::openSegment(qint64 sessionBytesAtStart) {
  ++m_segmentIndex;
  m_segmentPath = m_pathPattern.arg(m_segmentIndex);
  m_file = new QFile(m_segmentPath, this);
  if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qCWarning(lcVoiceOver) << "cannot open" << m_segmentPath
                           << m_file->errorString();
    emit writeFailed(tr("Could not create %1.").arg(m_segmentPath));
    delete m_file;
    m_file = nullptr;
    m_segmentPath.clear();
    return false;
  }

  const int channels = qMax(1, m_format.channelCount());
  const int rate = qMax(8000, m_format.sampleRate());
  const int blockAlign = channels * kBits / 8;

  QByteArray header;
  header.reserve(44);
  header.append("RIFF", 4);
  putU32(&header, 0); // patched on close: 36 + dataBytes
  header.append("WAVE", 4);
  header.append("fmt ", 4);
  putU32(&header, 16);                         // PCM fmt chunk size
  putU16(&header, 1);                          // PCM, uncompressed
  putU16(&header, quint16(channels));
  putU32(&header, quint32(rate));
  putU32(&header, quint32(rate * blockAlign)); // byte rate
  putU16(&header, quint16(blockAlign));
  putU16(&header, quint16(kBits));
  header.append("data", 4);
  putU32(&header, 0); // patched on close: dataBytes

  if (m_file->write(header) != header.size()) {
    qCWarning(lcVoiceOver) << "cannot write wav header to" << m_segmentPath;
    m_file->close();
    if (!m_segmentPath.isEmpty())
      QFile::remove(m_segmentPath);
    delete m_file;
    m_file = nullptr;
    m_segmentPath.clear();
    return false;
  }

  m_segmentStartBytes = sessionBytesAtStart;
  m_segmentBytes.store(0, std::memory_order_release);
  m_segmentDurationMs.store(0, std::memory_order_release);
  m_silenceRunBytes = 0;
  m_lastSpeechBytes = 0;
  m_capturing.store(true, std::memory_order_release);
  return true;
}

bool VoiceOverWavWriter::closeSegment(bool keep) {
  if (!m_file)
    return false;
  m_capturing.store(false, std::memory_order_release);
  const QString path = m_segmentPath;
  qint64 bytes = m_segmentBytes.load(std::memory_order_acquire);

  // Trim the hangover silence that proved the line had ended, leaving a short
  // tail so the last word does not stop dead.
  if (keep && m_config.handsFree && m_lastSpeechBytes > 0) {
    const qint64 trimmed =
        qMin(bytes, m_lastSpeechBytes + msToBytes(m_config.tailPaddingMs));
    if (trimmed > 0)
      bytes = trimmed;
  }

  const qint64 durationMs = bytesToMs(bytes);
  const bool longEnough =
      keep && durationMs >= (m_config.handsFree ? m_config.minSegmentMs : 1);

  m_file->flush();
  if (longEnough) {
    // Both size fields are only knowable now. A header left with zeroes opens
    // as a zero-length file in every player, which looks exactly like a
    // recording that failed.
    QByteArray riffSize;
    putU32(&riffSize, quint32(36 + bytes));
    QByteArray dataSize;
    putU32(&dataSize, quint32(bytes));
    const bool patched = m_file->seek(kRiffSizeOffset) &&
                         m_file->write(riffSize) == riffSize.size() &&
                         m_file->seek(kDataSizeOffset) &&
                         m_file->write(dataSize) == dataSize.size();
    // Drop the trimmed tail from the file itself rather than leaving bytes the
    // header says are not there.
    if (patched)
      m_file->resize(44 + bytes);
    m_file->flush();
    if (!patched)
      qCWarning(lcVoiceOver) << "could not patch wav sizes in" << path;
  }
  m_file->close();
  delete m_file;
  m_file = nullptr;
  m_segmentPath.clear();

  if (!longEnough) {
    // A blip, or a cancelled session: a part-written take is of no use and
    // would otherwise sit in the project folder looking real. Guarded because
    // a metering-only session never opens a segment, and QFile warns on an
    // empty name.
    if (!path.isEmpty())
      QFile::remove(path);
    --m_segmentIndex; // let the next segment reuse the number
    return false;
  }

  const qint64 offsetMs = bytesToMs(m_segmentStartBytes);
  qCInfo(lcVoiceOver) << "segment" << path << durationMs << "ms at" << offsetMs
                      << "ms into the session";
  emit segmentReady(path, durationMs, offsetMs);
  return true;
}

bool VoiceOverWavWriter::endSession() {
  const bool kept = m_file ? closeSegment(true) : false;
  if (isOpen())
    QIODevice::close();
  m_lookback.clear();
  return kept;
}

void VoiceOverWavWriter::abandonSession() {
  if (m_file)
    closeSegment(false);
  if (isOpen())
    QIODevice::close();
  m_lookback.clear();
}

qint64 VoiceOverWavWriter::segmentMs() const {
  return m_capturing.load(std::memory_order_acquire)
             ? m_segmentDurationMs.load(std::memory_order_acquire)
             : 0;
}

qint64 VoiceOverWavWriter::sessionMs() const {
  return m_sessionDurationUs.load(std::memory_order_acquire) / 1000;
}

void VoiceOverWavWriter::peakLevels(float *left, float *right) {
  const int l = m_peakL.exchange(0, std::memory_order_acq_rel);
  const int r = m_peakR.exchange(0, std::memory_order_acq_rel);
  if (left)
    *left = std::min(1.0f, float(l) / 32768.0f);
  if (right)
    *right = std::min(1.0f, float(r) / 32768.0f);
}

void VoiceOverWavWriter::appendLookback(const char *data, qint64 size) {
  const qint64 capacity = msToBytes(m_config.lookbackMs);
  if (capacity <= 0)
    return;
  m_lookback.append(data, int(size));
  if (m_lookback.size() > capacity)
    m_lookback.remove(0, int(m_lookback.size() - capacity));
}

qint64 VoiceOverWavWriter::readData(char *, qint64) { return -1; }

qint64 VoiceOverWavWriter::writeData(const char *data, qint64 maxSize) {
  if (maxSize <= 0 || !m_format.isValid())
    return 0;

  // Peak per channel. The playback meter hard-assumes 4 bytes/frame stereo; a
  // microphone is usually mono, so the stride comes from the negotiated format
  // rather than a constant.
  const int channels = qMax(1, m_format.channelCount());
  const qint64 samples = maxSize / 2; // Int16
  const auto *pcm = reinterpret_cast<const qint16 *>(data);
  int peakL = 0;
  int peakR = 0;
  for (qint64 i = 0; i + channels - 1 < samples; i += channels) {
    const int left = std::abs(int(pcm[i]));
    peakL = std::max(peakL, left);
    const int right =
        channels > 1 ? std::abs(int(pcm[i + 1])) : left; // mono feeds both
    peakR = std::max(peakR, right);
  }
  // Keep whatever the meter has not consumed yet, so a peak falling between two
  // reads is never lost.
  int previous = m_peakL.load(std::memory_order_acquire);
  while (peakL > previous &&
         !m_peakL.compare_exchange_weak(previous, peakL,
                                        std::memory_order_acq_rel)) {
  }
  previous = m_peakR.load(std::memory_order_acquire);
  while (peakR > previous &&
         !m_peakR.compare_exchange_weak(previous, peakR,
                                        std::memory_order_acq_rel)) {
  }

  const qint64 sessionBefore = m_sessionBytes.load(std::memory_order_acquire);
  m_sessionBytes.store(sessionBefore + maxSize, std::memory_order_release);
  m_sessionDurationUs.store(m_format.durationForBytes(sessionBefore + maxSize),
                           std::memory_order_release);
  if (m_config.webcam && !m_config.meterOnly)
    emit audioBufferReady(QAudioBuffer(QByteArray(data, int(maxSize)), m_format,
                                      m_format.durationForBytes(sessionBefore)));
  if (m_config.meterOnly)
    return maxSize;

  const bool loud =
      m_config.silenceFloor > 0 && qMax(peakL, peakR) >= m_config.silenceFloor;

  // ---- Hands-free: decide whether this buffer opens or closes a segment ----
  if (m_config.handsFree && !m_file) {
    if (!loud) {
      // Still listening. Hold the audio so the next segment can reach back and
      // include the instant before speech was recognised.
      appendLookback(data, maxSize);
      return maxSize;
    }
    // Speech. Open a segment that begins where the lookback begins, so the
    // first consonant is inside it rather than clipped off the front.
    const qint64 lookbackBytes = m_lookback.size();
    if (!openSegment(qMax<qint64>(0, sessionBefore - lookbackBytes)))
      return maxSize;
    if (lookbackBytes > 0) {
      const qint64 flushed = m_file->write(m_lookback);
      if (flushed > 0)
        m_segmentBytes.fetch_add(flushed, std::memory_order_acq_rel);
      m_lookback.clear();
    }
  }

  if (!m_file) {
    // One-take mode with no segment open means the session is finished.
    return maxSize;
  }

  const qint64 written = m_file->write(data, maxSize);
  if (written > 0)
    m_segmentBytes.fetch_add(written, std::memory_order_acq_rel);
  m_segmentDurationMs.store(bytesToMs(m_segmentBytes.load()),
                            std::memory_order_release);
  if (written != maxSize)
    qCWarning(lcVoiceOver) << "short wav write" << written << "of" << maxSize;

  if (m_config.handsFree) {
    const qint64 segmentNow = m_segmentBytes.load(std::memory_order_acquire);
    if (loud) {
      m_silenceRunBytes = 0;
      m_lastSpeechBytes = segmentNow;
    } else {
      m_silenceRunBytes += maxSize;
      if (bytesToMs(m_silenceRunBytes) >= m_config.hangoverMs) {
        // The line has ended. Commit it and go back to listening - the point of
        // hands-free is that the next line needs no button press.
        closeSegment(true);
      }
    }
  }
  return maxSize;
}

// -------------------------------------------------------------------- worker

VoiceOverRecorderWorker::VoiceOverRecorderWorker(QObject *parent)
    : QObject(parent), m_writer(new VoiceOverWavWriter(this)) {
  // Persistent for the entire worker lifetime: the GUI only reads atomics,
  // never a pointer that capture start/stop can concurrently replace.
  connect(m_writer, &VoiceOverWavWriter::segmentReady, this,
          &VoiceOverRecorderWorker::segmentReady);
  connect(m_writer, &VoiceOverWavWriter::audioBufferReady, this,
          &VoiceOverRecorderWorker::audioBufferReady, Qt::DirectConnection);
}

VoiceOverRecorderWorker::~VoiceOverRecorderWorker() { releaseSource(); }

void VoiceOverRecorderWorker::peakLevels(float *left, float *right) {
  if (m_writer) {
    m_writer->peakLevels(left, right);
    return;
  }
  if (left)
    *left = 0.0f;
  if (right)
    *right = 0.0f;
}

qint64 VoiceOverRecorderWorker::segmentMs() const {
  return m_writer ? m_writer->segmentMs() : 0;
}

qint64 VoiceOverRecorderWorker::sessionMs() const {
  return m_writer ? m_writer->sessionMs() : 0;
}

bool VoiceOverRecorderWorker::capturing() const {
  return m_writer && m_writer->capturing();
}

QAudioFormat VoiceOverRecorderWorker::resolveFormat(const QAudioDevice &device,
                                                    int sampleRate,
                                                    int channels,
                                                    QString *error) const {
  QAudioFormat format;
  format.setSampleRate(sampleRate);
  format.setChannelCount(channels);
  format.setSampleFormat(QAudioFormat::Int16);
  if (device.isFormatSupported(format))
    return format;

  // Fall back to whatever the device prefers, but only if it is still Int16:
  // this writer emits 16-bit PCM and converting sample formats by hand is a
  // different feature. Saying so beats recording noise.
  const QAudioFormat preferred = device.preferredFormat();
  if (preferred.sampleFormat() == QAudioFormat::Int16) {
    qCInfo(lcVoiceOver) << "requested" << sampleRate << "Hz" << channels
                        << "ch unsupported; using device preferred"
                        << preferred.sampleRate() << "Hz"
                        << preferred.channelCount() << "ch";
    return preferred;
  }
  if (error)
    *error = tr("\"%1\" does not offer 16-bit recording.")
                 .arg(device.description());
  return {};
}

void VoiceOverRecorderWorker::warm(const QAudioDevice &device, int sampleRate,
                                   int channels) {
  if (m_source || device.isNull())
    return;
  QString ignored;
  const QAudioFormat format =
      resolveFormat(device, sampleRate, channels, &ignored);
  if (!format.isValid())
    return;
  // Open the input client and drop it again. Opening is the slow part (a
  // synchronous call into the Windows audio service), and doing it here means
  // the user's first take starts promptly instead of after a pause.
  auto *probe = new QAudioSource(device, format);
  probe->start();
  probe->stop();
  delete probe;
  qCInfo(lcVoiceOver) << "input warmed:" << device.description();
}

void VoiceOverRecorderWorker::begin(const QAudioDevice &device, int sampleRate,
                                    int channels, const QString &pathPattern,
                                    VoiceOverCaptureConfig config) {
  m_requestId = config.requestId;
  if (m_source) {
    emit beginFailed(tr("A recording is already running."), m_requestId);
    return;
  }
  if (device.isNull()) {
    emit beginFailed(tr("No microphone is available."), m_requestId);
    return;
  }

  QString error;
  m_format = resolveFormat(device, sampleRate, channels, &error);
  if (!m_format.isValid()) {
    emit beginFailed(error.isEmpty() ? tr("This microphone cannot be recorded.")
                                     : error, m_requestId);
    return;
  }

  if (!m_writer->beginSession(pathPattern, m_format, config)) {
    m_writer->abandonSession();
    emit beginFailed(tr("Could not create the recording file."), m_requestId);
    return;
  }

  m_source = new QAudioSource(device, m_format, this);
  m_source->setBufferSize(m_format.bytesForDuration(40000));
  if (config.webcam)
    emit audioFormatReady(m_format);
  connect(m_source, &QAudioSource::stateChanged, this,
          [this](QAudio::State state) {
            if (state == QAudio::StoppedState && m_source &&
                m_source->error() != QAudio::NoError) {
              qCWarning(lcVoiceOver)
                  << "capture stopped with error" << m_source->error();
              emit captureFailed(tr("The microphone stopped unexpectedly. Check its connection."));
            }
          });
  m_source->start(m_writer);
  if (m_source->error() != QAudio::NoError) {
    qCWarning(lcVoiceOver) << "capture failed to start" << m_source->error();
    releaseSource();
    m_writer->abandonSession();
    emit beginFailed(
        tr("The microphone could not be opened. Another application may be "
           "using it."), m_requestId);
    return;
  }
  qCInfo(lcVoiceOver) << "session open" << m_format.sampleRate() << "Hz"
                      << m_format.channelCount() << "ch handsFree"
                      << config.handsFree;
  m_recording = !config.meterOnly;
  emit beginSucceeded(config.meterOnly, m_requestId);
}

void VoiceOverRecorderWorker::finish() {
  if (!m_writer)
    return;
  releaseSource();
  const bool kept = m_writer->endSession();
  m_recording = false;
  emit sessionEnded(kept, m_requestId);
}

void VoiceOverRecorderWorker::discard() {
  if (!m_writer)
    return;
  releaseSource();
  m_writer->abandonSession();
  const bool recording = m_recording;
  m_recording = false;
  emit discarded(recording, m_requestId);
}

void VoiceOverRecorderWorker::teardown() {
  if (m_writer)
    discard();
  else
    releaseSource();
}

void VoiceOverRecorderWorker::releaseSource() {
  if (!m_source)
    return;
  m_source->disconnect(this);
  m_source->stop();
  delete m_source;
  m_source = nullptr;
}

// -------------------------------------------------------------------- facade

VoiceOverRecorder::VoiceOverRecorder(QObject *parent) : QObject(parent) {
  qRegisterMetaType<VoiceOverCaptureConfig>("VoiceOverCaptureConfig");
  m_worker = new VoiceOverRecorderWorker;
  m_worker->moveToThread(&m_thread);
  connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
  connect(m_worker, &VoiceOverRecorderWorker::beginSucceeded, this, [this](bool meterOnly, quint64 requestId) {
    // A metering session is not a recording session: it must not light up the
    // record button or arm Stop.
    if (requestId != m_requestId || meterOnly || m_cancelling)
      return;
    m_starting = false;
    m_sessionActive = true;
    emit started();
  });
  connect(m_worker, &VoiceOverRecorderWorker::beginFailed, this,
          [this](const QString &message, quint64 requestId) {
            if (requestId != m_requestId)
              return;
            m_sessionActive = false;
            m_starting = false;
            m_monitoring = false;
            emit failed(message);
          });
  connect(m_worker, &VoiceOverRecorderWorker::segmentReady, this,
          &VoiceOverRecorder::segmentReady);
  connect(m_worker, &VoiceOverRecorderWorker::audioFormatReady, this,
          &VoiceOverRecorder::audioFormatReady);
  connect(m_worker, &VoiceOverRecorderWorker::audioBufferReady, this,
          &VoiceOverRecorder::audioBufferReady, Qt::DirectConnection);
  connect(m_worker, &VoiceOverRecorderWorker::captureFailed, this,
          &VoiceOverRecorder::captureFailed);
  connect(m_worker, &VoiceOverRecorderWorker::sessionEnded, this,
          [this](bool kept, quint64 requestId) {
            if (requestId != m_requestId)
              return;
            m_sessionActive = false;
            m_starting = false;
            emit sessionEnded(kept);
          });
  connect(m_worker, &VoiceOverRecorderWorker::discarded, this, [this](bool wasRecording, quint64 requestId) {
    if (requestId != m_requestId || !wasRecording)
      return;
    m_sessionActive = false;
    m_starting = false;
    m_cancelling = false;
    if (wasRecording)
      emit cancelled();
  });
  m_thread.setObjectName(QStringLiteral("cutpro-voiceover"));
  m_thread.start();
}

VoiceOverRecorder::~VoiceOverRecorder() {
  // Blocking on purpose: the source has to be stopped and the header patched
  // before the thread's event loop goes away, or the take is left truncated
  // with zeroed sizes.
  if (m_thread.isRunning()) {
    QMetaObject::invokeMethod(m_worker, "teardown", Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait(2000);
  }
}

void VoiceOverRecorder::warm(const QAudioDevice &device, int sampleRate,
                             int channels) {
  QMetaObject::invokeMethod(m_worker, "warm", Qt::QueuedConnection,
                            Q_ARG(QAudioDevice, device),
                            Q_ARG(int, sampleRate), Q_ARG(int, channels));
}

void VoiceOverRecorder::begin(const QAudioDevice &device, int sampleRate,
                              int channels, const QString &pathPattern,
                              const VoiceOverCaptureConfig &config) {
  // A take supersedes metering: the same input cannot be opened twice.
  if (m_monitoring) {
    m_monitoring = false;
    QMetaObject::invokeMethod(m_worker, "discard", Qt::QueuedConnection);
  }
  m_monitoring = config.meterOnly;
  m_starting = !config.meterOnly;
  m_cancelling = false;
  VoiceOverCaptureConfig requestConfig = config;
  requestConfig.requestId = ++m_requestId;
  QMetaObject::invokeMethod(m_worker, "begin", Qt::QueuedConnection,
                            Q_ARG(QAudioDevice, device),
                            Q_ARG(int, sampleRate), Q_ARG(int, channels),
                            Q_ARG(QString, pathPattern),
                            Q_ARG(VoiceOverCaptureConfig, requestConfig));
}

void VoiceOverRecorder::finish() {
  m_monitoring = false;
  QMetaObject::invokeMethod(m_worker, "finish", Qt::QueuedConnection);
}

void VoiceOverRecorder::discard() {
  m_monitoring = false;
  m_cancelling = m_starting || m_sessionActive;
  QMetaObject::invokeMethod(m_worker, "discard", Qt::QueuedConnection);
}

void VoiceOverRecorder::peakLevels(float *left, float *right) {
  // Direct call, like PreviewAudioSink::peakLevels: it only reads atomics, so a
  // queued round trip would cost more than it protects.
  m_worker->peakLevels(left, right);
}

qint64 VoiceOverRecorder::segmentMs() const { return m_worker->segmentMs(); }
qint64 VoiceOverRecorder::sessionMs() const { return m_worker->sessionMs(); }
bool VoiceOverRecorder::capturing() const { return m_worker->capturing(); }
