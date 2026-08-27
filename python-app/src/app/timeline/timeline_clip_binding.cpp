#include "app/timeline/timeline_clip_binding.h"

#include <QHash>
#include <QSet>
#include <QVariantMap>

void TimelineClipBinding::collapseLegacyEmbeddedAudio(
    QVariantList *clips, const QVariantList &media) {
  if (!clips)
    return;

  QHash<QString, int> channelsByMedia;
  for (const auto &value : media) {
    const QVariantMap item = value.toMap();
    channelsByMedia[item.value("id").toString()] =
        item.value("channels").toInt();
  }

  QHash<QString, int> videoByGroup;
  QHash<QString, QList<int>> audioByGroup;
  for (int i = 0; i < clips->size(); ++i) {
    const QVariantMap clip = clips->at(i).toMap();
    const QString group = clip.value("linkGroupId").toString();
    if (group.isEmpty())
      continue;
    if (clip.value("kind") == QStringLiteral("video") &&
        clip.value("separateAudio").toBool())
      videoByGroup[group] = i;
    else if (clip.value("kind") == QStringLiteral("audio"))
      audioByGroup[group].append(i);
  }

  QSet<int> removeIndexes;
  for (auto it = videoByGroup.cbegin(); it != videoByGroup.cend(); ++it) {
    if (!audioByGroup.contains(it.key()))
      continue;
    QVariantMap video = clips->at(it.value()).toMap();
    QVariantMap mergedEffects = video.value("effects").toMap();
    QVariantList mergedStack = video.value("effectStack").toList();
    for (const int audioIndex : audioByGroup.value(it.key())) {
      const QVariantMap audio = clips->at(audioIndex).toMap();
      const QVariantMap audioEffects = audio.value("effects").toMap();
      for (auto setting = audioEffects.cbegin(); setting != audioEffects.cend();
           ++setting)
        mergedEffects[setting.key()] = setting.value();
      mergedStack.append(audio.value("effectStack").toList());
      removeIndexes.insert(audioIndex);
    }

    if (!mergedEffects.isEmpty())
      video["effects"] = mergedEffects;
    if (!mergedStack.isEmpty())
      video["effectStack"] = mergedStack;
    video["embeddedAudio"] =
        channelsByMedia.value(video.value("mediaId").toString()) > 0;
    video.remove("separateAudio");
    video.remove("linkGroupId");
    video.remove("linkedRole");
    (*clips)[it.value()] = video;
  }

  for (int i = clips->size() - 1; i >= 0; --i) {
    if (removeIndexes.contains(i))
      clips->removeAt(i);
  }
}
