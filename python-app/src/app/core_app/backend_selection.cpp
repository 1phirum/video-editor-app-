#include "app/core_app/backend.h"

#include "app/lumetri/color_settings.h"
#include "app/effects/effect_registry.h"
#include "app/preview/gui_thread_watchdog.h"

QVariantMap Backend::activeColorClip() const {
  const QVariantMap explicitlySelected = selectedClip();
  if (!explicitlySelected.isEmpty() &&
      explicitlySelected.value("track").toString().startsWith('V')) {
    QVariantMap selected = explicitlySelected;
    if (!selected.contains("lumetri"))
      selected["lumetri"] = ColorSettings::clipDefaults();
    return selected;
  }
  QVariantMap selected;
  int selectedRank = -1;
  bool hasSoloVideoTrack = false;
  for (const auto &value : trackStates()) {
    const QVariantMap state = value.toMap();
    if (state.value("id").toString().startsWith('V') &&
        state.value("solo").toBool()) {
      hasSoloVideoTrack = true;
      break;
    }
  }
  // Only the picture clips can win here, and the cache already knows which
  // indexes those are. Walking all of m_clips instead meant a subtitle track
  // made this scan twenty thousand entries long - and colorSettingsChanged
  // fires on every cue boundary while playing, so it ran that often.
  ensureClipCaches();
  for (const int index : m_cachedVideoClips) {
    const QVariantMap clip = m_clips.at(index).toMap();
    const QString track = clip.value("track").toString();
    if (clip.value("enabled", true).toBool() == false || !trackVisible(track) ||
        (hasSoloVideoTrack && !trackSolo(track)) ||
        m_playheadMs < clip.value("startMs").toLongLong() ||
        m_playheadMs >= clip.value("startMs").toLongLong() +
                            clip.value("durationMs").toLongLong())
      continue;
    const int rank = track.mid(1).toInt();
    if (rank > selectedRank) {
      selected = clip;
      selectedRank = rank;
    }
  }
  if (!selected.isEmpty() && !selected.contains("lumetri"))
    selected["lumetri"] = ColorSettings::clipDefaults();
  return selected;
}

QVariantMap Backend::selectedClip() const {
  const int index = clipIndex(m_selectedClipId);
  return index < 0 ? QVariantMap{} : m_clips.at(index).toMap();
}

QVariantList Backend::effectDefinitions() const {
  return EffectRegistry::definitions();
}

void Backend::setSelectedClipId(const QString &clipId) {
  const QString normalized = clipIndex(clipId) >= 0 ? clipId : QString();
  if (normalized == m_selectedClipId)
    return;
  if (!m_customBlurEditClipId.isEmpty() &&
      normalized != m_customBlurEditClipId)
    endCustomBlurMaskEdit();
  m_selectedClipId = normalized;
  // Two notifications, deliberately not one.
  //
  // This is the identity half: it notifies selectedClipId, a QString, and the
  // timeline's own highlight is driven from QML-local state, so nothing
  // expensive is attached to it. It has to be synchronous - it is the
  // acknowledgement of a click - and now it can afford to be.
  {
    CUTPRO_GUI_SCOPE("Backend::setSelectedClipId/selectionChanged");
    emit selectionChanged();
  }
  // The detail half, and the colour panel, are not. Both rebuild Repeater
  // delegate trees from scratch, which is how a drop onto V1 turned into a
  // 72-second freeze; see m_selectionDetailNotify's comment for the trace.
  // Deferring costs the user nothing: the getters are unchanged, only the
  // moment QML re-reads them moves, and it moves out of the gesture.
  m_selectionDetailNotify.schedule();
  m_colorSettingsNotify.schedule();
}

QVariantMap Backend::activeColorMedia() const {
  const QVariantMap clip = activeColorClip();
  return clip.isEmpty() ? QVariantMap{}
                        : mediaById(clip.value("mediaId").toString());
}

bool Backend::setColorSetting(const QString &key, const QVariant &value) {
  QVariantMap updated = m_colorSettings;
  if (!ColorSettings::setProjectValue(&updated, key, value))
    return false;
  rememberState();
  m_colorSettings = updated;
  markDirty();
  // A slider drag calls this once per pointer move. Collapsing the notification
  // means one panel re-read per frame instead of one per move.
  m_colorSettingsNotify.schedule();
  return true;
}

bool Backend::setClipColorSetting(const QString &clipId, const QString &key,
                                  const QVariant &value) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  if (!clip.value("track").toString().startsWith('V'))
    return false;
  QVariantMap settings = ColorSettings::clipDefaults();
  const QVariantMap existing = clip.value("lumetri").toMap();
  for (auto it = existing.cbegin(); it != existing.cend(); ++it)
    settings[it.key()] = it.value();
  if (!ColorSettings::setClipValue(&settings, key, value))
    return false;
  rememberState();
  clip["lumetri"] = settings;
  m_clips[index] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  m_colorSettingsNotify.schedule();
  return true;
}

bool Backend::resetClipColorSettings(const QString &clipId) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  if (clip.value("lumetri").toMap().isEmpty())
    return false;
  rememberState();
  clip.remove("lumetri");
  m_clips[index] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  m_colorSettingsNotify.schedule();
  return true;
}
