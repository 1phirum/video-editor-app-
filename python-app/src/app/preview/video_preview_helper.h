#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "core/module_api.h"

// Resolves the active program-monitor clip in C++ and caches it until the next
// timeline boundary. This avoids scanning every clip and subtitle from QML on
// every playback tick.
//
// The resolve itself used to be a linear scan of every clip, with a nested scan
// of the track states (and two toUpper() allocations per pair) inside it. That
// is fine for a hand-cut sequence and ruinous for an imported caption track: at
// 19,831 subtitle clips one resolve cost about half a second of GUI thread, and
// playback crosses a cue boundary every second or two, so the 50 ms UI tick was
// only firing every ~300 ms and the playhead advanced in 300-900 ms lurches.
//
// So the clips are indexed once per timeline change into per-track arrays sorted
// by start time, and a resolve binary-searches them. Track enablement is
// resolved once into a hash instead of being recomputed per clip.
class CUTPRO_PREVIEW_API VideoPreviewHelper final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariant activeClip READ activeClip NOTIFY activeStateChanged)
  Q_PROPERTY(QVariant activeMedia READ activeMedia NOTIFY activeStateChanged)
  Q_PROPERTY(QVariant activeAudioClip READ activeAudioClip NOTIFY activeStateChanged)
  Q_PROPERTY(QVariantList activeAudioClips READ activeAudioClips NOTIFY activeStateChanged)
  Q_PROPERTY(QVariant activeSubtitle READ activeSubtitle NOTIFY activeStateChanged)
  Q_PROPERTY(QString activeClipId READ activeClipId NOTIFY activeStateChanged)
  Q_PROPERTY(int uiTickInterval READ uiTickInterval CONSTANT)

public:
  explicit VideoPreviewHelper(QObject *parent = nullptr);

  QVariant activeClip() const {
    return m_activeClip.isEmpty() ? QVariant{} : QVariant{m_activeClip};
  }
  QVariant activeMedia() const {
    return m_activeMedia.isEmpty() ? QVariant{} : QVariant{m_activeMedia};
  }
  QVariant activeAudioClip() const {
    return m_activeAudioClip.isEmpty() ? QVariant{} : QVariant{m_activeAudioClip};
  }
  QVariantList activeAudioClips() const { return m_activeAudioClips; }
  QVariant activeSubtitle() const {
    return m_activeSubtitle.isEmpty() ? QVariant{} : QVariant{m_activeSubtitle};
  }
  QString activeClipId() const { return m_activeClip.value("id").toString(); }
  int uiTickInterval() const { return 50; }

  void setTimeline(const QVariantList &clips, const QVariantList &media);
  void setTrackStates(const QVariantList &trackStates);
  void setCustomPreviewClipId(const QString &clipId);
  void setPosition(qint64 positionMs);

signals:
  void activeStateChanged();

private:
  // What a clip contributes to a resolve, with the strings already turned into
  // the two numbers and one enum the scan actually compares. Everything else
  // stays in m_clips and is only read for the handful of clips that win.
  // Overlay is a picture that composites on top of the base clip (a logo, a
  // graphic, a shape). It is deliberately excluded from base-picture selection
  // so it never replaces the clip the monitor decodes - the QML overlay layer
  // draws it above the decoded frame instead.
  enum class Kind : quint8 { Picture, Audio, Subtitle, Effect, Overlay };
  struct Span {
    qint64 startMs = 0;
    qint64 endMs = 0;
    int clipIndex = -1;
    int rank = 0;
    Kind kind = Kind::Picture;
  };
  // One track's spans, sorted by start, plus the two lookaside arrays that make
  // a position query a pair of binary searches:
  //  * maxEnd[i] is the greatest end among spans 0..i, so the reverse walk over
  //    candidates can stop as soon as no earlier span can still be open.
  //  * sortedEnds answers "the first edge after the playhead" for ends, the way
  //    the sorted starts already do for starts.
  struct TrackIndex {
    QString id;
    bool enabled = true;
    QVector<Span> spans;
    QVector<qint64> starts;
    QVector<qint64> maxEnd;
    QVector<qint64> sortedEnds;
  };

  void rebuildIndexes();
  void rebuildTrackEnabled();
  void resolve(bool force = false);
  QVariantMap clipAt(int index) const;
  QVariantMap mediaForClip(const QVariantMap &clip) const;
  QVariantMap audioForClip(const QVariantMap &clip) const;

  QVariantList m_clips;
  QVariantList m_media;
  QVariantList m_trackStates;
  QHash<QString, QVariantMap> m_mediaById;
  QHash<QString, int> m_clipIndexById;
  // Extracted audio, keyed by the id of the video clip it came out of. That
  // clip's sound lives on its own lane now, so its level, mute and vocal
  // replacement are read from there rather than from the video clip.
  QHash<QString, int> m_audioIndexByExtractedFrom;
  QHash<QString, int> m_audioIndexByLinkGroup;
  QList<TrackIndex> m_tracks;
  QVariantMap m_activeClip;
  QVariantMap m_activeMedia;
  QVariantMap m_activeAudioClip;
  QVariantList m_activeAudioClips;
  QVariantMap m_activeSubtitle;
  QString m_customPreviewClipId;
  qint64 m_positionMs = 0;
  qint64 m_nextBoundaryMs = 0;
  qint64 m_lastResolvedPositionMs = -1;
};
