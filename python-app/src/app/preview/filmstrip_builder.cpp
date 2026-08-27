#include "app/preview/filmstrip_builder.h"

#include "app/preview/preview_cache.h"
#include "app/preview/seek_thumbnail_extractor.h"

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>

#include <algorithm>

namespace {

// Bumped whenever the layout or the sampling changes, so a cached strip produced
// by an older rule is regenerated instead of being drawn with the new geometry.
constexpr auto kVariantPrefix = "filmstrip-seek-v1";
constexpr auto kPosterVariant = "poster-seek-v1";

QString stripVariant(const FilmstripBuilder::Options &options, int cells) {
  return QStringLiteral("%1-%2x%3x%4%5")
      .arg(QString::fromLatin1(kVariantPrefix))
      .arg(cells)
      .arg(options.cellWidth)
      .arg(options.cellHeight)
      .arg(options.exactSeek ? QStringLiteral("-exact") : QString());
}

// Cover-crop: fill the cell completely and centre the overflow. Letterboxing
// each tile instead would leave black bars between every thumbnail on the
// timeline, which is not what the strip is meant to look like.
void drawCell(QPainter *painter, const QRect &cell, const QImage &still) {
  if (!painter || still.isNull() || cell.isEmpty())
    return;
  const QSize scaled = still.size().scaled(cell.size(), Qt::KeepAspectRatioByExpanding);
  const QRect target(cell.left() + (cell.width() - scaled.width()) / 2,
                     cell.top() + (cell.height() - scaled.height()) / 2,
                     scaled.width(), scaled.height());
  painter->drawImage(target, still);
}

} // namespace

int FilmstripBuilder::cellCountFor(qint64 durationMs, const Options &options) {
  const int minimum = qMax(1, options.minimumCells);
  const int maximum = qMax(minimum, options.maximumCells);
  if (durationMs <= 0)
    return minimum;
  const qint64 perCell = qMax<qint64>(1, options.msPerCell);
  const int derived = int(qMin<qint64>(maximum, durationMs / perCell + 1));
  return qBound(minimum, derived, maximum);
}

FilmstripBuilder::Result FilmstripBuilder::build(const QString &path,
                                                 qint64 durationMs,
                                                 const Options &options,
                                                 const std::atomic_bool *cancel) {
  Result result;
  result.cellWidth = qMax(16, options.cellWidth);
  result.cellHeight = qMax(16, options.cellHeight);
  if (path.isEmpty()) {
    result.error = QStringLiteral("No media path was given.");
    return result;
  }

  const int cells = cellCountFor(durationMs, options);
  const QString variant = stripVariant(options, cells);

  const QString cached = PreviewCache::lookup(path, variant, QStringLiteral("jpg"));
  if (!cached.isEmpty()) {
    result.url = PreviewCache::toUrl(cached);
    result.cells = cells;
    result.fromCache = true;
    return result;
  }

  if (!SeekThumbnailExtractor::available()) {
    result.error = QStringLiteral("This build cannot generate filmstrips.");
    return result;
  }

  SeekThumbnailExtractor extractor(path);
  extractor.setCancelToken(cancel);
  SeekThumbnailExtractor::Options extractorOptions;
  // Cells are cover-cropped, so the still must be at least as large as the cell
  // in both directions; asking for twice the cell keeps the crop sharp without
  // decoding at full source resolution.
  extractorOptions.maximumFrameSize =
      QSize(result.cellWidth * 2, result.cellHeight * 2);
  extractorOptions.exactSeek = options.exactSeek;
  extractorOptions.frameTimeBudgetMs =
      options.timeBudgetMs > 0 ? qMax(1000, options.timeBudgetMs / cells) : 0;
  if (!extractor.open(extractorOptions)) {
    result.error = extractor.error();
    return result;
  }

  const qint64 span = durationMs > 0 ? durationMs : extractor.durationMs();

  QImage strip(result.cellWidth * cells, result.cellHeight,
               QImage::Format_RGB32);
  if (strip.isNull()) {
    result.error = QStringLiteral("Could not allocate the filmstrip image.");
    return result;
  }
  strip.fill(QColor(24, 24, 28));

  QPainter painter(&strip);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  QElapsedTimer timer;
  timer.start();
  QImage lastStill;
  int produced = 0;

  for (int index = 0; index < cells; ++index) {
    if (cancel && cancel->load(std::memory_order_acquire)) {
      result.cancelled = true;
      break;
    }
    const QRect cell(index * result.cellWidth, 0, result.cellWidth,
                     result.cellHeight);
    const bool outOfTime = options.timeBudgetMs > 0 &&
                           timer.elapsed() > options.timeBudgetMs;
    QImage still;
    if (!outOfTime) {
      // Cell centres: (index + 0.5) / cells of the way through.
      const qint64 position =
          span > 0 ? qint64((double(index) + 0.5) * double(span) / double(cells))
                   : 0;
      still = extractor.frameAt(position);
    }
    if (still.isNull())
      still = lastStill; // Repeat rather than leaving a black gap.
    else
      lastStill = still;
    if (!still.isNull()) {
      drawCell(&painter, cell, still);
      ++produced;
    }
  }
  painter.end();

  if (produced == 0) {
    result.error = extractor.error().isEmpty()
                       ? QStringLiteral("No frames could be decoded.")
                       : extractor.error();
    return result;
  }
  if (result.cancelled)
    return result;

  const QString output =
      PreviewCache::reserve(path, variant, QStringLiteral("jpg"));
  if (output.isEmpty()) {
    result.error = QStringLiteral("Could not write to the preview cache.");
    return result;
  }
  if (!strip.save(output, "JPG", qBound(40, options.jpegQuality, 95))) {
    QFile::remove(output);
    result.error = QStringLiteral("Could not save the filmstrip.");
    return result;
  }

  result.url = PreviewCache::toUrl(output);
  result.cells = cells;
  return result;
}

