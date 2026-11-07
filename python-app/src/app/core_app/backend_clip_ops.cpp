// Premiere-style timeline clip operations, kept out of backend.cpp.
//
// Everything here mutates the loose QVariantMap clips in m_clips in place and
// follows the same rhythm as the rest of the timeline editor: resolve indexes,
// rememberState() for undo, mutate, markDirty(), then emit clipsChanged() /
// timelineChanged(). Speed / field / interpolation are stored on each clip's
// intrinsic `effects` map (see ClipEffects) and read back by the export builder;
// label / groupId sit on the clip map itself.

#include "app/core_app/backend.h"

#include "app/effects/clip_effects.h"
#include "app/effects/effect_registry.h"
#include "app/effects/effect_stack.h"
#include "app/media/ffmpeg_runtime.h"
#include "core/ids.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QScopedValueRollback>
#include <QStringList>
#include <QtGlobal>
#include <QtMath>

#include <algorithm>
#include <limits>

namespace {
QString newClipOpId(const char *prefix) {
  return QString::fromStdString(core::new_id(prefix));
}
// Merge a clip's stored effects over the defaults so a partial map still
// validates every key the same way setClipEffectSetting does.
QVariantMap mergedEffects(const QVariantMap &clip) {
  QVariantMap effects = ClipEffects::defaults();
  const QVariantMap existing = clip.value("effects").toMap();
  for (auto it = existing.cbegin(); it != existing.cend(); ++it)
    effects[it.key()] = it.value();
  return effects;
}
} // namespace

QStringList Backend::editableClipIds(const QStringList &ids,
                                     const QString &mediaType) const {
  QStringList result;
  QSet<QString> seen;
  for (const QString &id : ids) {
    if (seen.contains(id))
      continue;
    seen.insert(id);
    const int index = clipIndex(id);
    if (index < 0)
      continue;
    const QVariantMap clip = m_clips.at(index).toMap();
    const QString kind = clip.value("kind").toString();
    if (trackLocked(clip.value("track").toString()))
      continue;
    if (mediaType == "video" && kind != "video" && kind != "image")
      continue;
    if (mediaType == "movingVideo" && kind != "video")
      continue;
    if (mediaType == "audio" && kind != "audio" &&
        (kind != "video" || mediaById(clip.value("mediaId").toString())
                                 .value("channels").toInt() <= 0))
      continue;
    if (mediaType == "effects" && kind == "subtitle")
      continue;
    result.append(id);
  }
  return result;
}

void Backend::setEffectEditSelection(const QStringList &ids) {
  if (m_effectEditSelection == ids)
    return;
  m_effectEditSelection = ids;
  emit selectionChanged();
}

QStringList Backend::effectEditTargets(const QString &primary) const {
  return editableClipIds(m_effectEditSelection.contains(primary)
                             ? m_effectEditSelection : QStringList{primary},
                         "effects");
}

QVariantMap Backend::applyEffectSelectionCommand(const QString &primary,
                                                 const QString &command,
                                                 const QVariantMap &args) {
  return applyClipSelectionCommand(effectEditTargets(primary), command, args,
                                   primary);
}

