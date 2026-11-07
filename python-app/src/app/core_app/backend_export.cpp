#include "app/core_app/backend.h"
#include "app/export/sequence_export_builder.h"
#include "app/export/export_encoder_policy.h"
#include "app/media/ffmpeg_runtime.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>
#include <new>

Q_DECLARE_LOGGING_CATEGORY(lcExport)

namespace {
QString seconds(qint64 ms) { return QString::number(ms / 1000.0, 'f', 3); }
QString chunkName(int index) {
  return QStringLiteral("chunk-%1.mov").arg(index, 5, 10, QLatin1Char('0'));
}
QString exportFailureMessage(const QByteArray &data) {
  const QStringList lines = QString::fromUtf8(data).split(
      QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
  static const QRegularExpression errorLine(
      QStringLiteral("(error|failed|invalid|unable|not found|no such|"
                     "permission denied|could not|cannot|unrecognized|memory)"),
      QRegularExpression::CaseInsensitiveOption);
  // Retain the specific cause even when FFmpeg ends with "Conversion failed!".
  for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
    const QString line = it->trimmed();
    if (line == QStringLiteral("Conversion failed!"))
      continue;
    if (errorLine.match(line).hasMatch())
      return QStringLiteral("FFmpeg: %1").arg(line.left(700));
  }
  return lines.isEmpty()
             ? QStringLiteral("FFmpeg exited before completing the export.")
             : QStringLiteral("FFmpeg: %1").arg(lines.last().trimmed().left(700));
}
} // namespace

void Backend::prepareExportEncoder() {
  const QString policy = m_exportSettings.value("videoEncoder", "auto").toString().toLower();
  if (!QStringList{"auto", "qsv", "software"}.contains(policy)) {
    failExport(QStringLiteral("Unknown video encoder selection."));
    return;
  }
  if (policy == "software" || ExportEncoderPolicy::hardware(m_exportSettings).isEmpty()) {
    if (policy != "software" && ExportEncoderPolicy::hardware(m_exportSettings).isEmpty())
      m_exportEncoderFallback = QStringLiteral("This format uses software encoding.");
    finishExportPreflight(false, m_exportEncoderFallback);
    return;
  }
  // Always encode the actual output configuration. No cached encoder listing
  // can outlive a runtime/driver update or certify a different resolution.
  auto *probe = new QProcess(this);
  m_exportProbeProcess = probe;
  probe->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
  probe->setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
  connect(probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, probe](int code, QProcess::ExitStatus status) {
    if (m_exportProbeProcess != probe) return;
    const bool success = code == 0 && status == QProcess::NormalExit;
    const QString reason = probe->property("timedOut").toBool()
        ? QStringLiteral("Quick Sync initialization timed out.")
        : success ? QString() : exportFailureMessage(probe->readAllStandardError());
    m_exportProbeProcess = nullptr;
    probe->deleteLater();
    finishExportPreflight(success, reason);
  });
  connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || m_exportProbeProcess != probe) return;
    const QString reason = probe->errorString();
    m_exportProbeProcess = nullptr;
    probe->deleteLater();
    finishExportPreflight(false, reason);
  });
  const int width = m_exportSettings.value("width").toInt();
  const int height = m_exportSettings.value("height").toInt();
  QStringList args{"-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
      QStringLiteral("testsrc2=size=%1x%2:rate=%3")
          .arg(width + width % 2).arg(height + height % 2)
          .arg(m_exportSettings.value("frameRate").toDouble(), 0, 'f', 3),
      "-frames:v", "8", "-an"};
  args << ExportEncoderPolicy::hardwareArguments(m_exportSettings) << "-f" << "null" << "-";
  m_exportStatus = QStringLiteral("Checking Intel Quick Sync");
  probe->start(FfmpegRuntime::executable(), args);
  QTimer::singleShot(15000, probe, [this, probe]() {
    if (m_exportProbeProcess == probe && probe->state() != QProcess::NotRunning) {
      probe->setProperty("timedOut", true);
      probe->kill();
    }
  });
  emit exportStateChanged();
}

