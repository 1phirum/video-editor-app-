// cutpro_whisper.exe - the native offline transcription worker.
//
// A drop-in replacement for tools/whisper_transcribe.py: it speaks the exact
// same line-delimited-JSON protocol on stdout, so the app's prefetcher,
// windowing, job.json, consume/handle, cancel/resume and mergeWindow are all
// unchanged. The difference is that it needs no Python and no pip - whisper.cpp
// (ggml) is linked in, and the only external file is the downloaded GGML model.
//
// Two invocation forms, matching the Python worker:
//   cutpro_whisper.exe MEDIA [MODEL] [LANGUAGE]   single pass, one JSON reply
//   cutpro_whisper.exe --job JOB.json             windowed, one JSON per line
//
// Progress is printed as "NN%" to stderr, because the backend parses the
// worker's stderr for the progress dashboard (same as Whisper's tqdm bar).
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariantMap>

#include <cstdio>
#include <vector>

#include "tools/whisper_worker/model_paths.h"
#include "tools/whisper_worker/segmenter.h"
#include "tools/whisper_worker/wav_reader.h"

#include "whisper.h"

namespace {

// One JSON object per line on stdout, flushed so the Qt side sees it at once.
// Compact, ASCII-unescaped: the app re-parses with QJsonDocument, so only the
// field names and units have to match the Python worker, not the byte layout.
void emitJson(const QJsonObject &object) {
  const QByteArray line =
      QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
  std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
  std::fflush(stdout);
}

void emitError(const QString &message) {
  QJsonObject object;
  object.insert(QStringLiteral("error"), message);
  emitJson(object);
}

// True for scripts that do not put spaces between words (CJK, Japanese kana,
// Khmer, Thai). Whisper's space-delimited tokens start a new word with a leading
// space; for these scripts every token is its own word instead. This mirrors the
// word grouping openai-whisper's word_timestamps produced, so the segmenter cuts
// lines the same way whichever engine ran.
bool isSpacelessScript(QChar ch) {
  const char32_t c = ch.unicode();
  return (c >= 0x0E00 && c <= 0x0E7F) || // Thai
         (c >= 0x1780 && c <= 0x17FF) || // Khmer
         (c >= 0x3000 && c <= 0x30FF) || // CJK punctuation, Hiragana, Katakana
         (c >= 0x3400 && c <= 0x4DBF) || // CJK Extension A
         (c >= 0x4E00 && c <= 0x9FFF) || // CJK Unified Ideographs
         (c >= 0xF900 && c <= 0xFAFF) || // CJK Compatibility Ideographs
         (c >= 0xFF00 && c <= 0xFFEF);   // Halfwidth and Fullwidth Forms
}

QChar firstNonSpace(const QString &text) {
  for (const QChar ch : text) {
    if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
      return ch;
  }
  return QChar();
}

// The ffmpeg the worker extracts audio with when it has to do so itself. The
// windowed path is handed one in the job; the single-pass path is not, so it
// resolves the bundled ffmpeg the same way FfmpegRuntime does - beside the exe
// under ffmpeg/bin, then the dev-tree sibling, then $CUTPRO_FFMPEG_ROOT, then
// whatever is on PATH.
QString resolveFfmpeg() {
  const QString appDir = QCoreApplication::applicationDirPath();
  const QStringList roots{
      QDir(appDir).filePath(QStringLiteral("ffmpeg")),
      QDir(appDir).filePath(
          QStringLiteral("../ffmpeg-9.0.1-full_build-shared")),
      qEnvironmentVariable("CUTPRO_FFMPEG_ROOT").trimmed()};
  for (const QString &root : roots) {
    if (root.isEmpty())
      continue;
    const QString candidate =
        QDir(root).filePath(QStringLiteral("bin/ffmpeg.exe"));
    if (QFileInfo::exists(candidate))
      return QDir::cleanPath(candidate);
  }
  const QString onPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  return onPath.isEmpty() ? QStringLiteral("ffmpeg") : onPath;
}

// Decode one span of the source to the fixed 16 kHz mono PCM s16le WAV whisper
// wants. Byte for byte the command window_audio_prefetcher.cpp and the Python
// worker build: -ss before -i for a cheap container seek, -vn so the video is
// never touched. `startMs`/`lengthMs` are the audio span (already lead-in
// adjusted by the caller); a negative `lengthMs` means "to the end of the
// source" (the single-pass whole-file case) and omits -t.
bool extractAudio(const QString &ffmpeg, const QString &source, qint64 startMs,
                  qint64 lengthMs, const QString &destination, QString *error) {
  QStringList arguments{
      QStringLiteral("-nostdin"),
      QStringLiteral("-hide_banner"),
      QStringLiteral("-loglevel"),
      QStringLiteral("error"),
      QStringLiteral("-y"),
      QStringLiteral("-ss"),
      QString::asprintf("%.3f", qMax<qint64>(0, startMs) / 1000.0)};
  if (lengthMs >= 0)
    arguments << QStringLiteral("-t")
              << QString::asprintf("%.3f", qMax<qint64>(1, lengthMs) / 1000.0);
  arguments << QStringLiteral("-i") << source << QStringLiteral("-map")
            << QStringLiteral("0:a:0") << QStringLiteral("-vn")
            << QStringLiteral("-sn") << QStringLiteral("-ac")
            << QStringLiteral("1") << QStringLiteral("-ar")
            << QStringLiteral("16000") << QStringLiteral("-c:a")
            << QStringLiteral("pcm_s16le") << QStringLiteral("-f")
            << QStringLiteral("wav") << destination;
  QProcess process;
  process.start(ffmpeg.isEmpty() ? QStringLiteral("ffmpeg") : ffmpeg, arguments);
  if (!process.waitForStarted(-1)) {
    *error = QStringLiteral("Could not start ffmpeg to extract audio for "
                            "transcription.");
    return false;
  }
  process.waitForFinished(-1);
  if (process.exitStatus() != QProcess::NormalExit ||
      process.exitCode() != 0 || !QFileInfo::exists(destination)) {
    const QString detail =
        QString::fromUtf8(process.readAllStandardError()).trimmed();
    *error = QStringLiteral("Could not extract audio for transcription: %1")
                 .arg(detail.isEmpty() ? QStringLiteral("ffmpeg failed")
                                       : detail);
    return false;
  }
  return true;
}

// Resolve the GGML weight file for a UI model id. Searches the writable
// per-user models dir (where downloads now land) and the legacy beside-exe dir.
// A missing file is the ordinary state before the user has downloaded one, so
// the message points them at where the download lives.
whisper_context *loadModel(const QString &modelId, QString *error) {
  const QString fileName = WhisperModels::ggmlFileName(modelId);
  if (fileName.isEmpty()) {
    *error = QStringLiteral("Unknown transcription model '%1'.").arg(modelId);
    return nullptr;
  }
  const QString path = WhisperModels::existingFilePath(fileName);
  if (path.isEmpty()) {
    *error = QStringLiteral("Whisper model '%1' is not installed. Open Settings "
                            "> Requirements to download it.")
                 .arg(modelId);
    return nullptr;
  }
  whisper_context_params cparams = whisper_context_default_params();
  cparams.use_gpu = false; // CPU-only build; keep it explicit.
  whisper_context *ctx = whisper_init_from_file_with_params(
      QDir::toNativeSeparators(path).toLocal8Bit().constData(), cparams);
  if (!ctx) {
    *error =
        QStringLiteral("Failed to load the Whisper model file at %1.").arg(path);
    return nullptr;
  }
  return ctx;
}

// whisper_full progress is 0..100. The backend reads the last "NN%" it sees on
// stderr, and resets per window, so emitting the raw percentage each time it
// changes drives the dashboard exactly as Whisper's tqdm bar did.
struct ProgressState {
  int last = -1;
};
void progressCallback(whisper_context *, whisper_state *, int progress,
                      void *userData) {
  auto *state = static_cast<ProgressState *>(userData);
  const int clamped = qBound(0, progress, 100);
  if (state && clamped == state->last)
    return;
  if (state)
    state->last = clamped;
  std::fprintf(stderr, "%d%%\n", clamped);
  std::fflush(stderr);
}

// Run whisper on one buffer of samples and reconstruct its word stream.
//
// Each whisper segment becomes one Segmenter::RawSegment: its tokens are grouped
// into words (a new word begins on a leading space or a spaceless-script token,
// mirroring openai-whisper's word_timestamps), and a segment that yields no
// usable word survives on its raw text - exactly the wordless-fallback the
// Python shape_segments relied on. Times stay window-local seconds; the app
// shifts them into source time.
bool transcribe(whisper_context *ctx, const std::vector<float> &samples,
                const QString &language, const QString &initialPrompt,
                QVector<Segmenter::RawSegment> *rawSegments,
                QString *detectedLanguage, QString *fullText, QString *error) {
  whisper_full_params params =
      whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.print_realtime = false;
  params.print_progress = false;
  params.print_timestamps = false;
  params.print_special = false;
  params.translate = false;
  params.no_context = false;   // condition_on_previous_text, within one call
  params.single_segment = false;
  params.token_timestamps = true; // per-token t0/t1 for the word stream
  const int cores = QThread::idealThreadCount();
  params.n_threads = cores > 0 ? cores : 4;

  ProgressState progress;
  params.progress_callback = &progressCallback;
  params.progress_callback_user_data = &progress;

  QByteArray languageBytes;
  const bool autoDetect = language.isEmpty() ||
                          language == QStringLiteral("auto");
  if (autoDetect) {
    // "auto" makes whisper detect the language AND transcribe. detect_language
    // must stay false: when true, whisper_full detects the language and returns
    // immediately with no segments - which is exactly the "done instantly, no
    // transcript" bug this avoids.
    params.language = "auto";
    params.detect_language = false;
  } else {
    languageBytes = language.toUtf8();
    params.language = languageBytes.constData();
    params.detect_language = false;
  }

  QByteArray promptBytes;
  if (!initialPrompt.isEmpty()) {
    promptBytes = initialPrompt.toUtf8();
    params.initial_prompt = promptBytes.constData();
  }

  if (samples.empty()) {
    // Nothing to decode (a silent or empty window). Not an error: emit no
    // segments and let the detected language fall through unchanged.
    if (detectedLanguage->isEmpty() && !autoDetect)
      *detectedLanguage = language;
    return true;
  }

  if (whisper_full(ctx, params, samples.data(), int(samples.size())) != 0) {
    *error = QStringLiteral("Whisper failed to process the audio.");
    return false;
  }

  if (detectedLanguage->isEmpty()) {
    const int langId = whisper_full_lang_id(ctx);
    if (langId >= 0) {
      const char *name = whisper_lang_str(langId);
      if (name)
        *detectedLanguage = QString::fromUtf8(name);
    }
    if (detectedLanguage->isEmpty() && !autoDetect)
      *detectedLanguage = language;
  }

  const whisper_token eot = whisper_token_eot(ctx);
  const int segmentCount = whisper_full_n_segments(ctx);
  for (int s = 0; s < segmentCount; ++s) {
    const QString segmentText =
        QString::fromUtf8(whisper_full_get_segment_text(ctx, s));
    fullText->append(segmentText);

    Segmenter::RawSegment raw;
    raw.text = segmentText.trimmed();
    raw.start = whisper_full_get_segment_t0(ctx, s) / 100.0;
    raw.end = whisper_full_get_segment_t1(ctx, s) / 100.0;

    Segmenter::Word current;
    bool haveCurrent = false;
    const auto flushWord = [&]() {
      if (haveCurrent && !current.text.trimmed().isEmpty() &&
          current.end >= current.start)
        raw.words.append(current);
      haveCurrent = false;
    };
    const int tokenCount = whisper_full_n_tokens(ctx, s);

    // Whisper emits CJK (and other byte-fallback) characters as several tokens,
    // each holding only a fragment of the character's UTF-8 bytes. Decoding a
    // fragment on its own yields U+FFFD, so first stitch the raw token bytes
    // back into whole characters - carrying each character's time span - and
    // only then group characters into words.
    struct TimedChar {
      QString text;
      double start;
      double end;
    };
    QVector<TimedChar> chars;
    {
      QByteArray charBytes;
      int need = 1;
      double charStart = 0.0;
      double charEnd = 0.0;
      const auto leadLength = [](unsigned char b) -> int {
        if (b < 0x80)
          return 1;
        if ((b >> 5) == 0x6)
          return 2; // 110xxxxx
        if ((b >> 4) == 0xE)
          return 3; // 1110xxxx
        if ((b >> 3) == 0x1E)
          return 4; // 11110xxx
        return 1;   // stray continuation byte: emit it alone
      };
      const auto flushChar = [&]() {
        if (charBytes.isEmpty())
          return;
        chars.append({QString::fromUtf8(charBytes), charStart, charEnd});
        charBytes.clear();
      };
      for (int t = 0; t < tokenCount; ++t) {
        if (whisper_full_get_token_id(ctx, s, t) >= eot)
          continue; // eot and the timestamp tokens carry no text
        const char *raw = whisper_full_get_token_text(ctx, s, t);
        if (!raw || !*raw)
          continue;
        const whisper_token_data data = whisper_full_get_token_data(ctx, s, t);
        const double tokenStart = data.t0 / 100.0;
        const double tokenEnd = data.t1 / 100.0;
        for (const char *p = raw; *p; ++p) {
          const unsigned char b = static_cast<unsigned char>(*p);
          if (charBytes.isEmpty()) {
            need = leadLength(b);
            charStart = tokenStart;
          }
          charBytes.append(char(b));
          charEnd = tokenEnd;
          if (charBytes.size() >= need)
            flushChar();
        }
      }
      flushChar(); // any trailing incomplete sequence
    }

    for (const TimedChar &ch : chars) {
      const QChar lead = firstNonSpace(ch.text);
      const bool startsNew =
          !haveCurrent || ch.text.startsWith(QLatin1Char(' ')) ||
          (!lead.isNull() && isSpacelessScript(lead));
      if (startsNew) {
        flushWord();
        current = Segmenter::Word();
        current.text = ch.text;
        current.start = ch.start;
        current.end = ch.end;
        haveCurrent = true;
      } else {
        current.text += ch.text;
        current.end = ch.end;
      }
    }
    flushWord();
    rawSegments->append(raw);
  }
  return true;
}

// Pick up the window audio the app is extracting ahead of us. The file is
// written as .part and renamed, so its presence means it is complete. A .failed
// marker (or the timeout, the backstop for the app having gone away) means this
// process extracts the window itself.
QString waitForPrefetched(const QString &tempDir, int index,
                          double timeoutSeconds) {
  const QString wav =
      QDir(tempDir).filePath(QString::asprintf("w%04d.wav", index));
  const QString failed =
      QDir(tempDir).filePath(QString::asprintf("w%04d.failed", index));
  QElapsedTimer timer;
  timer.start();
  const qint64 deadlineMs = qint64(qMax(1.0, timeoutSeconds) * 1000.0);
  while (timer.elapsed() < deadlineMs) {
    if (QFileInfo::exists(wav))
      return wav;
    if (QFileInfo::exists(failed))
      return QString();
    QThread::msleep(50);
  }
  return QString();
}

int runJob(const QString &jobPath) {
  QFile handle(jobPath);
  if (!handle.open(QIODevice::ReadOnly)) {
    emitError(QStringLiteral("Could not open the transcription job file."));
    return 4;
  }
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(handle.readAll(), &parseError);
  handle.close();
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    emitError(QStringLiteral("The transcription job file is not valid JSON."));
    return 4;
  }
  const QJsonObject job = document.object();