QVariantMap Backend::applyClipSelectionCommand(const QStringList &requested,
                                               const QString &command,
                                               const QVariantMap &args,
                                               const QString &primary) {
  const QString key = args.value("key").toString();
  static const QSet<QString> audioKeys{
      "volumeDb", "volumeBypass", "channelVolumeLeft", "channelVolumeRight",
      "balance", "pan", "vocalRemoval", "noiseReduction", "highPassHz",
      "lowPassHz", "compressor"};
  QString type = command == "split" ? QString() : QStringLiteral("effects");
  if (command == "field" || command == "color" || command == "colorPreset")
    type = "video";
  if (command == "setting")
    type = audioKeys.contains(key) ? "audio" : "video";
  QStringList ids = editableClipIds(requested, type);
  if (ids.isEmpty())
    return {{"changed", 0}};

  // Match the same occurrence of an effect, rather than reusing a unique
  // instance ID from another clip. Capture this before any removal/reorder.
  const QString instanceId = args.value("instanceId").toString();
  QString definitionId;
  int occurrence = 0;
  if (!instanceId.isEmpty()) {
    const QVariantList stack = clipById(primary).value("effectStack").toList();
    for (const QVariant &v : stack) {
      const QVariantMap instance = v.toMap();
      if (instance.value("id").toString() == instanceId) {
        definitionId = instance.value("definitionId").toString();
        break;
      }
    }
    for (const QVariant &v : stack) {
      const QVariantMap instance = v.toMap();
      if (instance.value("id").toString() == instanceId)
        break;
      if (instance.value("definitionId").toString() == definitionId)
        ++occurrence;
    }
  }

  if (command == "setting" && key == "vocalRemoval" && !args.value("value").toBool()) {
    for (const QString &id : ids)
      m_demucsSelectionQueue.removeAll(id);
    if (ids.contains(m_demucsClipId) && demucsInProgress()) {
      m_demucsSmoothTimer.stop();
      m_demucsProcess.kill();
    }
  }
  const bool vocalBatch = command == "setting" && key == "vocalRemoval" && args.value("value").toBool();
  QScopedValueRollback<bool> vocalGuard(m_demucsBatchSetting, vocalBatch);
  const QVariantList before = m_clips;
  const bool snapshotNeeded = !m_liveParameterEditActive || !m_liveParameterSnapshotTaken;
  const bool live = command == "setting" || command == "parameter" || command == "color";
  beginEditBatch(live);
  m_editBatchPayloadOnly = live;
  QSet<QString> processed;
  QString firstInstance;
  QString firstClip;
  for (const QString &id : ids) {
    if (processed.contains(id))
      continue;
    processed.insert(id);
    if (command == "speed") {
      const QStringList linked = expandedLinkedClipIds({id});
      // The single operation retimes a linked pair atomically. Do it once.
      for (const QString &peer : linked)
        processed.insert(peer);
      setClipSpeed(id, args.value("speed").toDouble(),
                   args.value("maintainPitch", true).toBool(),
                   args.value("interpolation", "sampling").toString(),
                   args.value("ripple").toBool());
    } else if (command == "split") {
      splitClip(id, args.value("positionMs").toLongLong());
    } else if (command == "setting") {
      setClipEffectSetting(id, key, args.value("value"));
    } else if (command == "reset") {
      resetClipEffectSettings(id);
    } else if (command == "field") {
      setClipFieldOptions(id, args.value("mode").toString(),
                          args.value("reverse").toBool());
    } else if (command == "color") {
      setClipColorSetting(id, key, args.value("value"));
    } else if (command == "colorPreset") {
      applyClipColorPreset(id, args.value("settings").toMap());
    } else if (command == "add" || command == "preset") {
      const QVariantList steps = command == "preset"
          ? args.value("steps").toList()
          : QVariantList{QVariantMap{{"effectId", args.value("effectId")}}};
      for (int stepIndex = 0; stepIndex < steps.size(); ++stepIndex) {
        const QVariantMap step = steps.at(stepIndex).toMap();
        const QVariantMap definition = EffectRegistry::definition(step.value("effectId").toString());
        int index = clipIndex(id);
        QVariantMap clip = m_clips.at(index).toMap();
        // Route audio effects to extracted audio, as the single-clip path does.
        if (definition.value("mediaType") == "audio" && clip.value("separateAudio").toBool()) {
          for (const int peer : audioPeerIndexes(index)) {
            if (m_clips.at(peer).toMap().value("kind") == "audio") {
              index = peer;
              clip = m_clips.at(index).toMap();
              break;
            }
          }
        }
        if (trackLocked(clip.value("track").toString()) ||
            !EffectRegistry::supportsClip(definition, clip.value("kind").toString(),
                mediaById(clip.value("mediaId").toString()).value("channels").toInt() > 0))
          continue;
        const QString targetId = clip.value("id").toString();
        // Selecting both the video and its extracted sound must not add twice.
        const QString token = targetId + QChar(0x1f) + QString::number(stepIndex);
        if (processed.contains(token))
          continue;
        processed.insert(token);
        QVariantList stack = clip.value("effectStack").toList();
        const QString newId = newClipOpId("effect");
        if (!EffectStack::append(&stack, definition, newId))
          continue;
        const QVariantMap parameters = step.value("parameters").toMap();
        for (auto it = parameters.cbegin(); it != parameters.cend(); ++it)
          EffectStack::setParameter(&stack, newId, definition, it.key(), it.value());
        clip["effectStack"] = stack;
        m_clips[index] = clip;
        markDirty();
        notifyClipEdit();
        if (firstInstance.isEmpty() || id == primary) {
          firstInstance = newId;
          firstClip = targetId;
        }
      }
    } else if (!definitionId.isEmpty()) {
      QString targetInstance;
      int n = 0;
      for (const QVariant &v : clipById(id).value("effectStack").toList()) {
        const QVariantMap instance = v.toMap();
        if (instance.value("definitionId").toString() == definitionId && n++ == occurrence) {
          targetInstance = instance.value("id").toString();
          break;
        }
      }
      if (targetInstance.isEmpty())
        continue;
      if (command == "parameter")
        setClipEffectParameter(id, targetInstance, key, args.value("value"));
      else if (command == "enableEffect")
        setClipEffectEnabled(id, targetInstance, args.value("enabled").toBool());
      else if (command == "removeEffect")
        removeClipEffect(id, targetInstance);
      else if (command == "moveEffect")
        moveClipEffect(id, targetInstance, args.value("offset").toInt());
      else if (command == "mask")
        setCustomBlurMask(id, targetInstance, args.value("x").toDouble(),
                          args.value("y").toDouble(), args.value("width").toDouble(),
                          args.value("height").toDouble());
      else if (command == "resetInstance")
        resetClipEffectInstance(id, targetInstance);
    }
  }
  const bool changed = before != m_clips;
  if (!changed && (!live || snapshotNeeded) && !m_undo.isEmpty() &&
      m_undo.constLast().value("clips").toList() == before) {
    m_undo.removeLast();
    if (live && m_liveParameterEditActive)
      m_liveParameterSnapshotTaken = false;
    emit historyChanged();
  }
  endEditBatch();
  if (vocalBatch) {
    for (const QString &id : ids) {
      if (!m_demucsSelectionQueue.contains(id))
        m_demucsSelectionQueue.append(id);
    }
    pumpDemucsSelectionQueue();
  }
  if (!firstInstance.isEmpty()) {
    setSelectedClipId(firstClip);
    emit effectControlsRequested();
    if (args.value("effectId") == "custom_blur")
      beginCustomBlurMaskEdit(firstClip, firstInstance);
  }
  return {{"changed", changed}, {"instanceId", firstInstance}, {"clipId", firstClip}};
}

