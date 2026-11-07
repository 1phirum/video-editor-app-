#include "app/core_app/backend.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>

double Backend::noiseRemovalProgress() const {
  return m_noiseApplying && !m_noiseJobs.isEmpty()
      ? (m_noiseJobIndex + m_noiseRemoval.progress()) / m_noiseJobs.size()
      : m_noiseRemoval.progress();
}

QString Backend::clipReplacementAudioPath(const QVariantMap &clip) const {
  return NoiseRemoval::replacementPath(clip,
      mediaById(clip.value("mediaId").toString()));
}

QStringList Backend::noiseRemovalTargets(const QStringList &requested) const {
  QStringList targets;
  for (const QString &id : editableClipIds(requested, "audio")) {
    int index = clipIndex(id);
    if (m_clips.at(index).toMap().value("separateAudio").toBool()) {
      int audioIndex = -1;
      for (const int peer : audioPeerIndexes(index)) {
        if (m_clips.at(peer).toMap().value("kind") == "audio") {
          audioIndex = peer;
          break;
        }
      }
      if (audioIndex < 0)
        continue;
      index = audioIndex;
    }
    const QString target = m_clips.at(index).toMap().value("id").toString();
    if (!targets.contains(target) && !editableClipIds({target}, "audio").isEmpty())
      targets.append(target);
  }
  return targets;
}

void Backend::ensureNoiseRemoval() {
  if (m_noiseWired)
    return;
  m_noiseWired = true;
  connect(&m_noiseRemoval, &NoiseRemoval::changed, this, &Backend::noiseRemovalChanged);
  connect(&m_noiseRemoval, &NoiseRemoval::finished, this,
          [this](const QString &path, const QString &error) {
    if (m_noiseCancelled || !error.isEmpty()) {
      finishNoiseRemoval(false, m_noiseCancelled ? tr("Noise removal cancelled.") : error);
      return;
    }
    if (m_noiseApplying) {
      QVariantMap job = m_noiseJobs.at(m_noiseJobIndex).toMap();
      job["output"] = path;
      m_noiseJobs[m_noiseJobIndex++] = job;
      startNextNoiseRemoval();
      return;
    }
    if (!m_noisePlayer) {
      m_noisePlayer = new QMediaPlayer(this);
      m_noiseOutput = new QAudioOutput(this);
      m_noisePlayer->setAudioOutput(m_noiseOutput);
      connect(m_noisePlayer, &QMediaPlayer::playbackStateChanged, this,
              [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::StoppedState) {
          m_noisePreview.clear();
          if (!m_noiseApplying && !m_noiseRemoval.busy())
            m_noiseStatus.clear();
          emit noiseRemovalChanged();
        }
      });
      connect(m_noisePlayer, &QMediaPlayer::errorOccurred, this,
              [this](QMediaPlayer::Error, const QString &message) {
        m_noisePreview.clear();
        m_noiseStatus = message;
        emit noiseRemovalChanged();
      });
    }
    m_noisePlayer->setSource(QUrl::fromLocalFile(path));
    m_noiseOutput->setVolume(1.0);
    m_noisePlayer->play();
    m_noiseStatus = tr("Playing %1 sample").arg(m_noisePreview == "original"
        ? tr("original") : tr("cleaned"));
    emit noiseRemovalChanged();
  });
}

bool Backend::previewNoiseRemoval(const QString &clipId, int strength, bool original) {
  if (noiseRemovalBusy() || strength < 0 || strength > 100)
    return false;
  const QStringList targets = noiseRemovalTargets({clipId});
  if (targets.isEmpty())
    return false;
  stopNoiseRemovalPreview();
  stopVoicePreview();
  setPlaying(false);
  ensureNoiseRemoval();
  m_noiseCancelled = false;
  const QVariantMap clip = clipById(targets.first());
  const QVariantMap media = mediaById(clip.value("mediaId").toString());
  const double speed = qMax(0.01, clip.value("effects").toMap().value("speed", 100.0).toDouble());
  const qint64 duration = qMin<qint64>(5000, qRound64(
      clip.value("durationMs").toLongLong() * speed / 100.0));
  if (duration <= 0)
    return false;
  m_noisePreview = original ? QStringLiteral("original") : QStringLiteral("cleaned");
  m_noiseStatus = tr("Preparing sample...");
  if (!m_noiseRemoval.start(NoiseRemoval::sourcePath(clip, media), original ? 0 : strength,
                            clip.value("sourceInMs").toLongLong(), duration)) {
    finishNoiseRemoval(false, tr("The source audio or bundled noise-removal model is unavailable."));
    return false;
  }
  emit noiseRemovalChanged();
  return true;
}

