#include "app/media/python_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
QString appDir() { return QCoreApplication::applicationDirPath(); }

// pythonw.exe first (no console window), python.exe as the fallback.
QString bundledInterpreter() {
  const QString base = QDir(appDir()).filePath(QStringLiteral("python"));
  for (const QString &name :
       {QStringLiteral("pythonw.exe"), QStringLiteral("python.exe")}) {
    const QString candidate = QDir(base).filePath(name);
    if (QFileInfo::exists(candidate))
      return QDir::cleanPath(candidate);
  }
  return {};
}

bool isDefaultChoice(const QString &configured) {
  const QString trimmed = configured.trimmed();
  return trimmed.isEmpty() ||
         trimmed.compare(QStringLiteral("python"), Qt::CaseInsensitive) == 0 ||
         trimmed.compare(QStringLiteral("pythonw"), Qt::CaseInsensitive) == 0;
}
} // namespace

QString PythonRuntime::bundledExecutable() { return bundledInterpreter(); }

QString PythonRuntime::systemExecutable(const QString &configured) {
  // An explicit, non-default choice wins.
  if (!isDefaultChoice(configured)) {
    const QString trimmed = configured.trimmed();
    const QString found = QStandardPaths::findExecutable(trimmed);
    if (!found.isEmpty())
      return found;
    if (QFileInfo::exists(trimmed))
      return QFileInfo(trimmed).absoluteFilePath();
    return trimmed;
  }
  // Never the bundle: torch cannot live there. A real Python on PATH, or the
  // bare name so the caller's process-start failure surfaces "no Python".
  const QString found =
      QStandardPaths::findExecutable(QStringLiteral("python"));
  return found.isEmpty() ? QStringLiteral("python") : found;
}

bool PythonRuntime::isBundled() { return !bundledInterpreter().isEmpty(); }

QString PythonRuntime::executable(const QString &configured) {
  // An explicit, non-default choice wins: a user who configured their own
  // interpreter (say one carrying Whisper/torch) means it.
  if (!isDefaultChoice(configured)) {
    const QString trimmed = configured.trimmed();
    const QString found = QStandardPaths::findExecutable(trimmed);
    if (!found.isEmpty())
      return found;
    if (QFileInfo::exists(trimmed))
      return QFileInfo(trimmed).absoluteFilePath();
    return trimmed;
  }

  // No explicit choice: prefer the interpreter shipped beside the app, then a
  // system "python" on PATH.
  const QString bundled = bundledInterpreter();
  if (!bundled.isEmpty())
    return bundled;
  const QString found = QStandardPaths::findExecutable(QStringLiteral("python"));
  return found.isEmpty() ? QStringLiteral("python") : found;
}