bool Backend::setClipSpeed(const QString &clipId, double speedPercent,
                           bool maintainPitch, const QString &timeInterpolation,
                           bool rippleTrailing) {
  if (clipIndex(clipId) < 0)
    return false;
  // A linked A/V pair is retimed together so the sound never drifts from the
  // picture; detached audio is left alone (expandedLinkedClipIds handles that).
  const QStringList affected = expandedLinkedClipIds(QStringList{clipId});
  QVector<int> indexes;
  for (const QString &id : affected) {
    const int i = clipIndex(id);
    if (i < 0)
      continue;
    if (trackLocked(m_clips.at(i).toMap().value("track").toString()))
      return false;
    indexes.append(i);
  }
  if (indexes.isEmpty())
    return false;

  rememberState();
  bool changed = false;
  for (const int i : indexes) {
    QVariantMap clip = m_clips.at(i).toMap();
    QVariantMap effects = mergedEffects(clip);
    const double oldSpeed = qMax(0.01, effects.value("speed").toDouble());
    ClipEffects::setValue(&effects, "speed", speedPercent);
    ClipEffects::setValue(&effects, "maintainPitch", maintainPitch);
    ClipEffects::setValue(&effects, "timeInterpolation", timeInterpolation);
    const double effSpeed = effects.value("speed").toDouble();
    const qint64 oldDur = clip.value("durationMs").toLongLong();
    // Retime around the span of source this clip actually uses, which is its
    // current timeline length scaled by its current speed - NOT
    // `sourceDurationMs`, which is the whole media file and is never narrowed by
    // a trim or a split. Using that made every retime of a trimmed clip throw
    // the trim away and re-expand to the full source: a ten-second cut of a
    // 26-hour recording came back as a 26-hour clip.
    const qint64 usedSource = qRound64(double(oldDur) * oldSpeed / 100.0);
    qint64 newDur = effSpeed > 0.0
                        ? qRound64(double(usedSource) * 100.0 / effSpeed)
                        : oldDur;
    // A very slow speed on a long source can still outrun the timeline; clamp
    // to the same 24-hour ceiling stills use rather than emit a clip no edit
    // can navigate.
    newDur = qBound<qint64>(Q_INT64_C(1), newDur, Q_INT64_C(86400000));
    if (newDur == oldDur && effects == clip.value("effects").toMap())
      continue;
    clip["effects"] = effects;
    clip["durationMs"] = newDur;
    m_clips[i] = clip;
    changed = true;

    if (rippleTrailing && newDur != oldDur) {
      const qint64 delta = newDur - oldDur;
      const QString track = clip.value("track").toString();
      const qint64 oldEnd = clip.value("startMs").toLongLong() + oldDur;
      for (int j = 0; j < m_clips.size(); ++j) {
        QVariantMap other = m_clips.at(j).toMap();
        if (other.value("track").toString() != track)
          continue;
        if (affected.contains(other.value("id").toString()))
          continue;
        if (other.value("startMs").toLongLong() >= oldEnd) {
          other["startMs"] =
              qMax<qint64>(0, other.value("startMs").toLongLong() + delta);
          m_clips[j] = other;
        }
      }
    }
  }
  if (!changed) {
    if (m_editBatchDepth == 0) {
      m_undo.removeLast();
      emit historyChanged();
    }
    return false;
  }
  markDirty();
  notifyClipEdit();
  return true;
}
// PLACEHOLDER_APPEND

bool Backend::setClipEnabled(const QString &clipId, bool on) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  if (clip.value("enabled", true).toBool() == on)
    return true;
  rememberState();
  clip["enabled"] = on;
  m_clips[index] = clip;
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}

bool Backend::toggleClipsEnabled(const QStringList &clipIds) {
  const QStringList eligible = editableClipIds(clipIds);
  QVector<int> indexes;
  bool anyDisabled = false;
  for (const QString &id : eligible) {
    const int i = clipIndex(id);
    if (i < 0 || indexes.contains(i))
      continue;
    indexes.append(i);
    if (!m_clips.at(i).toMap().value("enabled", true).toBool())
      anyDisabled = true;
  }
  if (indexes.isEmpty())
    return false;
  // If anything is off, the group turns fully on; otherwise it all turns off.
  const bool target = anyDisabled;
  rememberState();
  for (const int i : indexes) {
    QVariantMap clip = m_clips.at(i).toMap();
    clip["enabled"] = target;
    m_clips[i] = clip;
  }
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}