  const QString source = job.value(QStringLiteral("source")).toString();
  const QString modelId =
      job.value(QStringLiteral("model")).toString(QStringLiteral("small"));
  const QString language =
      job.value(QStringLiteral("language")).toString(QStringLiteral("auto"));
  QString ffmpeg = job.value(QStringLiteral("ffmpeg")).toString();
  if (ffmpeg.isEmpty())
    ffmpeg = resolveFfmpeg();
  QString tempDir = job.value(QStringLiteral("tempDir")).toString();
  if (tempDir.isEmpty())
    tempDir = QFileInfo(jobPath).absolutePath();
  const bool prefetch = job.value(QStringLiteral("prefetch")).toBool();
  const double prefetchTimeout =
      job.value(QStringLiteral("prefetchTimeoutSeconds")).toDouble(240.0);

  qint64 jobMaxMs = -1;
  const QJsonValue segmentation = job.value(QStringLiteral("segmentation"));
  if (segmentation.isObject()) {
    const QJsonValue maxMs =
        segmentation.toObject().value(QStringLiteral("maxMs"));
    if (maxMs.isDouble())
      jobMaxMs = qint64(maxMs.toDouble());
  }
  const Segmenter::Limits limits =
      Segmenter::limitsFromEnvironment(jobMaxMs, -1);

