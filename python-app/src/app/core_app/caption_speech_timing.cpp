#include "app/core_app/caption_speech_timing.h"

#include <QObject>
#include <QRunnable>
#include <QtGlobal>

#include "app/caption_word_clock.h"
#include "app/preview/audio_peak_window_service.h"

namespace {

// A cue below this is a single word or a stray fragment: there is nothing to
// distribute and the decode would cost more than the answer is worth.
constexpr qint64 kMinimumCueMs = 220;
// Above this a "cue" is not a spoken line - a title card, a badly imported
// subtitle - and a long decode for it would sit in front of the next real cue.
constexpr qint64 kMaximumCueMs = 15000;
// Envelope resolution. 20 ms a column is finer than the shortest gap between two
// spoken words, and the service caches a column array as floats, so this is a
// few hundred bytes a cue.
constexpr double kColumnMs = 20.0;
constexpr int kMinimumColumns = 48;
// Cues held before the table is dropped. A analysed cue is a short string, so
// this is tens of kilobytes; past it the user has scrubbed through more of the
// timeline than any one highlight needs to remember.
constexpr int kMaximumEntries = 4096;

} // namespace

// Owns nothing: the request is copied in, and the service and the word clock are
// both callable from any thread.
class CaptionSpeechTimingJob final : public QRunnable {
public:
  CaptionSpeechTimingJob(CaptionSpeechTiming *timing,
                         CaptionSpeechTiming::Request request)
      : m_timing(timing), m_request(std::move(request)) {
    setAutoDelete(true);
  }

  void run() override {
    if (m_timing->m_shutdown.load(std::memory_order_acquire))
      return;
    const QString packed =
        CaptionSpeechTiming::analyse(m_request, &m_timing->m_shutdown);
    if (m_timing->m_shutdown.load(std::memory_order_acquire))
      return;
    m_timing->store(m_request.key, packed);
    m_timing->notifyReady();
  }

private:
  CaptionSpeechTiming *m_timing = nullptr;
  CaptionSpeechTiming::Request m_request;
};

CaptionSpeechTiming::CaptionSpeechTiming(QObject *owner) : m_owner(owner) {
  // One at a time. The cue under the playhead is the only one anybody is looking
  // at, and a second decode thread would only take FFmpeg time away from
  // playback, which is decoding the same file.
  m_pool.setMaxThreadCount(1);
  m_pool.setObjectName(QStringLiteral("caption-speech-timing"));
}

CaptionSpeechTiming::~CaptionSpeechTiming() {
  // The cancel token is what a decode in flight is watching, so it goes first.
  m_shutdown.store(true, std::memory_order_release);
  m_pool.clear();
  m_pool.waitForDone();
}

void CaptionSpeechTiming::setReadyCallback(std::function<void()> callback) {
  QMutexLocker locker(&m_mutex);
  m_ready = std::move(callback);
}

CaptionSpeechTiming::Request
CaptionSpeechTiming::requestFor(const QString &source, qint64 audioStartMs,
                                qint64 cueMs, const QStringList &words) {
  Request request;
  if (source.isEmpty() || audioStartMs < 0 || words.size() < 2 ||
      cueMs < kMinimumCueMs || cueMs > kMaximumCueMs ||
      !AudioPeakWindowService::available())
    return request;
  request.source = source;
  request.audioStartMs = audioStartMs;
  request.cueMs = cueMs;
  request.words = words;
  // The text is in the key because the split is weighted by it: an edited line
  // with the same word count still has to be fitted again.
  request.key = QStringLiteral("%1|%2|%3|%4")
                    .arg(source)
                    .arg(audioStartMs)
                    .arg(cueMs)
                    .arg(qHash(words.join(QLatin1Char(' '))));
  return request;
}

QString CaptionSpeechTiming::analyse(const Request &request,
                                     const std::atomic_bool *cancel) {
  // Pitch first. A cue over music or room noise is loud from end to end, so the
  // envelope below would see one unbroken "voice" and split the cue evenly;
  // periodicity is what tells a syllable from a snare, and the pitch contour is
  // what says where one syllable ends and the next starts.
  const AudioPeakWindowService::Samples pcm =
      AudioPeakWindowService::instance().samples(
          request.source, request.audioStartMs, request.cueMs, cancel);
  if (pcm.valid()) {
    const QVector<CaptionWordClock::Span> spans = CaptionWordClock::fromVoice(
        CaptionVoiceYin::analyse(pcm.mono, pcm.sampleRate), request.words,
        double(request.cueMs));
    if (!spans.isEmpty())
      return CaptionWordClock::pack(spans);
  }
  if (pcm.cancelled)
    return QString();

  // No samples to be had - a build without direct FFmpeg linkage, or a source
  // that would not decode - so fall back to the shape of the waveform, which is
  // still better than dividing the cue by word length alone.
  const int columns =
      qBound(kMinimumColumns, int(request.cueMs / kColumnMs),
             AudioPeakWindowService::kDefaultColumns);
  const AudioPeakWindowService::Window window =
      AudioPeakWindowService::instance().window(request.source,
                                               request.audioStartMs,
                                               request.cueMs, columns, cancel);
  if (!window.valid())
    return QString();
  return CaptionWordClock::pack(CaptionWordClock::fromEnvelope(
      window.peaks, request.words, double(request.cueMs)));
}

void CaptionSpeechTiming::store(const QString &key, const QString &packed) {
  QMutexLocker locker(&m_mutex);
  if (m_done.size() >= kMaximumEntries)
    m_done.clear();
  m_done.insert(key, packed);
  m_running.remove(key);
}

void CaptionSpeechTiming::notifyReady() {
  std::function<void()> ready;
  {
    QMutexLocker locker(&m_mutex);
    ready = m_ready;
  }
  if (!ready || !m_owner)
    return;
  // Queued onto the owner's thread: this runs on a pool thread, and the caller
  // is going to touch its own state and emit a signal.
  QMetaObject::invokeMethod(m_owner, [ready]() { ready(); },
                            Qt::QueuedConnection);
}

QString CaptionSpeechTiming::timingFor(const QString &source,
                                       qint64 audioStartMs, qint64 cueMs,
                                       const QStringList &words) {
  const Request request = requestFor(source, audioStartMs, cueMs, words);
  if (request.key.isEmpty())
    return QString();
  {
    QMutexLocker locker(&m_mutex);
    const auto known = m_done.constFind(request.key);
    if (known != m_done.constEnd())
      return known.value();
    if (m_running.contains(request.key))
      return QString();
    m_running.insert(request.key);
  }
  m_pool.start(new CaptionSpeechTimingJob(this, request));
  return QString();
}

QString CaptionSpeechTiming::timingNow(const QString &source,
                                       qint64 audioStartMs, qint64 cueMs,
                                       const QStringList &words) {
  const Request request = requestFor(source, audioStartMs, cueMs, words);
  if (request.key.isEmpty())
    return QString();
  {
    QMutexLocker locker(&m_mutex);
    const auto known = m_done.constFind(request.key);
    if (known != m_done.constEnd())
      return known.value();
  }
  // Whatever the monitor already looked at is a cache hit above; the rest is
  // decoded here, which is why the caller bounds how many it asks for.
  const QString packed = analyse(request, &m_shutdown);
  store(request.key, packed);
  return packed;
}

void CaptionSpeechTiming::clear() {
  QMutexLocker locker(&m_mutex);
  m_done.clear();
}