bool Backend::setClipFieldOptions(const QString &clipId,
                                  const QString &fieldMode,
                                  bool reverseFieldDominance) {
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  QVariantMap clip = m_clips.at(index).toMap();
  QVariantMap effects = mergedEffects(clip);
  const bool okMode = ClipEffects::setValue(&effects, "fieldMode", fieldMode);
  ClipEffects::setValue(&effects, "reverseFieldDominance", reverseFieldDominance);
  if (!okMode && !effects.contains("fieldMode"))
    return false;
  rememberState();
  clip["effects"] = effects;
  m_clips[index] = clip;
  markDirty();
  notifyClipEdit();
  return true;
}

// Group and Link share a mechanism (a shared id on each member) but not a field:
// groupId is a soft selection group, linkGroupId is the hard A/V link the rest
// of the editor already honours for split/move/delete.
static bool assignSharedId(Backend *self, QVariantList &clips,
                           const QStringList &clipIds, const QString &field,
                           const QString &value) {
  Q_UNUSED(self);
  const QSet<QString> wanted(clipIds.begin(), clipIds.end());
  bool changed = false;
  for (int i = 0; i < clips.size(); ++i) {
    QVariantMap clip = clips.at(i).toMap();
    if (!wanted.contains(clip.value("id").toString()))
      continue;
    if (clip.value(field).toString() == value)
      continue;
    if (value.isEmpty())
      clip.remove(field);
    else
      clip[field] = value;
    clips[i] = clip;
    changed = true;
  }
  return changed;
}

bool Backend::groupClips(const QStringList &clipIds) {
  const QStringList eligible = editableClipIds(clipIds);
  if (eligible.size() < 2)
    return false;
  rememberState();
  QVariantList updated = m_clips;
  if (!assignSharedId(this, updated, eligible, QStringLiteral("groupId"),
                      newClipOpId("group"))) {
    m_undo.removeLast();
    emit historyChanged();
    return false;
  }
  m_clips = updated;
  markDirty();
  emit clipsChanged();
  return true;
}

bool Backend::ungroupClips(const QStringList &clipIds) {
  // Ungroup any clip that shares a group with one of the given clips, so the
  // whole group dissolves however few of its members were selected.
  QSet<QString> groups;
  for (const QVariant &value : m_clips) {
    const QVariantMap clip = value.toMap();
    if (clipIds.contains(clip.value("id").toString())) {
      const QString g = clip.value("groupId").toString();
      if (!g.isEmpty())
        groups.insert(g);
    }
  }
  if (groups.isEmpty())
    return false;
  rememberState();
  QVariantList updated = m_clips;
  bool changed = false;
  for (int i = 0; i < updated.size(); ++i) {
    QVariantMap clip = updated.at(i).toMap();
    if (groups.contains(clip.value("groupId").toString())) {
      clip.remove("groupId");
      updated[i] = clip;
      changed = true;
    }
  }
  if (!changed) {
    m_undo.removeLast();
    emit historyChanged();
    return false;
  }
  m_clips = updated;
  markDirty();
  emit clipsChanged();
  return true;
}

bool Backend::linkClips(const QStringList &clipIds) {
  const QStringList eligible = editableClipIds(clipIds);
  if (eligible.size() < 2)
    return false;
  rememberState();
  QVariantList updated = m_clips;
  if (!assignSharedId(this, updated, eligible, QStringLiteral("linkGroupId"),
                      newClipOpId("link"))) {
    m_undo.removeLast();
    emit historyChanged();
    return false;
  }
  m_clips = updated;
  markDirty();
  emit clipsChanged();
  return true;
}

bool Backend::unlinkClips(const QStringList &clipIds) {
  QSet<QString> groups;
  for (const QVariant &value : m_clips) {
    const QVariantMap clip = value.toMap();
    if (clipIds.contains(clip.value("id").toString())) {
      const QString g = clip.value("linkGroupId").toString();
      if (!g.isEmpty())
        groups.insert(g);
    }
  }
  if (groups.isEmpty())
    return false;
  rememberState();
  QVariantList updated = m_clips;
  bool changed = false;
  for (int i = 0; i < updated.size(); ++i) {
    QVariantMap clip = updated.at(i).toMap();
    if (groups.contains(clip.value("linkGroupId").toString())) {
      clip.remove("linkGroupId");
      updated[i] = clip;
      changed = true;
    }
  }
  if (!changed) {
    m_undo.removeLast();
    emit historyChanged();
    return false;
  }
  m_clips = updated;
  markDirty();
  emit clipsChanged();
  return true;
}
// PLACEHOLDER_APPEND2

