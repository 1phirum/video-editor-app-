#include "app/core_app/backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

// Persistence for the system-requirements probe (Python runtime, FFmpeg, Edge
// TTS, Whisper, Demucs, MDX-Net).
//
// The Requirements page used to re-probe the machine on every app launch and
// again every time the page was shown - each probe spawns the bundled Python
// interpreter, so the panel sat on "Checking..." on every open. Instead the
// last probe result is cached to a small JSON file and restored at startup, so
// the page shows the saved state immediately and only re-probes when the user
// presses Recheck (or after an in-app install refresh, which re-runs the probe
// and re-persists here). See Backend::checkSystemRequirements() /
// buildSystemRequirements() in backend.cpp.

namespace {
// A per-user cache, not a setting: it is machine state, so it must not ride the
// Settings OK/Cancel/Reset flow. AppDataLocation matches where the app already
// keeps its other writable caches.
QString requirementsCachePath() {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return QDir(dir).filePath(QStringLiteral("requirements-cache.json"));
}
} // namespace

void Backend::loadCachedRequirements() {
  QFile file(requirementsCachePath());
  if (!file.open(QIODevice::ReadOnly))
    return;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isArray())
    return;
  // No signal emit: this runs during construction, before any QML binds, so the
  // first property read already sees the restored list.
  m_systemRequirements = doc.array().toVariantList();
}

void Backend::persistRequirements() const {
  const QString path = requirementsCachePath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;
  const QJsonDocument doc(QJsonArray::fromVariantList(m_systemRequirements));
  file.write(doc.toJson(QJsonDocument::Compact));
  file.commit();
}
