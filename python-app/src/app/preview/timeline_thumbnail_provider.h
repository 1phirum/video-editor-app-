#pragma once

#include <QQuickAsyncImageProvider>
#include <QThreadPool>

#include <atomic>

// Serves "image://timeline-tile/<token>/<positionMs>" to the timeline.
//
// Asynchronous by design: an Image that asks for a thumbnail never blocks the
// GUI thread, and Qt calls cancel() on the request the moment the item is
// scrolled away or its source changes, so a decode nobody is waiting for stops
// instead of holding the pool.
//
// Requests run newest-first. After a zoom or a jump across the timeline, the
// tiles queued a moment ago are for slots that no longer exist; serving the
// latest ones first is what makes the strip appear to fill in from where the
// user is looking.
class TimelineThumbnailProvider final : public QQuickAsyncImageProvider {
public:
  TimelineThumbnailProvider();
  ~TimelineThumbnailProvider() override;

  QQuickImageResponse *requestImageResponse(const QString &id,
                                            const QSize &requestedSize) override;

private:
  // Two workers: enough to keep the strip filling while one of them is blocked
  // opening a new source, few enough to leave the monitor's decoder its cores.
  QThreadPool m_pool;
  std::atomic<int> m_priority{0};
};