  const QJsonArray windows = job.value(QStringLiteral("windows")).toArray();
  QDir().mkpath(tempDir);

  QString modelError;
  whisper_context *ctx = loadModel(modelId, &modelError);
  if (!ctx) {
    emitError(modelError);
    return 4;
  }

  QString detectedLanguage =
      (language != QStringLiteral("auto")) ? language : QString();
  QString previousText; // carried across windows as the next initial_prompt
  const int count = windows.size();
  int status = 0;
  for (int index = 0; index < count; ++index) {
    const QJsonObject window = windows.at(index).toObject();
    const qint64 startMs =
        qint64(window.value(QStringLiteral("startMs")).toDouble());
    const qint64 lengthMs =
        qint64(window.value(QStringLiteral("lengthMs")).toDouble());
    const qint64 leadInMs =
        qint64(window.value(QStringLiteral("leadInMs")).toDouble());

    QJsonObject windowEvent;
    windowEvent.insert(QStringLiteral("type"), QStringLiteral("window"));
    windowEvent.insert(QStringLiteral("index"), index);
    windowEvent.insert(QStringLiteral("count"), count);
    windowEvent.insert(QStringLiteral("startMs"), double(startMs));
    emitJson(windowEvent);

    QString wav =
        prefetch ? waitForPrefetched(tempDir, index, prefetchTimeout) : QString();
    // Owned means this process made the file and removes it. A prefetched window
    // belongs to the app, which drops it once its segments have been read.
    const bool owned = wav.isEmpty();
    QString ownedPath;
    if (owned) {
      ownedPath = QDir(tempDir).filePath(QString::asprintf("s%04d.wav", index));
      QString extractError;
      if (!extractAudio(ffmpeg, source, startMs - leadInMs, lengthMs + leadInMs,
                        ownedPath, &extractError)) {
        emitError(extractError);
        status = 4;
        break;
      }
      wav = ownedPath;
    }

    const WavReader::Audio audio = WavReader::load(wav);
    if (owned)
      QFile::remove(ownedPath);
    if (!audio.ok) {
      emitError(audio.error.isEmpty()
                    ? QStringLiteral("Could not read the window audio.")
                    : audio.error);
      status = 4;
      break;
    }

    QVector<Segmenter::RawSegment> rawSegments;
    QString fullText;
    QString transcribeError;
    const QString requestLanguage =
        detectedLanguage.isEmpty() ? language : detectedLanguage;
    if (!transcribe(ctx, audio.samples, requestLanguage, previousText,
                    &rawSegments, &detectedLanguage, &fullText,
                    &transcribeError)) {
      emitError(transcribeError);
      status = 4;
      break;
    }

    const QString windowText = fullText.trimmed();
    if (!windowText.isEmpty())
      previousText = windowText.right(400);

    QJsonObject segmentsEvent;
    segmentsEvent.insert(QStringLiteral("type"), QStringLiteral("segments"));
    segmentsEvent.insert(QStringLiteral("index"), index);
    segmentsEvent.insert(QStringLiteral("startMs"), double(startMs));
    segmentsEvent.insert(
        QStringLiteral("segments"),
        QJsonArray::fromVariantList(Segmenter::shapeSegments(rawSegments, limits)));
    emitJson(segmentsEvent);
  }