void Backend::finishExportPreflight(bool success, const QString &reason) {
  if (!m_exportActive) return;
  if (m_exportCancelled) {
    finishExportProcess(-1, QProcess::CrashExit);
    return;
  }
  const QString hardware = ExportEncoderPolicy::hardware(m_exportSettings);
  if (!success && !hardware.isEmpty() &&
      m_exportSettings.value("videoEncoder", "auto").toString().toLower() == "qsv") {
    failExport(QStringLiteral("Intel Quick Sync is unavailable for these settings: %1. Select Software to retry.").arg(reason));
    return;
  }
  m_exportEncoder = success ? hardware : ExportEncoderPolicy::software(m_exportSettings);
  if (!success) m_exportEncoderFallback = reason;
  m_exportSettings["resolvedVideoEncoder"] = m_exportEncoder;
  m_exportStatus = QStringLiteral("Preparing export");
  emit exportStateChanged();
  if (m_exportActive && !m_exportCancelled) startPreparedExport();
}

void Backend::startPreparedExport() try {
  if (!m_exportChunks.isEmpty()) {
    startNextExportChunk();
    return;
  }
  QString error;
  const auto arguments = SequenceExportBuilder::build(m_exportMedia, m_exportClips,
      m_exportMutedTracks, m_exportTrackStates, m_exportDurationMs,
      m_exportColorSettings, m_exportSettings, m_exportOutputPath, &error);
  if (arguments.isEmpty()) failExport(error);
  else {
    m_exportStatus = QStringLiteral("Exporting");
    startExportProcess(arguments);
  }
} catch (const std::exception &error) {
  failExport(QStringLiteral("Could not prepare export: %1").arg(QString::fromUtf8(error.what())));
} catch (...) {
  failExport(QStringLiteral("Could not prepare export."));
}

void Backend::appendExportLog() {
  if (!m_exportProcess.isOpen()) return;
  m_exportStandardError += m_exportProcess.readAllStandardError();
  if (m_exportStandardError.size() > 65536)
    m_exportStandardError = m_exportStandardError.right(65536);
}

bool Backend::startExportProcess(QStringList arguments) {
  // Export is controlled through QProcess signals/cancellation, never FFmpeg's
  // interactive stdin (which remains an open pipe in a desktop process).
  arguments.prepend(QStringLiteral("-nostdin"));
  m_exportMetrics.reset();
  m_exportStandardError.clear();
  if (!m_exportFilterScriptFile.isEmpty()) {
    QFile::remove(m_exportFilterScriptFile);
    m_exportFilterScriptFile.clear();
  }
  qsizetype commandLength = 0;
  for (const QString &argument : arguments)
    commandLength += argument.size() + 3;
  const int graphIndex = arguments.indexOf(QStringLiteral("-filter_complex"));
  if (graphIndex >= 0 && graphIndex + 1 < arguments.size() &&
      (arguments.at(graphIndex + 1).size() > 8000 || commandLength > 24000)) {
    const QString folder = m_exportWorkDir.isEmpty()
                               ? QFileInfo(m_exportOutputPath).absolutePath()
                               : m_exportWorkDir;
    QTemporaryFile script(QDir(folder).filePath(".cutpro-filter-XXXXXX.txt"));
    const QByteArray graph = arguments.at(graphIndex + 1).toUtf8();
    if (!script.open() || script.write(graph) != graph.size() || !script.flush()) {
      failExport(QStringLiteral("Could not write the export filter script: %1")
                     .arg(script.errorString()));
      return false;
    }
    m_exportFilterScriptFile = script.fileName();
    script.setAutoRemove(false);
    script.close();
    // FFmpeg 9 removed -filter_complex_script; -/ loads an option from a file.
    arguments[graphIndex] = QStringLiteral("-/filter_complex");
    arguments[graphIndex + 1] = m_exportFilterScriptFile;
  }
  m_exportActive = true;
#ifdef Q_OS_WIN
  m_exportProcess.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
  m_exportProcess.setWorkingDirectory(QFileInfo(m_exportOutputPath).absolutePath());
  m_exportProcess.start(FfmpegRuntime::executable(), arguments);
  emit exportStateChanged();
  return m_exportActive;
}