bool Backend::copyClips(const QStringList &clipIds) {
  QVariantList buffer;
  qint64 earliest = std::numeric_limits<qint64>::max();
  for (const QVariant &value : m_clips) {
    const QVariantMap clip = value.toMap();
    if (clipIds.contains(clip.value("id").toString()))
      earliest = qMin(earliest, clip.value("startMs").toLongLong());
  }
  if (earliest == std::numeric_limits<qint64>::max())
    return false;
  // Store each clip with startMs rebased to an offset from the earliest, so a
  // later paste can drop the whole set onto any anchor time.
  for (const QVariant &value : m_clips) {
    QVariantMap clip = value.toMap();
    if (!clipIds.contains(clip.value("id").toString()))
      continue;
    clip["startMs"] = clip.value("startMs").toLongLong() - earliest;
    buffer.append(clip);
  }
  if (buffer.isEmpty())
    return false;
  m_clipClipboard = buffer;
  return true;
}

bool Backend::cutClips(const QStringList &clipIds) {
  if (!copyClips(clipIds))
    return false;
  return removeClips(clipIds);
}

QStringList Backend::pasteClips(const QString &track, qint64 atMs) {
  if (m_clipClipboard.isEmpty())
    return {};
  const qint64 anchor = atMs >= 0 ? atMs : playheadMs();
  const bool single = m_clipClipboard.size() == 1;

  // Fresh group/link ids so the pasted set stays internally grouped/linked but
  // never joins the group it was copied from.
  QHash<QString, QString> groupRemap;
  QHash<QString, QString> linkRemap;
  auto remap = [](QHash<QString, QString> &table, const QString &old,
                  const char *prefix) {
    if (old.isEmpty())
      return QString();
    auto it = table.constFind(old);
    if (it != table.constEnd())
      return it.value();
    const QString fresh = newClipOpId(prefix);
    table.insert(old, fresh);
    return fresh;
  };

  rememberState();
  QStringList newIds;
  int requiredVideoTracks = m_videoTrackCount;
  int requiredAudioTracks = m_audioTrackCount;
  for (const QVariant &value : m_clipClipboard) {
    QVariantMap clip = value.toMap();
    clip["id"] = newClipOpId("clip");
    clip["startMs"] =
        qMax<qint64>(0, anchor + clip.value("startMs").toLongLong());
    if (single && !track.isEmpty())
      clip["track"] = track.toUpper();
    const QString g =
        remap(groupRemap, clip.value("groupId").toString(), "group");
    if (g.isEmpty())
      clip.remove("groupId");
    else
      clip["groupId"] = g;
    const QString l =
        remap(linkRemap, clip.value("linkGroupId").toString(), "link");
    if (l.isEmpty())
      clip.remove("linkGroupId");
    else
      clip["linkGroupId"] = l;
    if (!clip.contains("enabled"))
      clip["enabled"] = true;
    const QString dest = clip.value("track").toString();
    if (dest.startsWith('V'))
      requiredVideoTracks = qMax(requiredVideoTracks, dest.mid(1).toInt());
    else if (dest.startsWith('A'))
      requiredAudioTracks = qMax(requiredAudioTracks, dest.mid(1).toInt());
    newIds.append(clip.value("id").toString());
    m_clips.append(clip);
  }
  m_videoTrackCount = qMax(m_videoTrackCount, requiredVideoTracks);
  m_audioTrackCount = qMax(m_audioTrackCount, requiredAudioTracks);
  markDirty();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  return newIds;
}

bool Backend::pasteClipAttributes(const QStringList &targetClipIds) {
  const QStringList eligible = editableClipIds(targetClipIds);
  if (m_clipClipboard.isEmpty() || targetClipIds.isEmpty())
    return false;
  const QVariantMap source = m_clipClipboard.first().toMap();
  const QVariantMap sourceEffects = mergedEffects(source);
  const QVariantMap sourceLumetri = source.value("lumetri").toMap();
  const double speed = sourceEffects.value("speed").toDouble();

  QVector<int> indexes;
  for (const QString &id : eligible) {
    const int i = clipIndex(id);
    if (i >= 0 && !indexes.contains(i))
      indexes.append(i);
  }
  if (indexes.isEmpty())
    return false;
  rememberState();
  for (const int i : indexes) {
    QVariantMap clip = m_clips.at(i).toMap();
    // Don't paste a video transform onto a sound clip or a subtitle, and don't
    // overwrite an audio clip's own level with the source's. A marquee
    // selection routinely picks up subtitle and effect bars, and the QML guard
    // only vets the clip that was right-clicked.
    const QString kind = clip.value("kind").toString();
    if (kind == QStringLiteral("subtitle") || kind == QStringLiteral("effect"))
      continue;
    const double targetOldSpeed =
        qMax(0.01, mergedEffects(clip).value("speed").toDouble());
    clip["effects"] = sourceEffects;
    if (sourceLumetri.isEmpty())
      clip.remove("lumetri");
    else
      clip["lumetri"] = sourceLumetri;
    // Carry the applied effects too. Without this, "apply this look to the rest
    // of the selection" - the only multi-clip route the app has - silently
    // dropped every effect added from the browser and pasted only the built-in
    // sliders.
    const QVariantList sourceStack = source.value("effectStack").toList();
    if (sourceStack.isEmpty()) {
      clip.remove("effectStack");
    } else {
      // Fresh instance ids: two clips sharing one instance id would share a
      // single keyframe channel.
      QVariantList stack;
      stack.reserve(sourceStack.size());
      for (const QVariant &entry : sourceStack) {
        QVariantMap instance = entry.toMap();
        instance["id"] = newClipOpId("effect");
        stack.append(instance);
      }
      clip["effectStack"] = stack;
    }
    // The pasted speed changes how much of the source plays. Scale the target's
    // own used span by the speed change instead of recomputing from
    // `sourceDurationMs`, which is the whole media file and ignores every trim:
    // that reset each target to its full source length, turning a short cut of a
    // multi-hour recording back into a multi-hour clip.
    if (speed > 0.0 && !qFuzzyCompare(speed, targetOldSpeed)) {
      const qint64 oldDur = clip.value("durationMs").toLongLong();
      const qint64 usedSource =
          qRound64(double(oldDur) * targetOldSpeed / 100.0);
      clip["durationMs"] =
          qBound<qint64>(Q_INT64_C(1),
                         qRound64(double(usedSource) * 100.0 / speed),
                         Q_INT64_C(86400000));
    }
    m_clips[i] = clip;
  }
  markDirty();
  emit clipsChanged();
  emit timelineChanged();
  return true;
}

