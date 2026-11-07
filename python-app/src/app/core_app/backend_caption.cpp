#include "app/core_app/backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include "app/subtitles/speech_chain.h"
#include "app/subtitles/speech_fit.h"
#include "app/subtitles/speech_report.h"
#include "app/text_effect_catalog.h"
#include "core/ids.h"

QString Backend::addTextClip(qint64 startMs, const QString &text) {
  if (m_sequenceId.isEmpty())
    createSequence();
  if (startMs < 0)
    startMs = qMax<qint64>(0, m_playheadMs);
  // Long enough to read, short enough not to swallow the next line; the user
  // trims it on the timeline like any other clip.
  const qint64 durationMs = 3000;
  const QString body = text.trimmed().isEmpty() ? QStringLiteral("Text")
                                                : text.trimmed();
  rememberState();
  // Same shape SubtitleTimeline::clipsFromTranscript produces, minus the
  // transcript link: transcriptIndex -1 marks it as a hand-placed overlay so an
  // edit to it is never written back to a spoken source line.
  QVariantMap clip{{"id", QString::fromStdString(core::new_id("clip"))},
                   {"name", body},
                   {"text", body},
                   {"kind", QStringLiteral("subtitle")},
                   {"track", QStringLiteral("S1")},
                   {"mediaId", QString()},
                   {"startMs", startMs},
                   {"sourceInMs", 0},
                   {"sourceDurationMs", durationMs},
                   {"durationMs", durationMs},
                   {"enabled", true},
                   {"wordTimes", QVariantList{}},
                   {"transcriptIndex", -1}};
  m_clips.append(clip);
  markDirty();
  clearError();
  emit clipsChanged();
  emit timelineChanged();
  return clip.value(QStringLiteral("id")).toString();
}

bool Backend::updateTextClip(const QString &clipId, const QString &text) {
  const QString body = text.trimmed();
  for (auto &value : m_clips) {
    QVariantMap clip = value.toMap();
    if (clip.value(QStringLiteral("id")).toString() != clipId ||
        clip.value(QStringLiteral("kind")) != QStringLiteral("subtitle"))
      continue;
    if (clip.value(QStringLiteral("text")).toString() == body)
      return true;
    rememberState();
    clip[QStringLiteral("text")] = body;
    // The clip's name is what the timeline bar shows, so keep it readable even
    // when the overlay has been cleared to empty.
    clip[QStringLiteral("name")] =
        body.isEmpty() ? QStringLiteral("Text") : body;
    value = clip;
    markDirty();
    // A text edit changes the caption's words, not the picture or the timing.
    // clipsChanged still fires so the timeline bar's label and the monitor's
    // caption overlay update, but the monitor is told to keep its decoded frame
    // instead of re-seeking it on every keystroke, and timelineChanged - which
    // would relayout the whole track - is left out.
    m_skipNextPreviewSeek = true;
    emit clipsChanged();
    return true;
  }
  return false;
}

bool Backend::takePreviewSeekSuppression() {
  const bool suppress = m_skipNextPreviewSeek;
  m_skipNextPreviewSeek = false;
  return suppress;
}