void Backend::startNextExportChunk() try {
  if (!m_exportActive)
    return;
  if (m_exportCancelled) {
    finishExportProcess(-1, QProcess::CrashExit);
    return;
  }
  if (m_exportChunkIndex < m_exportChunks.size()) {
    const auto job = m_exportChunks.at(m_exportChunkIndex).toMap();
    auto settings = m_exportSettings;
    // Compress the video once. PCM intermediates avoid a fresh AAC encoder's
    // priming delay at every cut; master normalization runs over the full mix.
    settings["normalizeLoudness"] = false;
    // FFmpeg 9 can stall draining short A/V windows when the synthetic sources
    // reach EOF together. The output duration/frame cap terminates each job.
    settings["continuousCanvas"] = true;
    const QString path = QDir(m_exportWorkDir).filePath(chunkName(m_exportChunkIndex));
    QString error;
    auto arguments = SequenceExportBuilder::build(
        m_exportMedia, job.value("clips").toList(), m_exportMutedTracks,
        m_exportTrackStates, job.value("durationMs").toLongLong(),
        m_exportColorSettings, settings, path, &error);
    if (arguments.isEmpty()) {
      failExport(error);
      return;
    }
    const int audioCodec = arguments.indexOf(QStringLiteral("-c:a"));
    if (audioCodec >= 0)
      arguments[audioCodec + 1] = QStringLiteral("pcm_f32le");
    arguments.insert(arguments.size() - 1, QStringLiteral("-frames:v"));
    arguments.insert(arguments.size() - 1, QString::number(job.value("frameCount").toLongLong()));
    m_exportStatus = QStringLiteral("Exporting clip %1 of %2")
                         .arg(m_exportChunkIndex + 1).arg(m_exportChunks.size());
    startExportProcess(arguments);
    return;
  }

  QFile concat(QDir(m_exportWorkDir).filePath("chunks.ffconcat"));
  if (!concat.open(QIODevice::WriteOnly)) {
    failExport(QStringLiteral("Could not prepare the export join: %1").arg(concat.errorString()));
    return;
  }
  QByteArray list("ffconcat version 1.0\n");
  for (int i = 0; i < m_exportChunks.size(); ++i) {
    list += "file '" + chunkName(i).toUtf8() + "'\n";
    list += "duration " + seconds(m_exportChunks.at(i).toMap().value("durationMs").toLongLong()).toUtf8() + '\n';
  }
  if (concat.write(list) != list.size() || !concat.flush()) {
    failExport(QStringLiteral("Could not save the export join: %1").arg(concat.errorString()));
    return;
  }
  concat.close();
  QStringList arguments{"-y", "-f", "concat", "-safe", "1", "-i", concat.fileName(),
                        "-map", "0:v:0", "-c:v", "copy"};
  if (m_exportSettings.value("audioEnabled", true).toBool()) {
    arguments << "-map" << "0:a:0" << "-c:a" << "aac" << "-b:a"
              << QString::number(qBound(64, m_exportSettings.value("audioBitrateKbps", 192).toInt(), 512)) + "k"
              << "-ar" << (m_exportSettings.value("audioSampleRate", 48000).toInt() == 44100 ? "44100" : "48000")
              << "-ac" << QString::number(qBound(1, m_exportSettings.value("audioChannels", 2).toInt(), 2));
    if (m_exportSettings.value("normalizeLoudness", false).toBool())
      arguments << "-af" << QStringLiteral("loudnorm=I=%1:LRA=11:TP=-1.5")
          .arg(qBound(-31.0, m_exportSettings.value("loudnessTargetLufs", -14.0).toDouble(), -5.0), 0, 'f', 1);
  } else {
    arguments << "-an";
  }
  arguments << "-t" << seconds(m_exportDurationMs) << "-movflags" << "+faststart"
            << "-progress" << "pipe:1" << m_exportOutputPath;
  m_exportFinalizing = true;
  m_exportEstimate.beginFinalization(m_exportElapsed.elapsed());
  m_exportRemainingSeconds = m_exportEstimate.remainingSeconds();
  m_exportStatus = QStringLiteral("Finalizing export");
  startExportProcess(arguments);
} catch (const std::bad_alloc &) {
  failExport(QStringLiteral("Not enough memory to prepare the next export clip."));
} catch (const std::exception &error) {
  failExport(QStringLiteral("Could not continue the export: %1").arg(QString::fromUtf8(error.what())));
} catch (...) {
  failExport(QStringLiteral("Could not continue the export unexpectedly."));
}

