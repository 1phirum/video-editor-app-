#include "app/preview/decode_cost_model.h"

#include "app/preview/codec_decode_traits.h"

#include <QLoggingCategory>
#include <QMutexLocker>
#include <QtMath>

// Every advice change, at warning level, because an invisible one is how a
// preview ends up running at half rate with nothing in the log to say why. The
// line names the measurement that caused it, so the decision can be argued with
// instead of guessed at.
Q_LOGGING_CATEGORY(previewCostLog, "cutpro.preview.cost", QtWarningMsg)

namespace {

// Smoothing for the per-frame average. Slow enough that one slow seek does not
// resize the preview, fast enough to follow a genuine change of source.
constexpr double kSmoothing = 0.15;

int longSideOf(qint64 pixels, int sourceWidth, int sourceHeight) {
  if (pixels <= 0)
    return 0;
  double ratio = 16.0 / 9.0;
  if (sourceWidth > 0 && sourceHeight > 0)
    ratio = double(sourceWidth) / double(sourceHeight);
  const double width = qSqrt(double(pixels) * ratio);
  const double height = width > 0 ? double(pixels) / width : 0.0;
  return qMax(2, qRound(qMax(width, height)));
}

int evenSide(int side) {
  const int even = side - (side % 2);
  return qMax(2, even);
}

} // namespace

DecodeCostModel &DecodeCostModel::instance() {
  static DecodeCostModel model;
  return model;
}

DecodeCostModel::Entry *DecodeCostModel::entryFor(const QString &path) {
  if (path.isEmpty())
    return nullptr;
  const auto found = m_sources.find(path);
  if (found != m_sources.end())
    return &*found;
  trimLocked();
  return &m_sources[path];
}

void DecodeCostModel::trimLocked() {
  if (m_sources.size() < kMaximumSources)
    return;
  // The source with the fewest observations is the one whose advice is worth the
  // least; a file being played has thousands.
  auto victim = m_sources.begin();
  for (auto it = m_sources.begin(); it != m_sources.end(); ++it) {
    if (it->frames + it->stills < victim->frames + victim->stills)
      victim = it;
  }
  m_sources.erase(victim);
}

void DecodeCostModel::noteSource(const QString &path, const QString &codecName,
                                 int width, int height, double frameRate) {
  QMutexLocker locker(&m_mutex);
  Entry *entry = entryFor(path);
  if (!entry)
    return;
  const CodecDecodeTraits::Traits traits =
      CodecDecodeTraits::fromCodecName(codecName);
  entry->codec = traits.name;
  entry->costWeight = traits.costWeight;
  entry->expensive = traits.expensive();
  if (width > 0 && height > 0) {
    entry->width = width;
    entry->height = height;
  }
  if (frameRate > 0)
    entry->frameRate = frameRate;

  // The static estimate. Only the combinations that cannot work are bounded
  // here; everything else waits for a measurement rather than guessing away
  // picture the machine may well afford.
  const qint64 pixels = qint64(qMax(0, entry->width)) * qMax(0, entry->height);
  entry->advisedSide = 0;
  entry->advisedFrameRate = 0.0;
  if (traits.costWeight >= 2.5 && pixels >= 3840LL * 2160) {
    entry->advisedSide = 1280;
    entry->advisedFrameRate = 24.0;
  } else if (traits.expensive() && pixels > 1920LL * 1088) {
    entry->advisedSide = 1920;
    if (traits.costWeight >= 2.5)
      entry->advisedFrameRate = 30.0;
  }
}

