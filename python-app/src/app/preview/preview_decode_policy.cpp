#include "app/preview/preview_decode_policy.h"

#include "app/preview/decode_cost_model.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QScreen>
#include <QThread>
#include <QtMath>

namespace {

// Ceilings. Playback stops growing here because past this point the per-frame
// conversion and texture upload - not the decoder - is what breaks the frame
// rate. A still has no frame rate to hold, so it is allowed the full 4K.
constexpr int kPlaybackCeilingSide = 2560;
constexpr int kStillCeilingSide = 3840;
// Floors, used when the monitor has not reported its size yet or is docked
// small. 1080p is the floor rather than the panel size because the window can
// be maximised between two frames, and decoding a little more than is shown is
// cheaper than a visible resolution change mid-playback.
constexpr int kPlaybackFloorSide = 1920;
constexpr int kStillFloorSide = 2560;

QMutex g_surfaceMutex;
QSize g_surfacePixels;

// Scale-to-fit that never enlarges: a 720p source stays 720p instead of being
// blown up into a soft 1080p frame the file never contained.
QSize boundedSize(int sourceWidth, int sourceHeight, int maximumSide) {
  if (sourceWidth <= 0 || sourceHeight <= 0)
    return QSize(maximumSide, qMax(2, maximumSide * 9 / 16));

  const double scale =
      qMin(1.0, maximumSide / double(qMax(sourceWidth, sourceHeight)));
  int width = qMax(2, qRound(sourceWidth * scale));
  int height = qMax(2, qRound(sourceHeight * scale));
  // Even dimensions are accepted consistently by every FFmpeg scale path.
  width -= width % 2;
  height -= height % 2;
  return QSize(qMax(2, width), qMax(2, height));
}

int surfaceSide() {
  const QSize surface = PreviewDecodePolicy::surfaceSize();
  return qMax(surface.width(), surface.height());
}

int playbackMaximumSide() {
  return qBound(kPlaybackFloorSide, surfaceSide(), kPlaybackCeilingSide);
}

int stillMaximumSide() {
  return qBound(kStillFloorSide, surfaceSide(), kStillCeilingSide);
}

} // namespace

void PreviewDecodePolicy::setSurfaceSize(const QSize &logicalSize) {
  // Device pixels are what decides whether a frame is being upscaled on screen,
  // so the ratio is applied here rather than asked of QML. Clamped because a
  // bogus ratio would otherwise multiply the decode size.
  double ratio = 1.0;
  if (QGuiApplication::instance()) {
    if (const QScreen *screen = QGuiApplication::primaryScreen())
      ratio = qBound(1.0, screen->devicePixelRatio(), 3.0);
  }
  const QSize pixels(qMax(0, qRound(logicalSize.width() * ratio)),
                     qMax(0, qRound(logicalSize.height() * ratio)));
  QMutexLocker locker(&g_surfaceMutex);
  g_surfacePixels = pixels;
}

QSize PreviewDecodePolicy::surfaceSize() {
  QMutexLocker locker(&g_surfaceMutex);
  return g_surfacePixels;
}

QSize PreviewDecodePolicy::stillSize(int sourceWidth, int sourceHeight) {
  return boundedSize(sourceWidth, sourceHeight, stillMaximumSide());
}

QSize PreviewDecodePolicy::playbackSize(int sourceWidth, int sourceHeight) {
  return boundedSize(sourceWidth, sourceHeight, playbackMaximumSide());
}

PreviewDecodePolicy::Profile PreviewDecodePolicy::forSource(
    const QString &path, int sourceWidth, int sourceHeight,
    double requestedFrameRate, Intent intent) {
  constexpr qint64 mediumFile = 512LL * 1024 * 1024;
  const qint64 bytes = QFileInfo(path).size();
  const qint64 pixels = qint64(qMax(0, sourceWidth)) * qMax(0, sourceHeight);
  // Only the container probe and the decoder setup care about this now. It is
  // deliberately no longer allowed to touch the frame size.
  const bool lightweight = bytes >= mediumFile || pixels >= 3840LL * 2160;

  Profile result;
  result.lightweight = lightweight;
  // What the machine has been observed to manage on this source. Playback only:
  // a still is one frame the user is looking at, and it is allowed to be slow.
  const DecodeCostModel::Advice advice =
      DecodeCostModel::instance().adviceFor(path, sourceWidth, sourceHeight);
  int playbackSide = playbackMaximumSide();
  if (advice.maximumSide > 0)
    playbackSide = qMin(playbackSide, advice.maximumSide);
  result.frameSize = intent == Intent::Still
                         ? stillSize(sourceWidth, sourceHeight)
                         : boundedSize(sourceWidth, sourceHeight, playbackSide);
  result.nativeResolution =
      sourceWidth > 0 && sourceHeight > 0 &&
      result.frameSize == QSize(sourceWidth, sourceHeight);

  const double requested = requestedFrameRate > 0 ? requestedFrameRate : 30.0;
  // Above 1080p a 50/60 fps source is paced at 30: the frames are four times
  // the pixels and the monitor is a preview, not the deliverable.
  const qint64 framePixels =
      qint64(result.frameSize.width()) * result.frameSize.height();
  double ceilingFps = framePixels > 1920LL * 1088 ? 30.0 : 60.0;
  if (intent == Intent::Playback && advice.maximumFrameRate > 0)
    ceilingFps = qMin(ceilingFps, advice.maximumFrameRate);
  result.maximumFrameRate = qBound(1.0, qMin(requested, ceilingFps), 60.0);

  const int ideal = qMax(2, QThread::idealThreadCount());
  if (intent == Intent::Still) {
    // Frame threading delays output by roughly one frame per thread, and a
    // single still pays that delay in full. Keep it narrow so a scrub answers
    // quickly instead of decoding a queue nobody waits for.
    result.decoderThreads = qBound(2, ideal / 4, 4);
  } else if (lightweight || advice.expensive) {
    // Playback of a long 1080p/4K source is decode-bound, so give it real
    // threads. The old value of 2 was chosen when the frame was 480 px wide
    // and the picture, not the decoder, was being economised. An expensive
    // codec gets them whatever the file's size: software AV1 at 1080p is more
    // decode work than H.264 at 4K.
    result.decoderThreads = qBound(4, ideal / 2, 8);
  }

  if (lightweight) {
    // Large indexed MP4/MKV files should begin playback without FFmpeg
    // inspecting hundreds of megabytes. These bounds are only for preview.
    result.probeSizeBytes = 2 * 1024 * 1024;
    result.analyzeDurationUs = 1500000;
    result.audioBufferUs = 150000;
  }
  return result;
}
