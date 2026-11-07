#include "app/export/sequence_export_builder.h"
#include "app/export/export_encoder_policy.h"
#include "app/media/noise_removal.h"
#include "app/effects/clip_effects_pipeline.h"
#include "app/effects/audio_effect_pipeline.h"
#include "app/export/timeline_effect_window.h"
#include "app/lumetri/custom_blur_pipeline.h"
#include "app/lumetri/lumetri_pipeline.h"
#include "app/subtitles/subtitle_io.h"
#include "app/effects/video_effect_pipeline.h"
#include "app/subtitles/khmer_support.h"
#include "app/guard/license_session.h"

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

// atempo only behaves well in [0.5, 2.0], so extreme speeds are reached by
// chaining stages (e.g. 4x = atempo=2.0,atempo=2.0). Pitch is preserved.
QString buildAtempoChain(double factor) {
  QStringList parts;
  double remaining = factor;
  while (remaining > 2.0) {
    parts << QStringLiteral("atempo=2.0");
    remaining /= 2.0;
  }
  while (remaining < 0.5) {
    parts << QStringLiteral("atempo=0.5");
    remaining /= 0.5;
  }
  parts << QStringLiteral("atempo=%1").arg(remaining, 0, 'f', 6);
  return parts.join(',');
}

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
  QVariantList timelineEffects;
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
                          {"text", clip.value("text").toString()},
                          // Carried through so a karaoke caption burns in on the
                          // speaker's timing, not on an even split of the cue.
                          {"wordTimes", clip.value("wordTimes")}});
      } else if (clip.value("kind") == "effect") {
        // An effect-track item is not something to decode - it has no media
        // behind it. It applies to whatever picture its bar covers, so it is
        // held aside here and folded into each overlapping video clip below.
        if (clip.value("durationMs").toLongLong() > 0)
          timelineEffects.append(clip);
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
  // The output codec. Everything above is shared; only the final -c:v block and
  // the base-canvas alpha differ per format. Unknown values fall back to H.264
  // so an older project (no videoFormat key) exports exactly as before.
  QString videoFormat = settings.value("videoFormat", "h264").toString().toLower();
  if (!QStringList{"h264", "hevc", "hevc422", "av1", "rle"}.contains(videoFormat))
    videoFormat = QStringLiteral("h264");
  // Only QuickTime RLE carries a real alpha channel, and only then does a
  // transparent base make sense: gaps the footage never covers stay see-through
  // instead of being flattened onto black.
  const bool wantsAlpha = videoFormat == QStringLiteral("rle");

  QStringList arguments{"-y"};
  const auto addCanvasInput = [&](const QString &filter) {
    arguments << "-f" << "lavfi";
    if (!settings.value("continuousCanvas", false).toBool())
      arguments << "-t" << total;
    arguments << "-i" << filter;
  };
  addCanvasInput(QString("color=c=%1:s=%2x%3:r=%4")
                     .arg(wantsAlpha ? QStringLiteral("black@0.0")
                                     : QStringLiteral("black"))
                     .arg(width)
                     .arg(height)
                     .arg(frameRate, 0, 'f', 3));
  addCanvasInput(QStringLiteral("anullsrc=r=48000:cl=stereo"));
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
    const QString replacementPath = NoiseRemoval::replacementPath(clip, item);
    if (!replacementPath.isEmpty()) {
      arguments << "-i" << replacementPath;
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
  }

  QStringList filters;
  QString videoOutput = "0:v";
  // Audio is split into two buses so the mix can optionally duck the bed under
  // the narration: everything from a TTS voice track goes to narration, every
  // other audible clip (original sound, music) to the bed. The silent anullsrc
  // ([1:a]) seeds the bed so amix always has an input even with no clips.
  QStringList bedInputs{"[1:a]"};
  QStringList narrationInputs;
  int videoIndex = 0, audioIndex = 0;
  for (const auto &clip : exportClips) {
    const int input = clip.value("inputIndex").toInt();
    const qint64 start = clip.value("startMs").toLongLong();
    const qint64 sourceIn = clip.value("sourceInMs").toLongLong();
    const qint64 duration = clip.value("durationMs").toLongLong();
    // Speed retiming. The timeline length (duration) is the *output* length; the
    // amount of source consumed is duration*factor. factor==1 leaves every
    // command below byte-identical to an un-retimed export.
    const QVariantMap clipEffects = clip.value("effects").toMap();
    const double speedFactor =
        qMax(0.01, clipEffects.value("speed", 100.0).toDouble() / 100.0);
    const bool retimed = qAbs(speedFactor - 1.0) > 1e-6;
    const qint64 sourceUsed =
        retimed ? qRound64(duration * speedFactor) : duration;
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
      // The same channel map the Custom Blur masks use, handed to the intrinsic
      // transform stages too so a keyframed Position or Rotation animates in the
      // rendered file instead of exporting as one static value.
      const QVariantMap clipChannels = clip.value("keyframes").toMap();
      // The clip's animation channels ride along on the clip map (Backend attaches
      // them before it calls this), so a keyframed Blurriness renders the same
      // curve the program monitor shows instead of one baked-in value.
      QVariantList customBlurMasks = CustomBlurPipeline::enabledMasks(
          clip.value("effectStack").toList(),
          clip.value("keyframes").toMap());
      // Then the effect track. Each bar contributes only over the stretch where
      // it and this clip overlap, so the extent of the bar is what decides when
      // its effects are on - outside it the picture comes through untouched.
      // Timeline ms throughout: the trim/setpts below has already put this clip on
      // sequence time, which is the clock both gates use.
      QStringList timelineEffectChains;
      for (const auto &effectValue : timelineEffects) {
        const QVariantMap effectItem = effectValue.toMap();
        const qint64 effectStart = effectItem.value("startMs").toLongLong();
        const qint64 effectEnd =
            effectStart + effectItem.value("durationMs").toLongLong();
        const qint64 windowStart = qMax(effectStart, start);
        const qint64 windowEnd = qMin(effectEnd, start + duration);
        if (windowEnd <= windowStart + 1)
          continue;
        const QVariantList effectItemStack =
            effectItem.value("effectStack").toList();
        customBlurMasks += CustomBlurPipeline::windowedMasks(
            effectItemStack, effectItem.value("keyframes").toMap(), windowStart,
            windowEnd);
        const QString windowedChain = TimelineEffectWindow::gatedFilters(
            effectItemStack, windowStart, windowEnd);
        if (!windowedChain.isEmpty())
          timelineEffectChains << windowedChain;
      }
      QStringList effectParts;
      const QString intrinsicFilters =
          ClipEffectsPipeline::videoFilters(effects, clipChannels);
      const QString stackFilters = VideoEffectPipeline::filters(
          clip.value("effectStack").toList(), start, duration);
      if (!intrinsicFilters.isEmpty())
        effectParts << intrinsicFilters;
      if (!stackFilters.isEmpty())
        effectParts << stackFilters;
      // Last, because the lane sits over the clip: what a bar acts on is the
      // picture the clip's own effects have already produced.
      effectParts += timelineEffectChains;
      // Frame blending (only meaningful when retimed): cross-dissolve adjacent
      // frames so sped/slowed motion looks smooth. Frame sampling, the default,
      // needs nothing - setpts alone drops or repeats whole frames.
      if (retimed &&
          clipEffects.value("timeInterpolation").toString() ==
              QStringLiteral("blending"))
        effectParts << QStringLiteral("minterpolate=fps=%1:mi_mode=blend")
                           .arg(frameRate, 0, 'f', 3);
      const QString effectFilters = effectParts.join(',');
      const QString effectChain = effectFilters.isEmpty()
                                      ? QString()
                                      : QString(",%1").arg(effectFilters);
      const QString initialPrepared =
          customBlurMasks.isEmpty() ? prepared
                                    : QStringLiteral("vpreblur%1").arg(videoIndex);
      // setpts places the clip on the timeline; when retimed it also divides the
      // presentation time by the speed factor so the source plays over the
      // clip's (shorter/longer) timeline length.
      const QString vsetpts =
          retimed ? QStringLiteral("(PTS-STARTPTS)/%1+%2/TB")
                        .arg(speedFactor, 0, 'f', 6)
                        .arg(seconds(start))
                  : QStringLiteral("PTS-STARTPTS+%1/TB").arg(seconds(start));
      // Field handling runs before scaling, on the source picture. Each entry
      // leads with a comma so an empty value collapses cleanly.
      const QString fieldMode = clipEffects.value("fieldMode").toString();
      QString preScaleChain;
      if (fieldMode == QStringLiteral("deinterlace"))
        preScaleChain = clipEffects.value("reverseFieldDominance").toBool()
                            ? QStringLiteral(",yadif=parity=1")
                            : QStringLiteral(",yadif");
      else if (fieldMode == QStringLiteral("flicker"))
        // No true field metadata on the export picture; approximate flicker
        // removal with a light vertical low-pass (Gaussian, vertical only).
        preScaleChain = QStringLiteral(",gblur=sigma=0:sigmaV=0.8:steps=1");
      filters
          << QString(
                 "[%1:v]trim=start=%2:duration=%3,setpts=%4%11,"
                 "scale=%5:%6:force_original_aspect_ratio=decrease%7%9%10,"
                 "setsar=1[%8]")
                 .arg(input)
                 .arg(seconds(sourceIn))
                 .arg(seconds(sourceUsed))
                 .arg(vsetpts)
                 .arg(width)
                 .arg(height)
                 .arg(scaleFlags)
                 .arg(initialPrepared)
                 .arg(colorChain)
                 .arg(effectChain)
                 .arg(preScaleChain);
      const QString preparedOutput = CustomBlurPipeline::appendFilters(
          &filters, initialPrepared, QStringLiteral("vblur%1").arg(videoIndex),
          customBlurMasks);
      const QString blendSpec = ClipEffectsPipeline::blendMode(effects);
      if (blendSpec.isEmpty()) {
        filters << QString("[%1][%2]overlay=x='%3':y='%4':eof_action=pass:"
                           "shortest=0[%5]")
                       .arg(videoOutput, preparedOutput,
                            ClipEffectsPipeline::overlayX(effects, clipChannels),
                            ClipEffectsPipeline::overlayY(effects, clipChannels),
                            composed);
      } else {
        // Blend modes composite the clip against everything already drawn
        // beneath it. FFmpeg's `blend` filter has no positioning and rewrites
        // the whole frame, so: (1) drop the clip onto a full-frame transparent
        // canvas at its overlay position, (2) blend that against the accumulator,
        // then (3) re-apply the clip's own alpha so only its pixels are affected
        // and transparent regions keep the layers below untouched.
        const QString bg = QStringLiteral("vbg%1").arg(videoIndex);
        const QString topFull = QStringLiteral("vtopfull%1").arg(videoIndex);
        const QString baseA = QStringLiteral("vbaseA%1").arg(videoIndex);
        const QString baseB = QStringLiteral("vbaseB%1").arg(videoIndex);
        const QString topRgb = QStringLiteral("vtoprgb%1").arg(videoIndex);
        const QString topAlphaSrc = QStringLiteral("vtopas%1").arg(videoIndex);
        const QString topMask = QStringLiteral("vmask%1").arg(videoIndex);
        const QString blended = QStringLiteral("vblended%1").arg(videoIndex);
        const QString blendMasked =
            QStringLiteral("vbmasked%1").arg(videoIndex);
        filters << QString("color=c=black@0:s=%1x%2:r=%3[%4]")
                       .arg(width)
                       .arg(height)
                       .arg(frameRate, 0, 'f', 3)
                       .arg(bg);
        filters << QString("[%1][%2]overlay=x='%3':y='%4':eof_action=pass[%5]")
                       .arg(bg, preparedOutput,
                            ClipEffectsPipeline::overlayX(effects, clipChannels),
                            ClipEffectsPipeline::overlayY(effects, clipChannels),
                            topFull);
        filters << QString("[%1]split=2[%2][%3]").arg(videoOutput, baseA, baseB);
        filters << QString("[%1]split=2[%2][%3]")
                       .arg(topFull, topRgb, topAlphaSrc);
        filters << QString("[%1]alphaextract[%2]").arg(topAlphaSrc, topMask);
        filters << QString("[%1][%2]blend=%3:shortest=1[%4]")
                       .arg(topRgb, baseA, blendSpec, blended);
        filters << QString("[%1][%2]alphamerge[%3]")
                       .arg(blended, topMask, blendMasked);
        filters << QString("[%1][%2]overlay=eof_action=pass:shortest=0[%3]")
                       .arg(baseB, blendMasked, composed);
      }
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
          clip.value("effectStack").toList(), channels, duration);
      if (!intrinsicAudio.isEmpty())
        audioEffectParts << intrinsicAudio;
      if (!stackAudio.isEmpty())
        audioEffectParts << stackAudio;
      const QString audioFilters = audioEffectParts.join(',');
      const QString audioChain =
          audioFilters.isEmpty() ? QString() : QString(",%1").arg(audioFilters);
        const int audioInput = clip.value("audioInputIndex", input).toInt();
      // Speed for audio: keep pitch with a chained atempo, or let pitch move
      // with the speed via asetrate. Empty (and byte-identical) when factor==1.
      QString atempoSegment;
      if (retimed) {
        if (clipEffects.value("maintainPitch", true).toBool())
          atempoSegment = buildAtempoChain(speedFactor) + QChar(',');
        else
          atempoSegment = QStringLiteral("asetrate=%1,aresample=48000,")
                              .arg(48000.0 * speedFactor, 0, 'f', 3);
      }
      // Preserve the phase of retiming/fades/other audio effects when a render
      // window starts inside a clip. Trim the leading output after processing,
      // then rebase it, because audio delays cannot be negative.
      const qint64 leadingMs = qMax<qint64>(0, -start);
      const QString leadingTrim = leadingMs > 0
          ? QStringLiteral(",atrim=start=%1,asetpts=PTS-STARTPTS").arg(seconds(leadingMs))
          : QString();
      filters << QString(
               "[%1:a]atrim=start=%2:duration=%3,asetpts=PTS-STARTPTS,%7"
                     "aresample=48000%6%8,adelay=%4|%4[%5]")
               .arg(audioInput)
                     .arg(seconds(sourceIn))
                     .arg(seconds(sourceUsed))
                     .arg(qMax<qint64>(0, start))
                     .arg(audio)
                     .arg(audioChain)
                     .arg(atempoSegment)
                     .arg(leadingTrim);
      if (clip.value("generatedBy").toString() ==
          QStringLiteral("text_to_speech"))
        narrationInputs << QString("[%1]").arg(audio);
      else
        bedInputs << QString("[%1]").arg(audio);
    }
  }
  int subtitleIndex = 0;
  for (const auto &subtitle : rasterSubtitles) {
    const QString imageLabel = QStringLiteral("subimg%1").arg(subtitleIndex);
    const QString outputLabel = QStringLiteral("subout%1").arg(subtitleIndex);
    // Keep image paths inside the graph, which Backend can load from a file.
    // Thousands of -i PNG arguments exceed Windows' process command limit even
    // after the graph is moved. Decode each still once with one decoder thread.
    filters << QString("movie=filename='%1':dec_threads=1,format=rgba,setpts=PTS-STARTPTS[%2]")
                   .arg(escapedFilterPath(subtitle.path), imageLabel);
    // Hold the still at EOF, but only within this caption's time range. The
    // enable gate hides it again in gaps and after the caption ends.
    filters << QString("[%1][%2]overlay=shortest=0:eof_action=repeat:repeatlast=1:format=auto:enable='between(t,%3,%4)'[%5]")
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
  QString audioOutput = QStringLiteral("aout");
  if (audioEnabled) {
    const QStringList allInputs = bedInputs + narrationInputs;
    // Auto-ducking: when narration and a real audio bed both exist, pull the bed
    // down while the voice is speaking so dialogue stays intelligible over music
    // or original sound, then let it swell back in the gaps. sidechaincompress
    // keys the bed's gain off the narration bus; the narration itself is mixed
    // back in at full level. bedInputs always holds the silent [1:a] seed, so a
    // real bed means size > 1.
    const bool duckEnabled =
        settings.value("duckMusicUnderNarration", false).toBool();
    if (duckEnabled && !narrationInputs.isEmpty() && bedInputs.size() > 1) {
      // How far the bed is pulled down under speech, as a positive dB figure.
      // Mapped onto the compressor ratio: a deeper target needs a harder ratio.
      const double reductionDb =
          qBound(3.0, settings.value("duckAmountDb", 12.0).toDouble(), 24.0);
      const double ratio = qBound(2.0, reductionDb / 2.0, 20.0);
      filters << narrationInputs.join("") +
                     QString("amix=inputs=%1:duration=longest:normalize=0"
                             "[narrbus]")
                         .arg(narrationInputs.size());
      filters << QStringLiteral("[narrbus]asplit=2[narrmix][narrsc]");
      filters << bedInputs.join("") +
                     QString("amix=inputs=%1:duration=longest:normalize=0"
                             "[bedbus]")
                         .arg(bedInputs.size());
      // threshold ~-30 dBFS (0.032 linear): the bed ducks once the voice rises
      // above a whisper. attack is quick so the duck lands on the first word;
      // release is slow enough that the bed does not pump between syllables.
      filters << QString("[bedbus][narrsc]sidechaincompress=threshold=0.032:"
                         "ratio=%1:attack=20:release=350[bedducked]")
                     .arg(ratio, 0, 'f', 2);
      filters << QStringLiteral(
          "[bedducked][narrmix]amix=inputs=2:duration=longest:normalize=0[aout]");
    } else {
      filters << allInputs.join("") +
                     QString("amix=inputs=%1:duration=longest:normalize=0[aout]")
                         .arg(allInputs.size());
    }
    // Optional master loudness normalization (EBU R128) on the mixed program.
    // Streaming platforms target a fixed integrated loudness (YouTube ~-14,
    // TikTok/Instagram ~-14, broadcast -23 LUFS); without this the export level
    // drifts with however loud the narration and music happened to sit. This is
    // loudnorm's one-pass dynamic mode - accurate enough for a delivery target
    // without the extra probe run a two-pass measurement would need.
    if (settings.value("normalizeLoudness", false).toBool()) {
      const double targetLufs = qBound(
          -31.0, settings.value("loudnessTargetLufs", -14.0).toDouble(), -5.0);
      filters << QString("[aout]loudnorm=I=%1:LRA=11:TP=-1.5[anorm]")
                     .arg(targetLufs, 0, 'f', 1);
      audioOutput = QStringLiteral("anorm");
    }
  }

  // --- Independent licence gate on the render itself --------------------
  // A second, self-contained verify that does NOT trust Backend's m_licensed
  // flag (guard::exportGuardSeed re-reads the stored key and re-runs Ed25519
  // from the low guard module). It returns 0 only for a genuinely licensed,
  // untampered process; anything else - a forged gate, a swapped key, a tripped
  // tamper flag - yields a non-zero seed and this branch grays out the picture
  // and stamps it "UNLICENSED COPY". There is no separate boolean to patch: the
  // branch keys off the verify result directly, at a site in cutpro_export that
  // a crack of the startup path never touches.
  const quint64 guardSeed = guard::exportGuardSeed();
  if (guardSeed != 0) {
    const QString wmIn = videoOutput;
    const QString wmOut = QStringLiteral("vguard");
    filters << QString(
                   "[%1]hue=s=0,"
                   "drawtext=fontfile=C\\:/Windows/Fonts/arialbd.ttf:"
                   "text='UNLICENSED COPY':fontcolor=red@0.80:fontsize=h/11:"
                   "x=(w-tw)/2:y=(h-th)/2:box=1:boxcolor=black@0.40:"
                   "borderw=3:bordercolor=black,"
                   "drawtext=fontfile=C\\:/Windows/Fonts/arialbd.ttf:"
                   "text='Cut Pro - activate a valid licence':"
                   "fontcolor=white@0.85:fontsize=h/26:"
                   "x=(w-tw)/2:y=h-3*th[%2]")
                   .arg(wmIn, wmOut);
    videoOutput = wmOut;
  }

  arguments << "-filter_complex" << filters.join(";") << "-map"
            << QString("[%1]").arg(videoOutput);
  if (audioEnabled)
    arguments << "-map" << QString("[%1]").arg(audioOutput);
  arguments << "-t" << total << "-r" << QString::number(frameRate, 'f', 3);

  // The quality tier decides how the picture is encoded and the bitrate is only
  // a ceiling. -b:v made the encoder read that number as a quota instead and pad
  // every frame up to it, so a 576x1024 clip fitted into an 8K frame - two
  // thirds of which is black bar - still wrote 160 Mbps of mostly nothing, and
  // seven minutes came out at 8 GB. Constant quality with a bitrate cap spends
  // bits only where the picture earns them and clamps the peaks, which is what a
  // "bitrate" control means in every other editor.
  const QString crf = quality == "low"      ? QStringLiteral("28")
                      : quality == "medium" ? QStringLiteral("23")
                                            : QStringLiteral("18");
  const QString encoder = settings.value("resolvedVideoEncoder").toString();
  if (!encoder.isEmpty() && encoder != ExportEncoderPolicy::software(settings) &&
      encoder != ExportEncoderPolicy::hardware(settings)) {
    if (error) *error = QStringLiteral("The selected encoder does not support this video format.");
    return {};
  }
  if (!encoder.isEmpty() && encoder == ExportEncoderPolicy::hardware(settings)) {
    arguments << ExportEncoderPolicy::hardwareArguments(settings);
  } else if (videoFormat == QStringLiteral("hevc") ||
      videoFormat == QStringLiteral("hevc422")) {
    // H.265 halves the bitrate of H.264 at the same look. 422 keeps 10-bit
    // 4:2:2 chroma for grading headroom; x265 picks the main422-10 profile from
    // the pixel format on its own. hvc1 tags it so QuickTime / Windows play it.
    const bool is422 = videoFormat == QStringLiteral("hevc422");
    arguments << "-c:v"
              << "libx265"
              << "-pix_fmt" << (is422 ? "yuv422p10le" : "yuv420p")
              << "-preset" << encodingSpeed << "-crf" << crf << "-tag:v"
              << "hvc1";
    if (videoBitrateMbps > 0) {
      // x265's rate control lives in its own param string; values are kbit/s.
      arguments << "-x265-params"
                << QString("vbv-maxrate=%1:vbv-bufsize=%2")
                       .arg(qRound(videoBitrateMbps * 1000.0))
                       .arg(qRound(videoBitrateMbps * 2000.0));
    }
  } else if (videoFormat == QStringLiteral("av1")) {
    // SVT-AV1: preset is a 0-13 speed knob (higher = faster), not a word, and
    // AV1's CRF scale sits higher than x264's for the same quality.
    const QString svtPreset = quality == "low"      ? QStringLiteral("10")
                              : quality == "medium" ? QStringLiteral("8")
                                                    : QStringLiteral("6");
    const QString svtCrf = quality == "low"      ? QStringLiteral("38")
                           : quality == "medium" ? QStringLiteral("32")
                                                 : QStringLiteral("26");
    arguments << "-c:v"
              << "libsvtav1"
              << "-pix_fmt"
              << "yuv420p"
              << "-preset" << svtPreset << "-crf" << svtCrf;
  } else if (videoFormat == QStringLiteral("rle")) {
    // QuickTime Animation is lossless RLE: no CRF / bitrate / profile knobs, and
    // argb so the transparent base above survives as a real alpha channel.
    arguments << "-c:v"
              << "qtrle"
              << "-pix_fmt"
              << "argb";
  } else {
    arguments << "-c:v"
              << "libx264"
              << "-profile:v" << profile << "-pix_fmt"
              << "yuv420p"
              << "-preset" << encodingSpeed << "-crf" << crf;
    if (videoBitrateMbps > 0) {
      arguments << "-maxrate" << QString::number(videoBitrateMbps, 'f', 2) + "M"
                << "-bufsize"
                << QString::number(videoBitrateMbps * 2.0, 'f', 2) + "M";
    }
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
            << "pipe:1" << outputPath;
  return arguments;
}