bool Backend::setClipLabel(const QStringList &clipIds,
                           const QString &colorToken) {
  const QStringList eligible = editableClipIds(clipIds);
  QVector<int> indexes;
  for (const QString &id : eligible) {
    const int i = clipIndex(id);
    if (i >= 0 && !indexes.contains(i))
      indexes.append(i);
  }
  if (indexes.isEmpty())
    return false;
  rememberState();
  for (const int i : indexes) {
    QVariantMap clip = m_clips.at(i).toMap();
    if (colorToken.isEmpty())
      clip.remove("label");
    else
      clip["label"] = colorToken;
    m_clips[i] = clip;
  }
  markDirty();
  emit clipsChanged();
  return true;
}
// PLACEHOLDER_APPEND3

bool Backend::detectSceneEdits(const QString &clipId, double threshold,
                               bool applyCuts, bool createMarkers,
                               qint64 minShotMs) {
  if (m_sceneProcess.state() != QProcess::NotRunning)
    return false;
  const int index = clipIndex(clipId);
  if (index < 0)
    return false;
  const QVariantMap clip = m_clips.at(index).toMap();
  const QVariantMap media = mediaById(clip.value("mediaId").toString());
  const QString path = media.value("path").toString();
  if (path.isEmpty()) {
    setError(QStringLiteral("The selected clip has no source media to scan."));
    return false;
  }
  const QString ffmpegExe = FfmpegRuntime::executable();
  if (ffmpegExe.isEmpty() || !QFileInfo::exists(ffmpegExe)) {
    setError(QStringLiteral("The bundled ffmpeg could not be found."));
    return false;
  }

  // Scan only the slice of source the clip actually uses. That slice is the
  // timeline length scaled back up by the clip's speed, so a retimed clip is
  // still scanned over the right source range.
  const QVariantMap effects = mergedEffects(clip);
  const double factor = qMax(0.01, effects.value("speed").toDouble() / 100.0);
  const double startSec = clip.value("sourceInMs").toLongLong() / 1000.0;
  const double durSec =
      (clip.value("durationMs").toLongLong() * factor) / 1000.0;
  const double thresh = qBound(0.05, threshold, 0.95);

  if (!m_sceneWired) {
    m_sceneWired = true;
    connect(&m_sceneProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
              handleSceneDetectFinished(code, status);
            });
    // showinfo writes one line per detected cut to stderr and -progress writes
    // position lines to stdout. Both are drained as they arrive: reading stderr
    // only at the end meant a long pass showed nothing at all, and a large
    // enough backlog can stall the child on a full pipe.
    connect(&m_sceneProcess, &QProcess::readyReadStandardError, this, [this]() {
      const QByteArray chunk = m_sceneProcess.readAllStandardError();
      m_sceneStderrTail.append(chunk);
      m_sceneCutsSoFar += int(chunk.count("pts_time:"));
      updateSceneDetectStatus();
    });
    connect(&m_sceneProcess, &QProcess::readyReadStandardOutput, this, [this]() {
      const QByteArray chunk = m_sceneProcess.readAllStandardOutput();
      // out_time=HH:MM:SS.mmm is the one unambiguous field here; out_time_ms is
      // microseconds in some ffmpeg builds despite its name.
      static const QRegularExpression rx(
          QStringLiteral("out_time=(\\d+):(\\d+):(\\d+)\\.(\\d+)"));
      auto it = rx.globalMatch(QString::fromUtf8(chunk));
      while (it.hasNext()) {
        const auto match = it.next();
        const qint64 ms = match.captured(1).toLongLong() * 3600000 +
                          match.captured(2).toLongLong() * 60000 +
                          match.captured(3).toLongLong() * 1000 +
                          match.captured(4).leftJustified(3, '0').left(3).toLongLong();
        if (m_sceneScanSpanMs > 0)
          m_sceneProgress = qBound(0.0, double(ms) / double(m_sceneScanSpanMs), 1.0);
      }
      updateSceneDetectStatus();
    });
  }
