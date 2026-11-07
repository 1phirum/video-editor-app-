#include "app/preview/timeline_thumbnail_provider.h"

#include "app/preview/timeline_thumbnail_service.h"

#include <QQuickTextureFactory>
#include <QRunnable>

namespace {

QImage fitted(const QImage &image, const QSize &requested) {
  if (image.isNull())
    return image;
  const int width = requested.width();
  const int height = requested.height();
  if (width <= 0 && height <= 0)
    return image;
  // Scaling down here rather than in the scene graph: a timeline row 60 px high
  // holding thirty 480 px tiles would otherwise upload fifteen times the texture
  // memory it can show.
  if (width > 0 && height > 0) {
    if (width >= image.width() && height >= image.height())
      return image;
    return image.scaled(width, height, Qt::KeepAspectRatio,
                        Qt::SmoothTransformation);
  }
  if (width > 0)
    return width >= image.width()
               ? image
               : image.scaledToWidth(width, Qt::SmoothTransformation);
  return height >= image.height()
             ? image
             : image.scaledToHeight(height, Qt::SmoothTransformation);
}

class TileResponse final : public QQuickImageResponse, public QRunnable {
public:
  TileResponse(QString path, qint64 positionMs, QSize requestedSize)
      : m_path(std::move(path)), m_positionMs(positionMs),
        m_requestedSize(requestedSize) {
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
    if (m_path.isEmpty()) {
      m_error = QStringLiteral("Unknown timeline thumbnail source.");
      emit finished();
      return;
    }
    const TimelineThumbnailService::Tile tile =
        TimelineThumbnailService::instance().tile(m_path, m_positionMs,
                                                 &m_cancel);
    if (tile.valid()) {
      m_image = fitted(tile.image, m_requestedSize);
    } else if (tile.cancelled ||
               m_cancel.load(std::memory_order_acquire)) {
      // A cancelled tile is the normal outcome of a scroll. Reporting an error
      // for it would turn every fast pan into a screenful of warnings, so the
      // response completes with a transparent pixel instead.
      m_image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
      m_image.fill(Qt::transparent);
    } else {
      m_error = tile.error.isEmpty()
                    ? QStringLiteral("No thumbnail could be produced.")
                    : tile.error;
    }
    emit finished();
  }

private:
  QString m_path;
  qint64 m_positionMs = 0;
  QSize m_requestedSize;
  QImage m_image;
  QString m_error;
  std::atomic_bool m_cancel{false};
};

} // namespace

TimelineThumbnailProvider::TimelineThumbnailProvider() {
  m_pool.setMaxThreadCount(2);
  // Threads are kept alive between pans: a scroll is a burst of requests and
  // paying thread creation on each burst shows up as a stutter.
  m_pool.setExpiryTimeout(30000);
}

TimelineThumbnailProvider::~TimelineThumbnailProvider() {
  // Responses are still owned by the engine at this point; the pool must not be
  // running any of them when it goes away.
  m_pool.clear();
  m_pool.waitForDone();
}
QQuickImageResponse *
TimelineThumbnailProvider::requestImageResponse(const QString &id,
                                               const QSize &requestedSize) {
  // "<token>/<positionMs>". The token stands in for the file path because a
  // Windows path inside an image:// URL does not survive the round trip: the
  // drive colon, the separators and any space in it are all rewritten.
  const int separator = id.indexOf(QLatin1Char('/'));
  QString token = id;
  qint64 positionMs = 0;
  if (separator > 0) {
    token = id.left(separator);
    positionMs = id.mid(separator + 1).toLongLong();
  }
  const QString path =
      TimelineThumbnailService::instance().pathForToken(token);

  auto *response = new TileResponse(path, qMax<qint64>(0, positionMs),
                                    requestedSize);
  // Newest first: after a zoom the queue is full of slots that no longer exist.
  m_pool.start(response, m_priority.fetch_add(1, std::memory_order_relaxed) + 1);
  return response;
}
