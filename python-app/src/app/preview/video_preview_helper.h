#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Resolves the active program-monitor clip in C++ and caches it until the next
// timeline boundary. This avoids scanning every clip and subtitle from QML on
// every playback tick.
class VideoPreviewHelper final : public QObject {
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
  bool trackEnabled(const QString &track) const;
  void rebuildIndexes();
  void resolve(bool force = false);
  QVariantMap mediaForClip(const QVariantMap &clip) const;
  QVariantMap audioForClip(const QVariantMap &clip) const;

  QVariantList m_clips;
  QVariantList m_media;
  QVariantList m_trackStates;
  QHash<QString, QVariantMap> m_mediaById;
  QHash<QString, QVariantMap> m_clipById;
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