#ifdef Q_OS_WIN
  m_sceneProcess.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
      });
#endif

  m_sceneClipId = clipId;
  m_sceneClipSnapshot = clip;
  m_sceneClipStartMs = clip.value("startMs").toLongLong();
  m_sceneClipDurationMs = clip.value("durationMs").toLongLong();
  m_sceneApplyCuts = applyCuts;
  m_sceneCreateMarkers = createMarkers;
  m_sceneMinShotMs = qMax<qint64>(0, minShotMs);
  m_sceneCancelRequested = false;
  m_sceneProgress = 0.0;
  m_sceneCutsSoFar = 0;
  m_sceneStderrTail.clear();
  m_sceneScanSpanMs = qRound64(qMax(0.1, durSec) * 1000.0);

  const QStringList args{
      QStringLiteral("-hide_banner"),
      QStringLiteral("-nostats"),
      QStringLiteral("-progress"),
      QStringLiteral("pipe:1"),
      QStringLiteral("-ss"),
      QString::number(startSec, 'f', 3),
      QStringLiteral("-t"),
      QString::number(qMax(0.1, durSec), 'f', 3),
      QStringLiteral("-i"),
      path,
      QStringLiteral("-an"),
      QStringLiteral("-sn"),
      QStringLiteral("-filter:v"),
      // Score the cuts on a 360p copy. The scene metric is per-pixel, so this
      // cuts the filter's work by around an order of magnitude on HD source
      // while leaving timestamps - and therefore cut positions - untouched.
      QStringLiteral("scale=-2:360,select='gt(scene,%1)',showinfo")
          .arg(thresh, 0, 'f', 3),
      QStringLiteral("-f"),
      QStringLiteral("null"),
      QStringLiteral("-")};

  m_sceneStatus = QStringLiteral("Detecting scene cuts...");
  m_sceneProcess.start(ffmpegExe, args);
  if (!m_sceneProcess.waitForStarted(2000)) {
    m_sceneStatus = QStringLiteral("Scene detection could not be started");
    emit sceneDetectChanged();
    return false;
  }
  emit sceneDetectChanged();
  return true;
}

void Backend::updateSceneDetectStatus() {
  m_sceneStatus = m_sceneCutsSoFar > 0
                      ? QStringLiteral("Detecting scene cuts... %1 found")
                            .arg(m_sceneCutsSoFar)
                      : QStringLiteral("Detecting scene cuts...");
  emit sceneDetectChanged();
}

void Backend::cancelSceneDetect() {
  if (!sceneDetectInProgress())
    return;
  m_sceneCancelRequested = true;
  m_sceneQueue.clear();
  if (m_sceneProcess.state() != QProcess::NotRunning)
    m_sceneProcess.kill();
  else
    finishSceneJob(false, 0, QStringLiteral("Cancelled"));
}

