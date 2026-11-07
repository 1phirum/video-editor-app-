#pragma once

#include <QSize>
#include <QString>
#include <QtGlobal>

// Bounded decode settings for the interactive program monitor. Export keeps
// using the original source and is intentionally unaffected by this policy.
// Keeping this outside Backend prevents file-size and decoder tuning from
// leaking into the application facade.
//
// The bounds are on *work*, not on picture. The monitor decodes at the source's
// own resolution, and only trades that down when the source is larger than the
// panel it is drawn into - in which case the extra pixels could not be seen
// anyway. Nothing here decodes below what is on screen: a monitor softened by
// the decoder cannot be sharpened again at export, whereas a frame decoded at
// the display's own resolution is what the file actually looks like. Choosing a
// smaller output belongs to the export dialog, which reads the source directly.
//
// What still reacts to how large the file is: the container probe, the decoder
// thread count, the audio buffer, and the playback frame rate ceiling.
class PreviewDecodePolicy final {
public:
  // Playback has to hold a frame rate, so a 4K source is paced and bounded more
  // tightly while it plays. A still has one frame to produce and no pacing to
  // keep, so it is decoded as large as the source is.
  enum class Intent { Playback, Still };

  struct Profile {
    QSize frameSize;
    double maximumFrameRate = 60.0;
    int probeSizeBytes = 0;
    qint64 analyzeDurationUs = 0;
    int decoderThreads = 0;
    int audioBufferUs = 250000;
    bool lightweight = false;
    // frameSize is exactly the source's resolution, so swscale only converts
    // pixel format and the monitor shows the decoded picture unresampled.
    bool nativeResolution = false;
  };

  // Size of the widget the frames are drawn into, in QML logical units; the
  // screen's device pixel ratio is applied here. Reported by the monitor so a
  // maximised window gets more picture and a docked one does not pay for
  // pixels it cannot show. Unset means "assume a 1080p panel".
  static void setSurfaceSize(const QSize &logicalSize);
  static QSize surfaceSize();

  static Profile forSource(const QString &path, int sourceWidth,
                           int sourceHeight, double requestedFrameRate,
                           Intent intent = Intent::Playback);

  // Frame bound for a single still: a scrub position, a paused monitor, a
  // poster frame. Larger than the playback bound because it is decoded once.
  static QSize stillSize(int sourceWidth, int sourceHeight);
  // Frame bound for streaming playback, where a frame rate has to be held.
  static QSize playbackSize(int sourceWidth, int sourceHeight);
};