void DecodeCostModel::notePlaybackFrame(const QString &path,
                                        double milliseconds, qint64 pixels) {
  if (milliseconds <= 0 || pixels <= 0)
    return;
  QMutexLocker locker(&m_mutex);
  Entry *entry = entryFor(path);
  if (!entry)
    return;
  entry->frameMs = entry->frames == 0
                       ? milliseconds
                       : entry->frameMs * (1.0 - kSmoothing) +
                             milliseconds * kSmoothing;
  entry->framePixels = pixels;
  ++entry->frames;

  const double frameRate = entry->advisedFrameRate > 0 ? entry->advisedFrameRate
                           : entry->frameRate > 0      ? entry->frameRate
                                                       : 30.0;
  const double budgetMs = kFrameTimeShare * 1000.0 / qMax(1.0, frameRate);
  if (entry->frameMs > budgetMs * 1.25) {
    entry->underBudgetFrames = 0;
    if (++entry->overBudgetFrames < kLowerAfterFrames)
      return;
    entry->overBudgetFrames = 0;

    const int currentSide =
        entry->advisedSide > 0
            ? entry->advisedSide
            : longSideOf(pixels, entry->width, entry->height);
    // Cost is roughly proportional to area, so the side moves with the square
    // root of the overshoot.
    const double scale =
        qBound(0.5, qSqrt(budgetMs / entry->frameMs), 0.95);
    const int lowered = evenSide(qRound(currentSide * scale));
    if (lowered >= kMinimumAdvisedSide) {
      entry->advisedSide = lowered;
      ++m_downgrades;
      qCWarning(previewCostLog).nospace()
          << "preview cost: frame is " << qRound(entry->frameMs) << " ms against a "
          << qRound(budgetMs) << " ms budget at " << frameRate
          << " fps - lowering the preview to " << lowered << " px";
      return;
    }
    entry->advisedSide = kMinimumAdvisedSide;
    // Already as small as a preview may get: buy the time in frame rate
    // instead, which costs smoothness rather than detail.
    const double loweredRate = frameRate <= 24.0 ? 20.0
                               : frameRate <= 30.0 ? 24.0
                                                   : 30.0;
    if (loweredRate < frameRate) {
      entry->advisedFrameRate = loweredRate;
      ++m_downgrades;
      qCWarning(previewCostLog).nospace()
          << "preview cost: frame is " << qRound(entry->frameMs)
          << " ms and the picture is already at " << kMinimumAdvisedSide
          << " px - pacing the preview at " << loweredRate << " fps instead of "
          << frameRate;
    }
    return;
  }

  if (entry->frameMs < budgetMs * 0.5) {
    entry->overBudgetFrames = 0;
    if (++entry->underBudgetFrames < kRaiseAfterFrames)
      return;
    entry->underBudgetFrames = 0;
    if (entry->advisedSide <= 0)
      return;
    // Give the picture back gradually, and stop having an opinion once the
    // advice reaches the source's own size.
    const int sourceLongSide = qMax(entry->width, entry->height);
    const int raised = evenSide(qRound(entry->advisedSide * 1.25));
    if (sourceLongSide > 0 && raised >= sourceLongSide) {
      entry->advisedSide = 0;
      entry->advisedFrameRate = 0.0;
      qCWarning(previewCostLog).nospace()
          << "preview cost: frame is " << qRound(entry->frameMs)
          << " ms, comfortably inside the " << qRound(budgetMs)
          << " ms budget - the model has no opinion any more, full source size "
             "and frame rate";
    } else {
      entry->advisedSide = raised;
      qCWarning(previewCostLog).nospace()
          << "preview cost: frame is " << qRound(entry->frameMs) << " ms of a "
          << qRound(budgetMs) << " ms budget - raising the preview to " << raised
          << " px";
    }
    ++m_upgrades;
    return;
  }
  entry->overBudgetFrames = 0;
  entry->underBudgetFrames = 0;
}

void DecodeCostModel::noteStillDecode(const QString &path, double milliseconds,
                                      bool succeeded) {
  QMutexLocker locker(&m_mutex);
  Entry *entry = entryFor(path);
  if (!entry)
    return;
  if (milliseconds > 0) {
    entry->stillMs = entry->stills == 0
                         ? milliseconds
                         : entry->stillMs * (1.0 - kSmoothing) +
                               milliseconds * kSmoothing;
  }
  ++entry->stills;
  if (!succeeded)
    ++entry->stillFailures;
}

DecodeCostModel::Advice DecodeCostModel::adviceFor(const QString &path,
                                                   int sourceWidth,
                                                   int sourceHeight) const {
  Advice advice;
  QMutexLocker locker(&m_mutex);
  const auto found = m_sources.constFind(path);
  if (found == m_sources.constEnd())
    return advice;
  advice.costWeight = found->costWeight;
  advice.expensive = found->expensive;
  advice.measured = found->frames > 0;
  advice.maximumFrameRate = found->advisedFrameRate;
  advice.maximumSide = found->advisedSide;
  // An advice above the source's own size is no advice at all.
  const int sourceLongSide = qMax(qMax(0, sourceWidth), qMax(0, sourceHeight));
  if (advice.maximumSide > 0 && sourceLongSide > 0 &&
      advice.maximumSide >= sourceLongSide)
    advice.maximumSide = 0;
  return advice;
}

QString DecodeCostModel::codecFor(const QString &path) const {
  QMutexLocker locker(&m_mutex);
  const auto found = m_sources.constFind(path);
  return found == m_sources.constEnd() ? QString() : found->codec;
}

void DecodeCostModel::forget(const QString &path) {
  QMutexLocker locker(&m_mutex);
  m_sources.remove(path);
}

void DecodeCostModel::clear() {
  QMutexLocker locker(&m_mutex);
  m_sources.clear();
}

QVariantMap DecodeCostModel::statistics() const {
  QVariantMap stats;
  QMutexLocker locker(&m_mutex);
  stats[QStringLiteral("costTrackedSources")] = m_sources.size();
  stats[QStringLiteral("costDowngrades")] = qulonglong(m_downgrades);
  stats[QStringLiteral("costUpgrades")] = qulonglong(m_upgrades);
  // The busiest source is the one being previewed, and its numbers are the ones
  // worth showing in the diagnostics panel.
  const Entry *busiest = nullptr;
  for (auto it = m_sources.constBegin(); it != m_sources.constEnd(); ++it) {
    if (!busiest || it->frames > busiest->frames)
      busiest = &*it;
  }
  if (busiest) {
    stats[QStringLiteral("costCodec")] = busiest->codec;
    stats[QStringLiteral("costWeight")] = busiest->costWeight;
    stats[QStringLiteral("costFrameMs")] =
        qRound(busiest->frameMs * 100.0) / 100.0;
    stats[QStringLiteral("costStillMs")] =
        qRound(busiest->stillMs * 100.0) / 100.0;
    stats[QStringLiteral("costAdvisedSide")] = busiest->advisedSide;
    stats[QStringLiteral("costAdvisedFrameRate")] = busiest->advisedFrameRate;
    stats[QStringLiteral("costStillFailures")] =
        qulonglong(busiest->stillFailures);
  }
  return stats;
}