void Backend::handleSceneDetectFinished(int exitCode,
                                        QProcess::ExitStatus status) {
  m_sceneProgress = 0.0;
  if (m_sceneCancelRequested) {
    m_sceneCancelRequested = false;
    m_sceneStatus = QStringLiteral("Scene detection cancelled");
    emit sceneDetectChanged();
    finishSceneJob(false, 0, QStringLiteral("Cancelled"));
    return;
  }
  // Whatever the readyRead handler has not drained yet, plus everything it has.
  m_sceneStderrTail.append(m_sceneProcess.readAllStandardError());
  const QByteArray err = m_sceneStderrTail;
  if (status != QProcess::NormalExit || exitCode != 0) {
    m_sceneStatus = QStringLiteral("Scene detection failed");
    emit sceneDetectChanged();
    finishSceneJob(false, 0,
                             QStringLiteral("The scene-detection pass failed."));
    return;
  }

  // showinfo prints one line per selected (cut) frame with its source timestamp
  // as pts_time. With input seeking these are offsets from the scanned slice.
  QList<double> cutSeconds;
  static const QRegularExpression rx(QStringLiteral("pts_time:([0-9]+\\.?[0-9]*)"));
  auto it = rx.globalMatch(QString::fromUtf8(err));
  while (it.hasNext())
    cutSeconds.append(it.next().captured(1).toDouble());

  const int index = clipIndex(m_sceneClipId);
  if (index < 0) {
    m_sceneStatus = QStringLiteral("Scene detection: clip no longer exists");
    emit sceneDetectChanged();
    finishSceneJob(false, 0, QStringLiteral("The clip is gone."));
    return;
  }
  const QVariantMap clip = m_clips.at(index).toMap();
  for (const QString &key : {QString("mediaId"), QString("sourceInMs"),
                             QString("startMs"), QString("durationMs")}) {
    if (clip.value(key) != m_sceneClipSnapshot.value(key)) {
      finishSceneJob(false, 0, tr("The clip changed while scene detection was running."));
      return;
    }
  }
  if (mergedEffects(clip).value("speed") != mergedEffects(m_sceneClipSnapshot).value("speed")) {
    finishSceneJob(false, 0, tr("The clip speed changed while scene detection was running."));
    return;
  }
  const qint64 startMs = clip.value("startMs").toLongLong();
  const qint64 durationMs = clip.value("durationMs").toLongLong();
  const QVariantMap effects = mergedEffects(clip);
  const double factor = qMax(0.01, effects.value("speed").toDouble() / 100.0);

  // Source offset -> timeline offset (divide by speed), then to an absolute
  // timeline position inside this clip. Dedupe and keep only interior cuts.
  QList<qint64> positions;
  QSet<qint64> seen;
  for (const double sec : cutSeconds) {
    const qint64 pos = startMs + qRound64((sec * 1000.0) / factor);
    if (pos > startMs && pos < startMs + durationMs && !seen.contains(pos)) {
      seen.insert(pos);
      positions.append(pos);
    }
  }
  std::sort(positions.begin(), positions.end());
  // Drop a cut that arrives less than one minimum shot after the one kept
  // before it. Without this a single flashy sequence turns one clip into
  // thousands of slivers, each costing a linear index scan to create.
  if (m_sceneMinShotMs > 0 && !positions.isEmpty()) {
    QList<qint64> spaced;
    qint64 last = startMs;
    for (const qint64 pos : positions) {
      if (pos - last < m_sceneMinShotMs)
        continue;
      spaced.append(pos);
      last = pos;
    }
    positions = spaced;
  }
  const int detected = positions.size();

  int acted = 0;
  // One undo step and one projection for the whole detection, not one per cut:
  // a long shot can produce dozens of cut points, and each splitClip/addMarker
  // otherwise took its own full-project snapshot and fired its own clipsChanged.
  const bool willMutate =
      !positions.isEmpty() && (m_sceneCreateMarkers || m_sceneApplyCuts);
  if (willMutate)
    beginEditBatch();
  if (m_sceneCreateMarkers) {
    for (const qint64 pos : positions)
      addMarker(pos, QStringLiteral("Scene"), QStringLiteral("#f4cf58"));
  }
  if (m_sceneApplyCuts) {
    // Descending so an earlier split never shifts a position still to be cut;
    // the left piece keeps the clip id, so every split targets it.
    QList<qint64> descending = positions;
    std::sort(descending.begin(), descending.end(), std::greater<qint64>());
    for (const qint64 pos : descending) {
      if (splitClip(m_sceneClipId, pos))
        ++acted;
    }
  } else {
    acted = detected;
  }
  if (willMutate)
    endEditBatch();

  m_sceneStatus = QStringLiteral("Scene detection: %1 cuts").arg(detected);
  emit sceneDetectChanged();
  finishSceneJob(true, acted, QString());
}




// Keep the process and its result buffers owned by one clip at a time.
bool Backend::detectClipsSceneEdits(const QStringList &ids, double threshold,
                                    bool applyCuts, bool createMarkers,
                                    qint64 minShotMs) {
  if (sceneDetectInProgress())
    return false;
  m_sceneQueue = editableClipIds(ids, "movingVideo");
  if (m_sceneQueue.isEmpty())
    return false;
  m_sceneBatchActive = true;
  m_sceneBatchThreshold = threshold;
  m_sceneApplyCuts = applyCuts;
  m_sceneCreateMarkers = createMarkers;
  m_sceneMinShotMs = minShotMs;
  m_sceneBatchActed = 0;
  m_sceneBatchError.clear();
  pumpSceneQueue();
  return true;
}

void Backend::pumpSceneQueue() {
  while (!m_sceneQueue.isEmpty()) {
    const QString id = m_sceneQueue.takeFirst();
    if (editableClipIds({id}, "movingVideo").isEmpty())
      continue;
    if (detectSceneEdits(id, m_sceneBatchThreshold, m_sceneApplyCuts,
                          m_sceneCreateMarkers, m_sceneMinShotMs))
      return;
    m_sceneBatchError = tr("A selected clip could not be scanned.");
  }
  m_sceneBatchActive = false;
  m_sceneStatus = m_sceneBatchError.isEmpty()
      ? tr("Scene detection: %1 cuts").arg(m_sceneBatchActed) : m_sceneBatchError;
  emit sceneDetectChanged();
  emit sceneDetectFinished(m_sceneBatchError.isEmpty(), m_sceneBatchActed,
                           m_sceneBatchError);
}

void Backend::finishSceneJob(bool ok, int acted, const QString &error) {
  if (!m_sceneBatchActive) {
    emit sceneDetectFinished(ok, acted, error);
    return;
  }
  m_sceneBatchActed += acted;
  if (!ok)
    m_sceneBatchError = error;
  // Yield between clips so cancel/close is delivered before the next scan.
  QTimer::singleShot(0, this, &Backend::pumpSceneQueue);
}