FilmstripBuilder::Result FilmstripBuilder::cached(const QString &path,
                                                  qint64 durationMs,
                                                  const Options &options) {
  Result result;
  result.cellWidth = qMax(16, options.cellWidth);
  result.cellHeight = qMax(16, options.cellHeight);
  if (path.isEmpty())
    return result;
  const int cells = cellCountFor(durationMs, options);
  const QString hit =
      PreviewCache::lookup(path, stripVariant(options, cells),
                           QStringLiteral("jpg"));
  if (hit.isEmpty())
    return result;
  result.url = PreviewCache::toUrl(hit);
  result.cells = cells;
  result.fromCache = true;
  return result;
}

QString FilmstripBuilder::cachedPoster(const QString &path) {
  if (path.isEmpty())
    return {};
  return PreviewCache::lookupUrl(path, QString::fromLatin1(kPosterVariant),
                                 QStringLiteral("jpg"));
}

QString FilmstripBuilder::poster(const QString &path, qint64 durationMs,
                                 const std::atomic_bool *cancel) {
  if (path.isEmpty())
    return {};
  const QString cached =
      PreviewCache::lookup(path, QString::fromLatin1(kPosterVariant),
                           QStringLiteral("jpg"));
  if (!cached.isEmpty())
    return PreviewCache::toUrl(cached);
  if (!SeekThumbnailExtractor::available())
    return {};

  SeekThumbnailExtractor extractor(path);
  extractor.setCancelToken(cancel);
  SeekThumbnailExtractor::Options options;
  options.maximumFrameSize = QSize(480, 480);
  options.frameTimeBudgetMs = 6000;
  if (!extractor.open(options))
    return {};

  const qint64 span = durationMs > 0 ? durationMs : extractor.durationMs();
  // A few percent in, capped at 3 s: past the fade-in of most sources without
  // being so far in that a short clip's poster is its last frame.
  const qint64 position = span > 0 ? qMin<qint64>(3000, span / 20) : 0;
  QImage still = extractor.frameAt(position);
  if (still.isNull() && position > 0)
    still = extractor.frameAt(0);
  if (still.isNull())
    return {};

  const QString output = PreviewCache::reserve(
      path, QString::fromLatin1(kPosterVariant), QStringLiteral("jpg"));
  if (output.isEmpty() || !still.save(output, "JPG", 85)) {
    if (!output.isEmpty())
      QFile::remove(output);
    return {};
  }
  return PreviewCache::toUrl(output);
}