void Backend::finishExportProcess(int code, QProcess::ExitStatus status) {
  if (!m_exportActive)
    return;
  updateExportProgress();
  appendExportLog();
  if (!m_exportActive)
    return; // A progress observer may already have cancelled and completed it.
  if (m_exportCancelled) {
    m_exportElapsedMs = m_exportElapsed.elapsed();
    m_exportActive = false;
    m_exportStatus = QStringLiteral("Export cancelled");
    m_exportRemainingSeconds = -1;
    cleanupExportWork();
    emit exportStateChanged();
    emit exportFinished(false, m_exportOutputPath);
    return;
  }
  if (code != 0 || status != QProcess::NormalExit) {
    const QString cause = exportFailureMessage(m_exportStandardError);
    failExport(m_exportEncoder.endsWith("_qsv")
        ? cause + QStringLiteral(". Quick Sync failed during rendering; select Software and restart the export.")
        : cause);
    return;
  }
  if (!m_exportChunks.isEmpty() && !m_exportFinalizing) {
    const auto job = m_exportChunks.at(m_exportChunkIndex).toMap();
    m_exportEstimate.sample(m_exportElapsed.elapsed(), job.value("startMs").toLongLong() +
        job.value("durationMs").toLongLong());
    ++m_exportChunkIndex;
    QTimer::singleShot(0, this, &Backend::startNextExportChunk);
    return;
  }
  m_exportActive = false;
  m_exportElapsedMs = m_exportElapsed.elapsed();
  if (m_exportFinalizing) m_exportJoinSpeed = m_exportEstimate.completedJoinSpeed(m_exportElapsedMs);
  m_exportProgress = 1.0;
  m_exportRemainingSeconds = 0;
  m_exportStatus = QStringLiteral("Export complete");
  cleanupExportWork();
  emit exportStateChanged();
  emit exportFinished(true, m_exportOutputPath);
}

void Backend::failExport(const QString &error) {
  m_exportElapsedMs = m_exportElapsed.isValid() ? m_exportElapsed.elapsed() : 0;
  m_exportActive = false;
  m_exportStatus = QStringLiteral("Export failed");
  m_exportRemainingSeconds = -1;
  qCWarning(lcExport) << error << m_exportStandardError;
  setError(error.isEmpty() ? QStringLiteral("Could not prepare the export.") : error);
  cleanupExportWork();
  emit exportStateChanged();
  emit exportFinished(false, m_exportOutputPath);
}

void Backend::cleanupExportWork() {
  if (!m_exportFilterScriptFile.isEmpty()) {
    QFile::remove(m_exportFilterScriptFile);
    m_exportFilterScriptFile.clear();
  }
  const QFileInfo work(m_exportWorkDir);
  const QString folder = work.canonicalFilePath();
  const QString parent = QFileInfo(m_exportOutputPath).absoluteDir().canonicalPath();
  if (!m_exportWorkDir.isEmpty() && !folder.isEmpty() && !parent.isEmpty() &&
      !work.isSymLink() && work.fileName().startsWith(".cutpro-export-") &&
      QFileInfo(folder).absoluteDir().canonicalPath() == parent) {
    // Only this export's owned directory is removed, off the GUI thread.
    (void)QtConcurrent::run([folder]() {
      if (!QDir(folder).removeRecursively())
        qCWarning(lcExport) << "Could not remove export working folder" << folder;
    });
  }
  m_exportWorkDir.clear();
  m_exportChunks.clear();
  m_exportMedia.clear();
  m_exportClips.clear();
  m_exportTrackStates.clear();
  m_exportMutedTracks.clear();
  m_exportColorSettings.clear();
  m_exportSettings.clear();
}

void Backend::updateExportProgress() {
  if (!m_exportProcess.isOpen()) return; // cancellation during encoder preflight
  if (!m_exportMetrics.consume(m_exportProcess.readAllStandardOutput()) || !m_exportActive)
    return;
  qint64 doneMs = m_exportMetrics.outTimeMs;
  const qint64 totalMs = qMax<qint64>(1, m_exportDurationMs);
  double progress = double(doneMs) / totalMs;
  if (!m_exportChunks.isEmpty()) {
    if (m_exportFinalizing) {
      progress = 0.97 + 0.03 * qBound(0.0, progress, 1.0);
    } else {
      const auto job = m_exportChunks.at(m_exportChunkIndex).toMap();
      doneMs = job.value("startMs").toLongLong() +
          qMin(doneMs, job.value("durationMs").toLongLong());
      progress = 0.97 * double(doneMs) / totalMs;
    }
  }
  m_exportProgress = qMax(m_exportProgress, qBound(0.0, progress, 0.999));
  m_exportEstimate.sample(m_exportElapsed.elapsed(), doneMs);
  m_exportRemainingSeconds = m_exportEstimate.remainingSeconds();
  emit exportStateChanged();
}