  if (status == 0) {
    QJsonObject done;
    done.insert(QStringLiteral("type"), QStringLiteral("done"));
    done.insert(QStringLiteral("language"),
                detectedLanguage.isEmpty() ? language : detectedLanguage);
    emitJson(done);
  }
  whisper_free(ctx);
  return status;
}

int runSingle(const QString &sourcePath, const QString &modelId,
              const QString &language) {
  QString modelError;
  whisper_context *ctx =
      loadModel(modelId.isEmpty() ? QStringLiteral("small") : modelId,
                &modelError);
  if (!ctx) {
    emitError(modelError);
    return 4;
  }

  // whisper.cpp does not decode media files, so extract the whole clip to one
  // 16 kHz mono WAV first. Single pass is only taken for short sources, so this
  // is a small temporary file.
  const QString tempDir = QDir::tempPath();
  const QString wav =
      QDir(tempDir).filePath(QStringLiteral("cutpro_whisper_%1.wav")
                                 .arg(QCoreApplication::applicationPid()));
  QString extractError;
  if (!extractAudio(resolveFfmpeg(), sourcePath, 0, /*whole file*/ -1, wav,
                    &extractError)) {
    whisper_free(ctx);
    emitError(extractError);
    return 4;
  }

  const WavReader::Audio audio = WavReader::load(wav);
  QFile::remove(wav);
  if (!audio.ok) {
    whisper_free(ctx);
    emitError(audio.error.isEmpty()
                  ? QStringLiteral("Could not read the extracted audio.")
                  : audio.error);
    return 4;
  }

  QVector<Segmenter::RawSegment> rawSegments;
  QString detectedLanguage =
      (language.isEmpty() || language == QStringLiteral("auto")) ? QString()
                                                                 : language;
  QString fullText;
  QString transcribeError;
  if (!transcribe(ctx, audio.samples, language, QString(), &rawSegments,
                  &detectedLanguage, &fullText, &transcribeError)) {
    whisper_free(ctx);
    emitError(transcribeError);
    return 4;
  }
  whisper_free(ctx);

  QJsonObject reply;
  reply.insert(QStringLiteral("text"), fullText);
  reply.insert(QStringLiteral("segments"),
               QJsonArray::fromVariantList(Segmenter::shapeSegments(
                   rawSegments, Segmenter::limitsFromEnvironment())));
  reply.insert(QStringLiteral("language"),
               detectedLanguage.isEmpty()
                   ? (language.isEmpty() ? QStringLiteral("auto") : language)
                   : detectedLanguage);
  emitJson(reply);
  return 0;
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  // whisper.cpp/ggml log their model-load and system-info chatter to stderr by
  // default. The backend parses this worker's stderr for "NN%" progress, so keep
  // that channel to progress alone and drop the library's own logging; real
  // failures are still reported as an {"error": ...} object on stdout.
  whisper_log_set([](ggml_log_level, const char *, void *) {}, nullptr);
  const QStringList args = app.arguments();
  if (args.size() < 2) {
    emitError(QStringLiteral("usage: cutpro_whisper MEDIA [MODEL] [LANGUAGE] | "
                             "--job JOB.json"));
    return 2;
  }
  if (args.at(1) == QStringLiteral("--job")) {
    if (args.size() < 3) {
      emitError(QStringLiteral("usage: cutpro_whisper --job JOB.json"));
      return 2;
    }
    return runJob(args.at(2));
  }
  const QString model = args.size() > 2 ? args.at(2) : QStringLiteral("small");
  const QString language = args.size() > 3 ? args.at(3) : QStringLiteral("auto");
  return runSingle(args.at(1), model, language);
}