bool Backend::applyClipsNoiseRemoval(const QStringList &requested, int strength) {
  if (noiseRemovalBusy() || strength < 1 || strength > 100)
    return false;
  const QStringList ids = noiseRemovalTargets(requested);
  if (ids.isEmpty())
    return false;
  stopNoiseRemovalPreview();
  setPlaying(false);
  ensureNoiseRemoval();
  m_noiseCancelled = false;
  m_noiseApplying = true;
  m_noiseStrength = strength;
  m_noiseJobIndex = 0;
  m_noiseProjectId = projectId();
  m_noiseSequenceId = sequenceId();
  m_noiseJobs.clear();
  for (const QString &id : ids) {
    const QVariantMap clip = clipById(id);
    const QVariantMap media = mediaById(clip.value("mediaId").toString());
    m_noiseJobs.append(QVariantMap{{"id", id}, {"clip", clip},
        {"input", NoiseRemoval::sourcePath(clip, media)},
        {"durationMs", media.value("durationMs")}});
  }
  startNextNoiseRemoval();
  return true;
}

void Backend::startNextNoiseRemoval() {
  if (m_noiseJobIndex < m_noiseJobs.size()) {
    const QVariantMap job = m_noiseJobs.at(m_noiseJobIndex).toMap();
    m_noiseStatus = tr("Cleaning clip %1 of %2...").arg(m_noiseJobIndex + 1).arg(m_noiseJobs.size());
    if (!m_noiseRemoval.start(job.value("input").toString(), m_noiseStrength,
                              0, 0, job.value("durationMs").toLongLong()))
      finishNoiseRemoval(false, tr("The source audio or bundled noise-removal model is unavailable."));
    emit noiseRemovalChanged();
    return;
  }
  // Render all selected clips before one undoable commit. Cancelling a batch
  // never leaves a partially modified selection.
  for (const QVariant &value : m_noiseJobs) {
    const QVariantMap job = value.toMap();
    const QString id = job.value("id").toString();
    const QVariantMap current = clipById(id);
    if (projectId() != m_noiseProjectId || sequenceId() != m_noiseSequenceId ||
        current != job.value("clip").toMap() ||
        noiseRemovalTargets({id}) != QStringList{id} ||
        NoiseRemoval::sourcePath(current, mediaById(current.value("mediaId").toString()))
            != job.value("input").toString()) {
      finishNoiseRemoval(false, tr("The clips changed while cleaning. Nothing was applied; try again."));
      return;
    }
  }
  beginEditBatch();
  for (const QVariant &value : m_noiseJobs) {
    const QVariantMap job = value.toMap();
    const int index = clipIndex(job.value("id").toString());
    QVariantMap clip = m_clips.at(index).toMap();
    QVariantMap effects = clip.value("effects").toMap();
    effects["noiseRemoval"] = true;
    effects["noiseRemovalPath"] = job.value("output");
    effects["noiseRemovalInputPath"] = job.value("input");
    effects["noiseRemovalStrength"] = m_noiseStrength;
    clip["effects"] = effects;
    m_clips[index] = clip;
  }
  markDirty();
  notifyClipEdit(true);
  endEditBatch();
  finishNoiseRemoval(true, QString());
}

void Backend::finishNoiseRemoval(bool ok, const QString &error) {
  m_noiseApplying = false;
  m_noiseJobs.clear();
  m_noisePreview.clear();
  m_noiseStatus = ok ? tr("Noise removal applied. Original audio preserved.") : error;
  emit noiseRemovalChanged();
  emit noiseRemovalFinished(ok, error);
}

bool Backend::restoreClipsNoiseRemoval(const QStringList &requested) {
  if (noiseRemovalBusy())
    return false;
  const QStringList targets = noiseRemovalTargets(requested);
  QStringList ids;
  for (const QString &id : targets) {
    if (clipById(id).value("effects").toMap().contains("noiseRemoval"))
      ids.append(id);
  }
  if (ids.isEmpty())
    return false;
  stopNoiseRemovalPreview();
  beginEditBatch();
  for (const QString &id : ids) {
    const int index = clipIndex(id);
    QVariantMap clip = m_clips.at(index).toMap();
    QVariantMap effects = clip.value("effects").toMap();
    for (const QString &key : {QString("noiseRemoval"), QString("noiseRemovalPath"),
         QString("noiseRemovalInputPath"), QString("noiseRemovalStrength")})
      effects.remove(key);
    clip["effects"] = effects;
    m_clips[index] = clip;
  }
  markDirty();
  notifyClipEdit(true);
  endEditBatch();
  m_noiseStatus = tr("Original audio restored.");
  emit noiseRemovalChanged();
  return true;
}

void Backend::stopNoiseRemovalPreview() {
  if (m_noisePlayer)
    m_noisePlayer->stop();
  if (!m_noiseApplying && m_noiseRemoval.busy()) {
    m_noiseCancelled = true;
    m_noiseRemoval.cancel();
  }
  m_noisePreview.clear();
  emit noiseRemovalChanged();
}

void Backend::cancelNoiseRemoval() {
  m_noiseCancelled = true;
  if (m_noisePlayer)
    m_noisePlayer->stop();
  m_noisePreview.clear();
  m_noiseRemoval.cancel();
  emit noiseRemovalChanged();
}
