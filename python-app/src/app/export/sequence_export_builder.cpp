#include "app/export/sequence_export_builder.h"
#include "app/effects/clip_effects_pipeline.h"
#include "app/effects/audio_effect_pipeline.h"
#include "app/lumetri/custom_blur_pipeline.h"
#include "app/lumetri/lumetri_pipeline.h"
#include "app/subtitles/subtitle_io.h"
#include "app/effects/video_effect_pipeline.h"
#include "app/subtitles/khmer_support.h"

#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>
#include <QGuiApplication>
#include <algorithm>

namespace {
QString seconds(qint64 milliseconds) {
  return QString::number(milliseconds / 1000.0, 'f', 3);
}
QString subtitleSeconds(double value) {
  return QString::number(qMax(0.0, value), 'f', 3);
}
int trackNumber(const QString &track) { return track.mid(1).toInt(); }

QString escapedFilterPath(QString path) {
  path.replace('\\', '/');
  path.replace(":", "\\:");
  path.replace("'", "\\'");
  return path;
}

} // namespace

QStringList SequenceExportBuilder::build(
    const QVariantList &media, const QVariantList &clips,
    const QStringList &mutedTracks, const QVariantList &trackStates,
    qint64 durationMs, const QVariantMap &colorSettings,
    const QVariantMap &settings, const QString &outputPath, QString *error) {
  QHash<QString, QVariantMap> mediaById;
  for (const auto &value : media) {
    const auto item = value.toMap();
    mediaById.insert(item.value("id").toString(), item);
  }

  QHash<QString, QVariantMap> stateByTrack;
  bool videoSolo = false;
  bool audioSolo = false;
  for (const auto &value : trackStates) {
    const auto state = value.toMap();
    const QString track = state.value("id").toString();
    stateByTrack.insert(track, state);
    if (state.value("solo").toBool()) {
      videoSolo = videoSolo || track.startsWith('V');
      audioSolo = audioSolo || track.startsWith('A');
    }
  }
  const auto trackEnabled = [&](const QString &track) {
    const QVariantMap state = stateByTrack.value(track);
    if (!state.value("visible", true).toBool())
      return false;
    if (track.startsWith('V') && videoSolo)
      return state.value("solo").toBool();
    if (track.startsWith('A') && audioSolo)
      return state.value("solo").toBool();
    return true;
  };

  QVariantList ordered;
  QVariantList subtitleSegments;
  for (const auto &value : clips)
    if (value.toMap().value("enabled", true).toBool() &&
        trackEnabled(value.toMap().value("track").toString())) {
      const auto clip = value.toMap();
      if (clip.value("kind") == "subtitle") {
        const qint64 startMs = clip.value("startMs").toLongLong();
        const qint64 duration = clip.value("durationMs").toLongLong();
        if (duration > 0 && !clip.value("text").toString().trimmed().isEmpty())
          subtitleSegments.append(
              QVariantMap{{"start", startMs / 1000.0},
                          {"end", (startMs + duration) / 1000.0},
                          {"text", clip.value("text").toString()}});
      } else {
        ordered.append(value);
      }
    }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto &left, const auto &right) {
              const auto a = left.toMap();
              const auto b = right.toMap();
              const bool aVideo = a.value("track").toString().startsWith('V');
              const bool bVideo = b.value("track").toString().startsWith('V');
              if (aVideo != bVideo)
                return aVideo;
              if (trackNumber(a.value("track").toString()) !=
                  trackNumber(b.value("track").toString()))
                return trackNumber(a.value("track").toString()) <
                       trackNumber(b.value("track").toString());
              return a.value("startMs").toLongLong() <
                     b.value("startMs").toLongLong();
            });
  if (ordered.isEmpty()) {
    if (error)
      *error = QStringLiteral("No enabled clips are available for export.");
    return {};
  }

  int width = 1920, height = 1080;
  double frameRate = 30.0;
  for (const auto &value : ordered) {
    const auto clip = value.toMap();
    const auto item = mediaById.value(clip.value("mediaId").toString());
    if (clip.value("kind") != "audio" && item.value("width").toInt() > 0) {
      width = item.value("width").toInt();
      height = item.value("height").toInt();
      frameRate = item.value("frameRate").toDouble() > 0
                      ? item.value("frameRate").toDouble()
                      : 30.0;
      break;
    }
  }

  const int requestedWidth = settings.value("width").toInt();
  const int requestedHeight = settings.value("height").toInt();
  if (requestedWidth > 0 && requestedHeight > 0) {
    width = requestedWidth;
    height = requestedHeight;
  }
  const double requestedFrameRate = settings.value("frameRate").toDouble();
  if (requestedFrameRate > 0)
    frameRate = requestedFrameRate;
  width += width % 2;
  height += height % 2;
  const QString total = seconds(durationMs);

  const bool audioEnabled = settings.value("audioEnabled", true).toBool();
  const int audioBitrateKbps =
      qBound(64, settings.value("audioBitrateKbps", 192).toInt(), 512);
  const int audioSampleRate =
      settings.value("audioSampleRate", 48000).toInt() == 44100 ? 44100 : 48000;
  const int audioChannels =
      qBound(1, settings.value("audioChannels", 2).toInt(), 2);
  const double videoBitrateMbps =
      qBound(0.0, settings.value("videoBitrateMbps", 0.0).toDouble(), 200.0);
  const QString scaleFlags =
      settings.value("maximumRenderQuality", true).toBool()
          ? QStringLiteral(":flags=lanczos")
          : QString();
  const QString quality = settings.value("quality", "high").toString();
  QString encodingSpeed = settings.value("encodingSpeed").toString();
  if (!QStringList{"ultrafast", "veryfast", "fast", "medium", "slow"}.contains(
          encodingSpeed))
    encodingSpeed = quality == "low" ? QStringLiteral("veryfast")
                                     : QStringLiteral("medium");
  QString profile = settings.value("profile", "high").toString().toLower();
  if (!QStringList{"baseline", "main", "high"}.contains(profile))
    profile = QStringLiteral("high");

  QStringList arguments{"-y",
                        "-f",
                        "lavfi",
                        "-t",
                        total,
                        "-i",
                        QString("color=c=black:s=%1x%2:r=%3")
                            .arg(width)
                            .arg(height)
                            .arg(frameRate, 0, 'f', 3),
                        "-f",
                        "lavfi",
                        "-t",
                        total,
                        "-i",
                        "anullsrc=r=48000:cl=stereo"};
  QVector<QVariantMap> exportClips;
  int nextInputIndex = 2;
  for (const auto &value : ordered) {
    const auto clip = value.toMap();
    const auto item = mediaById.value(clip.value("mediaId").toString());
    if (item.isEmpty())
      continue;
    if (clip.value("kind") == "image")
      arguments << "-loop"
                << "1"
                << "-t" << seconds(clip.value("durationMs").toLongLong());
    arguments << "-i" << item.value("path").toString();
    auto entry = clip;
    entry["inputIndex"] = nextInputIndex++;
    const QVariantMap effects = clip.value("effects").toMap();
    const QString demucsPath = effects.value("demucsPath").toString();
    if (effects.value("vocalRemoval").toBool() &&
        QFileInfo::exists(demucsPath)) {
      arguments << "-i" << demucsPath;
      entry["audioInputIndex"] = nextInputIndex++;
    }
    entry["channels"] = item.value("channels");
    exportClips.append(entry);
  }

  QList<KhmerSupport::RasterSubtitle> rasterSubtitles;
  QString fallbackAssPath;
  if (!subtitleSegments.isEmpty()) {
    QString subtitleError;
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
      if (!KhmerSupport::renderRaster(subtitleSegments, settings, width, height,
                                      &rasterSubtitles, &subtitleError)) {
        if (error)
          *error = QStringLiteral("Could not render Khmer subtitles: %1")
                       .arg(subtitleError);
        return {};
      }
    } else {
      fallbackAssPath = QDir(QStandardPaths::writableLocation(
          QStandardPaths::TempLocation))
          .filePath(QStringLiteral("cutpro-headless-subtitles-%1.ass")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
      if (!KhmerSupport::writeAss(fallbackAssPath, subtitleSegments, settings,
                                  width, height, &subtitleError)) {
        if (error)
          *error = QStringLiteral("Could not prepare subtitles for export: %1")
                       .arg(subtitleError);
        return {};
      }
    }
    for (const auto &subtitle : rasterSubtitles)
      arguments << "-loop" << "1" << "-t" << total
                << "-i" << subtitle.path;
  }

  QStringList filters;
  QString videoOutput = "0:v";
  QStringList audioInputs{"[1:a]"};
  int videoIndex = 0, audioIndex = 0;
  for (const auto &clip : exportClips) {
    const int input = clip.value("inputIndex").toInt();
    const qint64 start = clip.value("startMs").toLongLong();
    const qint64 sourceIn = clip.value("sourceInMs").toLongLong();
    const qint64 duration = clip.value("durationMs").toLongLong();
    if (clip.value("kind") != "audio") {
      const QString prepared = QString("vclip%1").arg(videoIndex);
      const QString composed = QString("vout%1").arg(videoIndex);
      const QVariantMap item =
          mediaById.value(clip.value("mediaId").toString());
      const QString colorFilters =
          LumetriPipeline::filterForClip(clip, item, colorSettings);
      const QString colorChain =
          colorFilters.isEmpty() ? QString() : QString(",%1").arg(colorFilters);
      const QVariantMap effects = clip.value("effects").toMap();
      QStringList effectParts;
      const QString intrinsicFilters =
          ClipEffectsPipeline::videoFilters(effects);
      const QString stackFilters =
          VideoEffectPipeline::filters(clip.value("effectStack").toList());
      if (!intrinsicFilters.isEmpty())
        effectParts << intrinsicFilters;
      if (!stackFilters.isEmpty())
        effectParts << stackFilters;
      const QString effectFilters = effectParts.join(',');
      const QString effectChain = effectFilters.isEmpty()
                                      ? QString()
                                      : QString(",%1").arg(effectFilters);
      const QVariantList customBlurMasks =
          CustomBlurPipeline::enabledMasks(clip.value("effectStack").toList());
      const QString initialPrepared =
          customBlurMasks.isEmpty() ? prepared
                                    : QStringLiteral("vpreblur%1").arg(videoIndex);
      filters
          << QString(
                 "[%1:v]trim=start=%2:duration=%3,setpts=PTS-STARTPTS+%4/TB,"
                 "scale=%5:%6:force_original_aspect_ratio=decrease%7%9%10,"
                 "setsar=1[%8]")
                 .arg(input)
                 .arg(seconds(sourceIn))
                 .arg(seconds(duration))
                 .arg(seconds(start))
                 .arg(width)
                 .arg(height)
                 .arg(scaleFlags)
                 .arg(initialPrepared)
                 .arg(colorChain)
                 .arg(effectChain);
      const QString preparedOutput = CustomBlurPipeline::appendFilters(
          &filters, initialPrepared, QStringLiteral("vblur%1").arg(videoIndex),
          customBlurMasks);
      filters << QString("[%1][%2]overlay=x='%3':y='%4':eof_action=pass:"
                         "shortest=0[%5]")
                     .arg(videoOutput, preparedOutput,
                          ClipEffectsPipeline::overlayX(effects),
                          ClipEffectsPipeline::overlayY(effects), composed);
      videoOutput = composed;
      ++videoIndex;
    }
    if (audioEnabled && !clip.value("separateAudio").toBool() &&
        clip.value("channels").toInt() > 0 &&
        !mutedTracks.contains(clip.value("track").toString())) {
      const QString audio = QString("aclip%1").arg(audioIndex++);
      QStringList audioEffectParts;
      const int channels = clip.value("channels").toInt();
      QVariantMap intrinsicEffects = clip.value("effects").toMap();
      // Demucs already produced the accompaniment stem. Do not run the
      // center-cancellation fallback over that separated audio a second time.
      if (clip.contains("audioInputIndex"))
        intrinsicEffects["vocalRemoval"] = false;
      const QString intrinsicAudio = ClipEffectsPipeline::audioFilters(
          intrinsicEffects, channels);
      const QString stackAudio = AudioEffectPipeline::filters(
          clip.value("effectStack").toList(), channels);
      if (!intrinsicAudio.isEmpty())
        audioEffectParts << intrinsicAudio;
      if (!stackAudio.isEmpty())
        audioEffectParts << stackAudio;
      const QString audioFilters = audioEffectParts.join(',');
      const QString audioChain =
          audioFilters.isEmpty() ? QString() : QString(",%1").arg(audioFilters);
        const int audioInput = clip.value("audioInputIndex", input).toInt();
      filters << QString(
               "[%1:a]atrim=start=%2:duration=%3,asetpts=PTS-STARTPTS,"
                     "aresample=48000%6,adelay=%4|%4[%5]")
               .arg(audioInput)
                     .arg(seconds(sourceIn))
                     .arg(seconds(duration))
                     .arg(start)
                     .arg(audio)
                     .arg(audioChain);
      audioInputs << QString("[%1]").arg(audio);
    }
  }
  const int subtitleInputBase = nextInputIndex;
  int subtitleIndex = 0;
  for (const auto &subtitle : rasterSubtitles) {
    const QString imageLabel = QStringLiteral("subimg%1").arg(subtitleIndex);
    const QString outputLabel = QStringLiteral("subout%1").arg(subtitleIndex);
    filters << QString("[%1:v]format=rgba,setpts=PTS-STARTPTS[%2]")
                   .arg(subtitleInputBase + subtitleIndex)
                   .arg(imageLabel);
    // Do not retain the final frame after this subtitle image reaches EOF;
    // otherwise every previous caption remains stacked in later segments.
    filters << QString("[%1][%2]overlay=shortest=0:eof_action=pass:repeatlast=0:format=auto:enable='between(t,%3,%4)'[%5]")
                   .arg(videoOutput, imageLabel, subtitleSeconds(subtitle.start),
                        subtitleSeconds(subtitle.end), outputLabel);
    videoOutput = outputLabel;
    ++subtitleIndex;
  }
  if (!fallbackAssPath.isEmpty()) {
    filters << QString("[%1]subtitles=filename='%2'[vsub]")
                   .arg(videoOutput, escapedFilterPath(fallbackAssPath));
    videoOutput = QStringLiteral("vsub");
  }
  if (audioEnabled)
    filters << audioInputs.join("") +
                   QString("amix=inputs=%1:duration=longest:normalize=0[aout]")
                       .arg(audioInputs.size());

  arguments << "-filter_complex" << filters.join(";") << "-map"
            << QString("[%1]").arg(videoOutput);
  if (audioEnabled)
    arguments << "-map"
              << "[aout]";
  arguments << "-t" << total << "-r" << QString::number(frameRate, 'f', 3)
            << "-c:v"
            << "libx264"
            << "-profile:v" << profile << "-pix_fmt"
            << "yuv420p"
            << "-preset" << encodingSpeed;

  if (videoBitrateMbps > 0) {
    const QString bitrate = QString::number(videoBitrateMbps, 'f', 2) + "M";
    const QString maxrate =
        QString::number(videoBitrateMbps * 1.5, 'f', 2) + "M";
    const QString buffer =
        QString::number(videoBitrateMbps * 2.0, 'f', 2) + "M";
    arguments << "-b:v" << bitrate << "-maxrate" << maxrate << "-bufsize"
              << buffer;
  } else {
    const QString crf = quality == "low"      ? QStringLiteral("28")
                        : quality == "medium" ? QStringLiteral("23")
                                              : QStringLiteral("18");
    arguments << "-crf" << crf;
  }

  if (audioEnabled) {
    arguments << "-c:a"
              << "aac"
              << "-b:a" << QString::number(audioBitrateKbps) + "k"
              << "-ar" << QString::number(audioSampleRate) << "-ac"
              << QString::number(audioChannels);
  } else {
    arguments << "-an";
  }
  arguments << "-movflags"
            << "+faststart";
  arguments << "-progress"
            << "pipe:2" << outputPath;
  return arguments;
}
