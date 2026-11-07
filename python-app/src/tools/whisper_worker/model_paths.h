#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// The single source of truth mapping the UI model id
// (TranscriptionSettingsPage.qml: tiny/base/small/medium/large-v3/turbo) to the
// GGML weight file whisper.cpp loads. Shared by the native worker (to resolve
// the file), the backend (to build the download URL and probe what is
// installed) and the settings probe, so all agree on one filename per model.
//
// The files download from
// https://huggingface.co/ggerganov/whisper.cpp/resolve/main/<fileName>.
namespace WhisperModels {

// The GGML file name for a UI model id, or an empty string for an unknown id.
inline QString ggmlFileName(const QString &modelId) {
  if (modelId == QStringLiteral("tiny"))
    return QStringLiteral("ggml-tiny.bin");
  if (modelId == QStringLiteral("base"))
    return QStringLiteral("ggml-base.bin");
  if (modelId == QStringLiteral("small"))
    return QStringLiteral("ggml-small.bin");
  if (modelId == QStringLiteral("medium"))
    return QStringLiteral("ggml-medium.bin");
  if (modelId == QStringLiteral("large-v3"))
    return QStringLiteral("ggml-large-v3.bin");
  if (modelId == QStringLiteral("turbo"))
    return QStringLiteral("ggml-large-v3-turbo.bin");
  return QString();
}

// Legacy location: models/whisper next to cutpro.exe. Pre-fix builds downloaded
// here, and a dev/portable tree (writable exe dir) still works this way, so it
// stays as a read-only fallback when locating an already-downloaded file.
inline QString modelsSubdir() { return QStringLiteral("models/whisper"); }
inline QString besideExeDir() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(modelsSubdir());
}

// The writable models directory: %LOCALAPPDATA%\Cut Pro\models\whisper on
// Windows. A per-machine install lives in Program Files, which the non-elevated
// app cannot write, so downloads must land in the user profile instead.
// GenericDataLocation does not depend on the application name, so cutpro.exe and
// whisper_worker.exe (same user) always resolve to the identical folder.
inline QString writableModelsDir() {
  const QString base =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  return QDir(base).filePath(QStringLiteral("Cut Pro/models/whisper"));
}

// Where downloads are written: the CUTPRO_WHISPER_CACHE override if set, else
// the writable per-user directory.
inline QString downloadDir() {
  const QString explicitRoot =
      qEnvironmentVariable("CUTPRO_WHISPER_CACHE").trimmed();
  return explicitRoot.isEmpty() ? writableModelsDir() : explicitRoot;
}

// Ordered roots searched to locate an already-downloaded weight file:
// the override, the writable dir, then the legacy beside-exe dir. De-duplicated.
inline QStringList searchRoots() {
  QStringList roots;
  const QString explicitRoot =
      qEnvironmentVariable("CUTPRO_WHISPER_CACHE").trimmed();
  for (const QString &candidate :
       {explicitRoot, writableModelsDir(), besideExeDir()}) {
    if (candidate.isEmpty())
      continue;
    const QString clean = QDir::cleanPath(QDir(candidate).absolutePath());
    if (!clean.isEmpty() && !roots.contains(clean))
      roots.append(clean);
  }
  return roots;
}

// Absolute path to a model file if it exists in any search root, else empty.
inline QString existingFilePath(const QString &fileName) {
  if (fileName.isEmpty())
    return QString();
  for (const QString &root : searchRoots()) {
    const QString path = QDir(root).filePath(fileName);
    if (QFileInfo::exists(path))
      return path;
  }
  return QString();
}

} // namespace WhisperModels
