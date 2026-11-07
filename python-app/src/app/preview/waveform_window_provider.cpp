#include "app/preview/waveform_window_provider.h"

#include "app/preview/audio_peak_window_service.h"

#include <QQuickTextureFactory>
#include <QRunnable>
#include <QStringList>

namespace {

class WaveformResponse final : public QQuickImageResponse, public QRunnable {
public:
  WaveformResponse(QString path, qint64 startMs, qint64 spanMs, int columns,
                   QSize requestedSize)
      : m_path(std::move(path)), m_startMs(startMs), m_spanMs(spanMs),
        m_columns(columns), m_requestedSize(requestedSize) {
    // The engine owns the response and deletes it after finished(); letting the
    // pool delete it as well is a double free.
    setAutoDelete(false);
  }

  QQuickTextureFactory *textureFactory() const override {
    return QQuickTextureFactory::textureFactoryForImage(m_image);
  }

  QString errorString() const override { return m_error; }

  void cancel() override { m_cancel.store(true, std::memory_order_release); }

  void run() override {
    if (m_path.isEmpty() || m_spanMs <= 0) {
      m_error = QStringLiteral("Unknown waveform window source.");
      emit finished();
      return;
    }
    const AudioPeakWindowService::Window window =
        AudioPeakWindowService::instance().window(m_path, m_startMs, m_spanMs,
                                                 m_columns, &m_cancel);
    if (window.valid()) {
      m_image = AudioPeakWindowService::render(window.peaks, renderSize());
      if (m_image.isNull())
        m_error = QStringLiteral("Could not render the waveform window.");
    } else if (window.cancelled || m_cancel.load(std::memory_order_acquire)) {
      // A cancelled window is the normal outcome of a pan. Completing with a
      // transparent pixel keeps a fast scroll from logging a screenful of
      // warnings.
      m_image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
      m_image.fill(Qt::transparent);
    } else {
      m_error = window.error.isEmpty()
                    ? QStringLiteral("No waveform could be produced.")
                    : window.error;
    }
    emit finished();
  }

private:
  // The strip is drawn stretched vertically, so only the width has to match what
  // was asked for; a fixed height keeps every window the same texture size and
  // therefore batchable.
  QSize renderSize() const {
    const int width = m_requestedSize.width() > 0
                          ? qBound(64, m_requestedSize.width(), 2048)
                          : AudioPeakWindowService::kRenderWidth;
    const int height = m_requestedSize.height() > 0
                           ? qBound(16, m_requestedSize.height(), 512)
                           : AudioPeakWindowService::kRenderHeight;
    return QSize(width, height);
  }

  QString m_path;
  qint64 m_startMs = 0;
  qint64 m_spanMs = 0;
  int m_columns = AudioPeakWindowService::kDefaultColumns;
  QSize m_requestedSize;
  QImage m_image;
  QString m_error;
  std::atomic_bool m_cancel{false};
};

} // namespace

WaveformWindowProvider::WaveformWindowProvider() {
  m_pool.setMaxThreadCount(2);
  // Threads are kept alive between pans: a scroll is a burst of requests and
  // paying thread creation on each burst shows up as a stutter.
  m_pool.setExpiryTimeout(30000);
}

WaveformWindowProvider::~WaveformWindowProvider() {
  // Responses are still owned by the engine at this point; the pool must not be
  // running any of them when it goes away.
  m_pool.clear();
  m_pool.waitForDone();
}

QQuickImageResponse *
WaveformWindowProvider::requestImageResponse(const QString &id,
                                            const QSize &requestedSize) {
  // "<token>/<startMs>/<spanMs>[/<columns>]". The token stands in for the file
  // path because a Windows path inside an image:// URL does not survive the round
  // trip: the drive colon, the separators and any space in it are all rewritten.
  const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString token;
  qint64 startMs = 0;
  qint64 spanMs = 0;
  int columns = AudioPeakWindowService::kDefaultColumns;
  if (!parts.isEmpty())
    token = parts.at(0);
  if (parts.size() > 1)
    startMs = parts.at(1).toLongLong();
  if (parts.size() > 2)
    spanMs = parts.at(2).toLongLong();
  if (parts.size() > 3)
    columns = parts.at(3).toInt();

  const QString path = AudioPeakWindowService::instance().pathForToken(token);
  auto *response =
      new WaveformResponse(path, qMax<qint64>(0, startMs), spanMs, columns,
                           requestedSize);
  // Newest first: after a pan the queue is full of windows that have already
  // scrolled off screen.
  m_pool.start(response, m_priority.fetch_add(1, std::memory_order_relaxed) + 1);
  return response;
}
