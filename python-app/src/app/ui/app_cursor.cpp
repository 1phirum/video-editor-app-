#include "app/ui/app_cursor.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>

namespace {

// One cursor as the asset folder describes it: one file per raster density
// (base, @3to2x = 1.5x, @2x, @3x), each with its own hotspot in the base 32x32
// logical grid. The hotspot is stored per density because a couple of names —
// VerticalText — ship under two different hotspots.
struct CursorAsset {
  QHash<int, QString> paths;    // density * 100 -> resource path
  QHash<int, QPoint> hotspots;  // density * 100 -> hotspot
};

using CursorIndex = QHash<QString, CursorAsset>;

const CursorIndex &index() {
  static const CursorIndex assets = [] {
    CursorIndex result;
    // Cur_<Name>_<hotspotX>_<hotspotY>[@2x|@3x|@3to2x].png. The name itself may
    // end in a digit (Light1), so the two hotspot groups are anchored to the end
    // and the greedy name group backtracks around them. Three of Adobe's 1.5x
    // files are named "@3to2" without the trailing x, hence the optional x.
    static const QRegularExpression pattern(
        QStringLiteral("^Cur_(.+)_(\\d+)_(\\d+)(@2x|@3x|@3to2x?)?$"));
    const QDir dir(QStringLiteral(":/assets/cursors"));
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files);
    for (const QFileInfo &file : files) {
      const QRegularExpressionMatch match =
          pattern.match(file.completeBaseName());
      if (!match.hasMatch())
        continue;
      const QString variant = match.captured(4);
      int density = 100;
      if (variant == QStringLiteral("@2x"))
        density = 200;
      else if (variant == QStringLiteral("@3x"))
        density = 300;
      else if (variant.startsWith(QStringLiteral("@3to2")))
        density = 150;
      CursorAsset &asset = result[match.captured(1)];
      asset.paths.insert(density, file.filePath());
      asset.hotspots.insert(
          density, QPoint(match.captured(2).toInt(), match.captured(3).toInt()));
    }
    return result;
  }();
  return assets;
}

// The smallest artwork that is at least as dense as the screen, so a cursor is
// downscaled rather than blown up. Falls back to the densest file on offer.
int densityFor(const CursorAsset &asset, qreal devicePixelRatio) {
  static const int ladder[] = {100, 150, 200, 300};
  const int wanted = qRound(qMax(1.0, devicePixelRatio) * 100.0);
  int best = 0;
  for (const int density : ladder) {
    if (!asset.paths.contains(density))
      continue;
    if (density >= wanted)
      return density;
    best = density;
  }
  return best;
}

} // namespace

QCursor AppCursor::cursor(const QString &name, qreal devicePixelRatio) {
  const auto asset = index().constFind(name);
  if (asset == index().constEnd())
    return QCursor(Qt::ArrowCursor);
  const int density = densityFor(*asset, devicePixelRatio);
  if (density == 0)
    return QCursor(Qt::ArrowCursor);

  // Decoding a PNG per enter/leave is wasteful when a trim handle is crossed
  // dozens of times a second, and QCursor is cheap to copy.
  static QHash<QString, QCursor> cache;
  const QString key = name + QLatin1Char('@') + QString::number(density);
  const auto cached = cache.constFind(key);
  if (cached != cache.constEnd())
    return *cached;

  QPixmap pixmap(asset->paths.value(density));
  if (pixmap.isNull())
    return QCursor(Qt::ArrowCursor);
  // The hotspot is expressed in the base 32x32 grid, which is what QCursor wants
  // once the pixmap carries its own ratio.
  pixmap.setDevicePixelRatio(density / 100.0);
  const QPoint hotspot = asset->hotspots.value(density);
  const QCursor built(pixmap, hotspot.x(), hotspot.y());
  cache.insert(key, built);
  return built;
}

QStringList AppCursor::available() {
  QStringList names = index().keys();
  names.sort();
  return names;
}

AppCursorAttached *AppCursor::qmlAttachedProperties(QObject *object) {
  if (auto *item = qobject_cast<QQuickItem *>(object))
    return new AppCursorAttached(item);
  return nullptr;
}

AppCursorAttached::AppCursorAttached(QQuickItem *item)
    : QObject(item), m_item(item) {
  // The item usually has no window yet while its component is being created, so
  // the first apply() guesses the density from the primary screen. Re-apply once
  // the real window (and its scale factor) is known.
  connect(item, &QQuickItem::windowChanged, this,
          [this](QQuickWindow *) { apply(); });
}

void AppCursorAttached::setName(const QString &name) {
  if (m_name == name)
    return;
  m_name = name;
  apply();
  emit nameChanged();
}

void AppCursorAttached::apply() {
  if (!m_item)
    return;
  if (m_name.isEmpty()) {
    m_item->unsetCursor();
    return;
  }
  qreal ratio = 1.0;
  if (const QQuickWindow *window = m_item->window())
    ratio = window->effectiveDevicePixelRatio();
  else if (const QScreen *screen = QGuiApplication::primaryScreen())
    ratio = screen->devicePixelRatio();
  m_item->setCursor(AppCursor::cursor(m_name, ratio));
}
