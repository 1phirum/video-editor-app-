#pragma once

#include <QQuickAsyncImageProvider>
#include <QThreadPool>

#include <atomic>

// Serves "image://wave-window/<token>/<startMs>/<spanMs>[/<columns>]" to the
// timeline.
//
// The companion of TimelineThumbnailProvider, for audio. Where the tile provider
// answers "what does this source look like at this instant", this one answers
// "what does it sound like across this span" - and the span is the part of the
// clip currently on screen, so the envelope is drawn at the zoom the user is
// actually at rather than stretched out of a whole-file sheet.
//
// Same policies as the tile provider, for the same reasons: asynchronous so the
// GUI thread never waits on a decode, cancellable so a fast pan abandons work
// nobody will see, and newest-first so the visible windows are served before the
// ones that scrolled away.
class WaveformWindowProvider final : public QQuickAsyncImageProvider {
public:
  WaveformWindowProvider();
  ~WaveformWindowProvider() override;

  QQuickImageResponse *requestImageResponse(const QString &id,
                                            const QSize &requestedSize) override;

private:
  // Two workers, matching the thumbnail pool. Each keeps one warm container per
  // source (AudioPeakWindowService's thread-local reader), so a pan across one
  // clip is seeks rather than opens.
  QThreadPool m_pool;
  std::atomic<int> m_priority{0};
};