void Backend::setCaptionFontFamily(const QString &family) {
  const QString value = family.trimmed();
  if (value.isEmpty() || value == m_captionStyle.fontFamily)
    return;
  m_captionStyle.fontFamily = value;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionFontSize(int size) {
  // Headroom to grow a caption large the way CapCut allows - the old ceiling of
  // 120 stopped the text well short of filling a vertical frame.
  const int value = qBound(1, size, 400);
  if (value == m_captionStyle.fontSize)
    return;
  m_captionStyle.fontSize = value;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBoxWidth(double fraction) {
  const double value = qBound(0.1, fraction, 1.0);
  if (qFuzzyCompare(value, m_captionStyle.boxWidth))
    return;
  m_captionStyle.boxWidth = value;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionTextColor(const QString &color) {
  if (color.trimmed().isEmpty() || color == m_captionStyle.textColor)
    return;
  m_captionStyle.textColor = color;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBold(bool bold) {
  if (bold == m_captionStyle.bold)
    return;
  m_captionStyle.bold = bold;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionItalic(bool italic) {
  if (italic == m_captionStyle.italic)
    return;
  m_captionStyle.italic = italic;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBackgroundVisible(bool visible) {
  if (visible == m_captionStyle.backgroundVisible)
    return;
  m_captionStyle.backgroundVisible = visible;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBackgroundColor(const QString &color) {
  if (color.trimmed().isEmpty() || color == m_captionStyle.backgroundColor)
    return;
  m_captionStyle.backgroundColor = color;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionPosition(const QString &position) {
  if ((position != "top" && position != "center" && position != "bottom" &&
       position != "custom") ||
      position == m_captionStyle.position)
    return;
  m_captionStyle.position = position;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionAlignment(const QString &alignment) {
  if ((alignment != "left" && alignment != "center" &&
       alignment != "right") ||
      alignment == m_captionStyle.alignment)
    return;
  m_captionStyle.alignment = alignment;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurEnabled(bool enabled) {
  if (enabled == m_captionStyle.blurEnabled)
    return;
  m_captionStyle.blurEnabled = enabled;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurTrackingEnabled(bool enabled) {
  if (enabled == m_captionStyle.blurTrackingEnabled)
    return;
  m_captionStyle.blurTrackingEnabled = enabled;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurRegionNormalized(double x, double y, double width,
                                             double height) {
  const double boundedWidth = qBound(0.05, width, 1.0);
  const double boundedHeight = qBound(0.05, height, 1.0);
  const double boundedX = qBound(0.0, x, 1.0 - boundedWidth);
  const double boundedY = qBound(0.0, y, 1.0 - boundedHeight);
  if (qFuzzyCompare(m_captionStyle.blurRegionX, boundedX) &&
      qFuzzyCompare(m_captionStyle.blurRegionY, boundedY) &&
      qFuzzyCompare(m_captionStyle.blurRegionWidth, boundedWidth) &&
      qFuzzyCompare(m_captionStyle.blurRegionHeight, boundedHeight))
    return;
  m_captionStyle.blurRegionX = boundedX;
  m_captionStyle.blurRegionY = boundedY;
  m_captionStyle.blurRegionWidth = boundedWidth;
  m_captionStyle.blurRegionHeight = boundedHeight;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurStrength(int strength) {
  const int value = qBound(1, strength, 64);
  if (value == m_captionStyle.blurStrength)
    return;
  m_captionStyle.blurStrength = value;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionBlurPadding(int padding) {
  const int value = qBound(0, padding, 64);
  if (value == m_captionStyle.blurPadding)
    return;
  m_captionStyle.blurPadding = value;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionPositionNormalized(double x, double y) {
  const double boundedX = qBound(0.0, x, 1.0);
  const double boundedY = qBound(0.0, y, 1.0);
  if (m_captionStyle.position == "custom" &&
      qFuzzyCompare(m_captionStyle.positionX, boundedX) &&
      qFuzzyCompare(m_captionStyle.positionY, boundedY))
    return;
  m_captionStyle.position = QStringLiteral("custom");
  m_captionStyle.positionX = boundedX;
  m_captionStyle.positionY = boundedY;
  markDirty();
  emit captionStyleChanged();
}

QVariantList Backend::textEffectStyles() const {
  return TextEffectCatalog::styles();
}

QVariantList Backend::textEffectAnimations() const {
  return TextEffectCatalog::animations();
}

QVariantMap Backend::textEffectStyleFor(const QString &id) const {
  return TextEffectCatalog::styleFor(id);
}

QVariantMap Backend::solveCaptionAnimation(const QString &text, double elapsedMs,
                                           double cueDurationMs,
                                           const QString &animationId,
                                           double speed,
                                           const QString &wordTimes) const {
  return TextEffectCatalog::solve(
      text, animationId.isEmpty() ? m_captionStyle.animation : animationId,
      speed > 0.0 ? speed : m_captionStyle.animationSpeed, elapsedMs,
      cueDurationMs, wordTimes);
}

QVariantMap Backend::captionGlyphLayout(const QString &text,
                                       const QString &animationId,
                                       const QString &fontFamily, int pixelSize,
                                       bool bold, bool italic) const {
  return TextEffectCatalog::layoutFor(
      text, animationId.isEmpty() ? m_captionStyle.animation : animationId,
      fontFamily.isEmpty() ? m_captionStyle.fontFamily : fontFamily, pixelSize,
      bold, italic);
}

QString Backend::captionAudioFromClip(const QVariantMap &clip,
                                      qint64 cueStartMs,
                                      qint64 *sourceStartMs) const {
  if (clip.isEmpty())
    return QString();
  const qint64 clipStart = clip.value(QStringLiteral("startMs")).toLongLong();
  const qint64 clipEnd =
      clipStart + clip.value(QStringLiteral("durationMs")).toLongLong();
  if (cueStartMs < clipStart || cueStartMs >= clipEnd)
    return QString();
  const QVariantMap media =
      mediaById(clip.value(QStringLiteral("mediaId")).toString());
  const QString path = media.value(QStringLiteral("path")).toString();
  if (path.isEmpty() || media.value(QStringLiteral("channels")).toInt() <= 0)
    return QString();
  if (sourceStartMs)
    *sourceStartMs = clip.value(QStringLiteral("sourceInMs")).toLongLong() +
                     (cueStartMs - clipStart);
  return path;
}

QString Backend::captionAudioSourceFor(qint64 cueStartMs,
                                       qint64 *sourceStartMs) const {
  // The lanes the helper has already resolved for this instant, so finding the
  // audio under a cue is two map lookups instead of a scan over the timeline.
  // Extracted audio first: it is the voice on its own, which is a cleaner
  // envelope than a mix with music under it.
  const QString extracted = captionAudioFromClip(
      m_videoPreviewHelper.activeAudioClip().toMap(), cueStartMs, sourceStartMs);
  if (!extracted.isEmpty())
    return extracted;
  return captionAudioFromClip(m_videoPreviewHelper.activeClipMap(), cueStartMs,
                              sourceStartMs);
}

QString Backend::captionAudioSourceForExport(qint64 cueStartMs,
                                             qint64 *sourceStartMs) const {
  // No playhead here, so the clip has to be found. mediaClips() is the timeline
  // minus the cues - a handful of entries against a whole transcript - and an
  // audio lane wins over the video it was extracted from, same as above.
  QString fallback;
  qint64 fallbackStart = 0;
  for (const QVariant &value : mediaClips()) {
    const QVariantMap clip = value.toMap();
    qint64 start = 0;
    const QString path = captionAudioFromClip(clip, cueStartMs, &start);
    if (path.isEmpty())
      continue;
    if (clip.value(QStringLiteral("kind")) == QStringLiteral("audio")) {
      if (sourceStartMs)
        *sourceStartMs = start;
      return path;
    }
    if (fallback.isEmpty()) {
      fallback = path;
      fallbackStart = start;
    }
  }
  if (!fallback.isEmpty() && sourceStartMs)
    *sourceStartMs = fallbackStart;
  return fallback;
}

void Backend::refreshActiveCaptionWordTimes() {
  QString derived;
  // Only the karaoke animations read a word timing, and only a cue that has none
  // of its own needs one measured: a transcribed cue already carries Whisper's.
  const QVariantMap cue = m_videoPreviewHelper.activeSubtitle().toMap();
  if (m_captionStyle.animation.startsWith(QLatin1String("karaoke")) &&
      !cue.isEmpty() &&
      cue.value(QStringLiteral("wordTimes")).toString().isEmpty()) {
    const QStringList words = cue.value(QStringLiteral("text"))
                                  .toString()
                                  .simplified()
                                  .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const qint64 cueStart = cue.value(QStringLiteral("startMs")).toLongLong();
    qint64 sourceStart = 0;
    const QString path = captionAudioSourceFor(cueStart, &sourceStart);
    if (!path.isEmpty())
      derived = m_captionSpeech.timingFor(
          path, sourceStart,
          cue.value(QStringLiteral("durationMs")).toLongLong(), words);
  }
  if (derived == m_activeCaptionWordTimes)
    return;
  m_activeCaptionWordTimes = derived;
  emit activeCaptionWordTimesChanged();
}

void Backend::setCaptionTextEffect(const QString &id) {
  // Through the catalog, so an id no build knows cannot be stored: the gallery
  // would then show nothing selected and the renderer would have no recipe.
  const QString resolved = TextEffectCatalog::styleFor(id)
                               .value(QStringLiteral("id"))
                               .toString();
  if (resolved == m_captionStyle.textEffect)
    return;
  m_captionStyle.textEffect = resolved;
  markDirty();
  emit captionStyleChanged();
}

void Backend::setCaptionAnimation(const QString &id) {
  const QString resolved = TextEffectCatalog::animationFor(id)
                               .value(QStringLiteral("id"))
                               .toString();
  if (resolved == m_captionStyle.animation)
    return;
  m_captionStyle.animation = resolved;
  markDirty();
  emit captionStyleChanged();
  // Switching to karaoke while parked on an imported cue is what starts that
  // cue's audio analysis, so the highlight has a real timing to follow.
  refreshActiveCaptionWordTimes();
}

void Backend::setCaptionAnimationSpeed(double speed) {
  const double value = qBound(0.25, speed, 3.0);
  if (qFuzzyCompare(value, m_captionStyle.animationSpeed))
    return;
  m_captionStyle.animationSpeed = value;
  markDirty();
  emit captionStyleChanged();
}

QVariantMap Backend::generatedSpeechReport() const {
  return cutpro::speech::buildSpeechReport(m_clips);
}

QVariantMap Backend::fitGeneratedSpeech(double maxLateSeconds) {
  if (m_ttsImportActive)
    return QVariantMap{{QStringLiteral("moved"), 0},
                       {QStringLiteral("busy"), true}};

  cutpro::speech::FitOptions options;
  options.maxLateMs =
      static_cast<qint64>(qBound(0.0, maxLateSeconds, 60.0) * 1000.0 + 0.5);
  options.audioTrackCount = m_audioTrackCount;
  options.lockedLane = [this](int number) {
    return trackLocked(QStringLiteral("A%1").arg(number));
  };

  rememberState();
  QVariantMap result = cutpro::speech::fitGeneratedSpeech(m_clips, options);
  if (result.value(QStringLiteral("moved")).toInt() <= 0) {
    // Already where it wants them. Undoing nothing is worse than not offering it.
    if (!m_undo.isEmpty()) {
      m_undo.removeLast();
      emit historyChanged();
    }
    return result;
  }

  // A voice may have been chained onto a track the sequence does not have yet, and a
  // track the old layout needed may now be empty.
  const QVariantList tracks = result.value(QStringLiteral("tracks")).toList();
  for (const QVariant &name : tracks)
    ensureTrackExists(name.toString());
  pruneEmptyTracks();
  markDirty();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  return result;
}

QVariantMap Backend::chainGeneratedSpeech(int gapMs) {
  if (m_ttsImportActive)
    return QVariantMap{{QStringLiteral("moved"), 0},
                       {QStringLiteral("busy"), true}};

  cutpro::speech::ChainOptions options;
  options.gapMs = qBound(0, gapMs, 5000);
  options.audioTrackCount = m_audioTrackCount;
  options.lockedLane = [this](int number) {
    return trackLocked(QStringLiteral("A%1").arg(number));
  };

  rememberState();
  QVariantMap result = cutpro::speech::chainGeneratedSpeech(m_clips, options);
  if (result.value(QStringLiteral("moved")).toInt() <= 0) {
    // Already chained. Undoing nothing is worse than not offering it.
    if (!m_undo.isEmpty()) {
      m_undo.removeLast();
      emit historyChanged();
    }
    return result;
  }

  const QVariantList chainTracks = result.value(QStringLiteral("tracks")).toList();
  for (const QVariant &name : chainTracks)
    ensureTrackExists(name.toString());
  pruneEmptyTracks();
  markDirty();
  emit clipsChanged();
  emit tracksChanged();
  emit timelineChanged();
  return result;
}

QVariantMap Backend::speechSpeedForPicture() const {
  cutpro::speech::ChainOptions options;
  options.gapMs =
      qBound(0, uiStateValue(QStringLiteral("tts/gapMs"), 0).toInt(), 5000);
  return cutpro::speech::speechSpeedForPicture(m_clips, options);
}
