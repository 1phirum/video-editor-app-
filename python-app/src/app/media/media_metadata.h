#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>

// In-process container/stream probe.
//
// Backend::probeMedia used to shell out to ffprobe with a 5 second
// waitForFinished() and kill the process on timeout. On a 6.3 GB, 8 hour MP4 the
// moov atom alone is tens of megabytes, so the probe regularly lost the race,
// was killed, and returned an empty JSON document - which is why a long source
// showed 0x0, no frame rate and no channel count while a short one filled every
// field. There is no timeout to lose here: the same libavformat that the decoder
// already links reads the header directly, on the calling worker thread, with no
// process launch, no pipe and no JSON round trip.
class MediaMetadata final {
public:
  struct VideoStream {
    bool valid = false;
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    // Clockwise degrees from the display matrix. A phone clip is stored
    // landscape with rotation 90; ignoring it is what makes a portrait video
    // preview sideways.
    int rotationDegrees = 0;
    qint64 bitRate = 0;
    QString codec;
    QString pixelFormat;
    QString profile;
    // Streams with B-frames cannot be cut on an arbitrary frame without
    // re-encoding, and need a longer decode run after a seek.
    bool hasBFrames = false;
  };

  struct AudioStream {
    bool valid = false;
    int sampleRate = 0;
    int channels = 0;
    qint64 bitRate = 0;
    QString codec;
    QString sampleFormat;
  };

  struct Info {
    bool valid = false;
    QString error;
    qint64 durationMs = 0;
    qint64 sizeBytes = 0;
    qint64 bitRate = 0;
    QString formatName;
    int videoStreams = 0;
    int audioStreams = 0;
    int subtitleStreams = 0;
    // False when the container has no seek index. Those sources cannot be
    // scrubbed by seeking, so the filmstrip builder falls back to sequential
    // reads instead of issuing seeks that libav answers with
    // "Cannot find an index entry before timestamp".
    bool seekable = true;
    VideoStream video;
    AudioStream audio;

    // Frame size as the user should see it, with rotation applied.
    QSize displaySize() const;
    // Writes the keys Backend/QML already consume, plus the extra detail the
    // Info panel can show. Existing keys keep their exact names and types.
    void applyTo(QVariantMap *media) const;
  };

  // False in builds without direct FFmpeg linkage; callers then keep their
  // previous ffprobe path.
  static bool available();

  static Info probe(const QString &path);
};
