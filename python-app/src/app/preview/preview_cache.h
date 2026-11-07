#pragma once

#include <QSize>
#include <QString>
#include <QStringList>

// On-disk cache for generated preview artefacts.
//
// MediaPreviewGenerator kept its own private cachedOutput() helper, so every new
// preview producer had to reimplement key derivation and would happily collide
// with the others. This centralises it: one key scheme (canonical path + mtime +
// size + a per-variant string), one root, and one place to change the eviction
// policy.
//
// The key includes the modification time and size, so re-encoding a file in
// place produces a different key and the stale thumbnail is never shown.
class PreviewCache final {
public:
  // Absolute path a producer should write to. Empty when the source is
  // unreadable or the cache directory cannot be created.
  static QString reserve(const QString &sourcePath, const QString &variant,
                         const QString &extension);

  // Absolute path if a non-empty file is already cached, otherwise empty.
  static QString lookup(const QString &sourcePath, const QString &variant,
                        const QString &extension);

  // file:// URL for a cached artefact, or an empty string.
  static QString lookupUrl(const QString &sourcePath, const QString &variant,
                           const QString &extension);

  static QString toUrl(const QString &absolutePath);

  static QString root();

  // Drops the oldest files once the cache exceeds `maximumBytes`. Preview
  // artefacts are small individually but a long editing session over a large
  // library accumulates thousands of them, and nothing was ever deleting them.
  static qint64 sweep(qint64 maximumBytes);

  static constexpr qint64 kDefaultBudgetBytes = 512LL * 1024 * 1024;
};
