#!/usr/bin/env python3
"""Whisper worker that writes a JSON transcript to stdout.

Two invocation forms:

  whisper_transcribe.py MEDIA [MODEL] [LANGUAGE]
      Transcribes the whole file in one pass and prints one JSON object. This is
      the short-source path and behaves exactly as it always has.

  whisper_transcribe.py --job JOB.json
      Windowed path for long sources. The job file names the source, the model,
      the ffmpeg executable, a scratch directory and a list of windows. Whisper's
      transcribe() is not streaming - it decodes the entire audio track into one
      float32 array and builds the mel spectrogram for all of it before emitting
      a word, so an eight hour file needs 20+ GB and freezes the machine. Here
      each window is extracted to a small 16 kHz mono WAV, transcribed, and
      deleted, so peak memory follows the window length rather than the file
      length. Results are streamed as one JSON object per line:

        {"type": "window",   "index": i, "count": n, "startMs": ...}
        {"type": "segments", "index": i, "segments": [...]}   window-local times
        {"type": "done",     "language": "en"}
        {"error": "..."}                                      on failure

      The model is loaded once for the whole job: reloading it per window would
      cost seconds and gigabytes of churn each time.
"""
import json
import os
import subprocess
import sys
from contextlib import redirect_stdout


def configure_utf8_streams():
    """Keep Windows consoles from failing on Khmer/non-ASCII output."""
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            reconfigure(encoding="utf-8", errors="replace")


def emit(payload):
    """One JSON object per line, flushed so the Qt side sees it immediately."""
    sys.stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
    sys.stdout.flush()


# Whisper emits its tqdm percentage stream when verbose is False. The Qt backend
# parses that stream for the progress dashboard.
# Word timestamps follow the actual spoken words instead of Whisper's coarse
# sentence windows. They preserve natural pauses and give the subtitle/TTS
# pipeline boundaries that match the human voice.
def transcribe_options(language):
    options = {
        "fp16": False,
        "verbose": False,
        "word_timestamps": True,
        "condition_on_previous_text": True,
    }
    if language and language != "auto":
        options["language"] = language
    return options


def split_segments(raw_segments):
    """Whisper's raw segments -> subtitle-sized segments with clean boundaries."""
    segments = []
    for raw in raw_segments:
        text = str(raw.get("text", "")).strip()
        words = raw.get("words") or []
        valid_words = [
            word for word in words
            if word.get("start") is not None
            and word.get("end") is not None
            and float(word.get("end", 0)) > float(word.get("start", 0))
        ]
        if len(valid_words) >= 2:
            # Split long Whisper windows at genuine pauses. This keeps a
            # translated/TTS phrase from running through silence or into
            # the next spoken phrase.
            group = [valid_words[0]]
            for word in valid_words[1:]:
                gap = float(word["start"]) - float(group[-1]["end"])
                if gap >= 0.55:
                    group_text = " ".join(
                        str(item.get("word", "")).strip() for item in group
                    ).strip()
                    if group_text:
                        segments.append({
                            "start": float(group[0]["start"]),
                            "end": float(group[-1]["end"]),
                            "text": group_text,
                        })
                    group = [word]
                else:
                    group.append(word)
            group_text = " ".join(
                str(item.get("word", "")).strip() for item in group
            ).strip()
            if group_text:
                segments.append({
                    "start": float(group[0]["start"]),
                    "end": float(group[-1]["end"]),
                    "text": group_text,
                })
            continue
        elif valid_words:
            start = float(valid_words[0]["start"])
            end = float(valid_words[-1]["end"])
        else:
            start = float(raw.get("start", 0) or 0)
            end = float(raw.get("end", start) or start)
        if text and end > start:
            segments.append({"start": start, "end": end, "text": text})

    # Keep a real pause between adjacent speech chunks. Whisper can place
    # neighbouring coarse segments on the same boundary; never let one
    # subtitle overlap the next after rounding to milliseconds.
    for index in range(len(segments) - 1):
        next_start = float(segments[index + 1]["start"])
        if segments[index]["end"] > next_start:
            segments[index]["end"] = max(
                segments[index]["start"] + 0.001, next_start
            )
    return segments


def extract_window(ffmpeg, source, start_ms, length_ms, destination):
    """Decode one window to 16 kHz mono PCM - the only format Whisper wants.

    -ss before -i so the container seek is cheap on a multi-gigabyte source, and
    -vn so the video stream is never decoded at all.
    """
    command = [
        ffmpeg or "ffmpeg",
        "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-ss", "%.3f" % (max(0, start_ms) / 1000.0),
        "-t", "%.3f" % (max(1, length_ms) / 1000.0),
        "-i", source,
        "-map", "0:a:0", "-vn", "-sn",
        "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le",
        "-f", "wav", destination,
    ]
    completed = subprocess.run(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE)
    if completed.returncode != 0 or not os.path.exists(destination):
        detail = (completed.stderr or b"").decode("utf-8", "replace").strip()
        raise RuntimeError("Could not extract audio for transcription: %s"
                           % (detail or "ffmpeg failed"))
    return destination


def run_job(job_path):
    import whisper

    with open(job_path, "r", encoding="utf-8") as handle:
        job = json.load(handle)

    source = job["source"]
    language = job.get("language", "auto")
    ffmpeg = job.get("ffmpeg", "")
    temp_dir = job.get("tempDir") or os.path.dirname(job_path)
    windows = job.get("windows") or []
    os.makedirs(temp_dir, exist_ok=True)

    # Loaded once for the whole job, outside the window loop.
    with redirect_stdout(sys.stderr):
        model = whisper.load_model(job.get("model", "small"))

    detected_language = language if language != "auto" else ""
    # Carried into the next window so Whisper keeps its context across the seam.
    previous_text = ""
    for index, window in enumerate(windows):
        start_ms = int(window.get("startMs", 0))
        length_ms = int(window.get("lengthMs", 0))
        lead_in_ms = int(window.get("leadInMs", 0))
        emit({"type": "window", "index": index, "count": len(windows),
              "startMs": start_ms})

        wav = os.path.join(temp_dir, "w%04d.wav" % index)
        try:
            extract_window(ffmpeg, source, start_ms - lead_in_ms,
                           length_ms + lead_in_ms, wav)
            options = transcribe_options(detected_language or language)
            if previous_text:
                options["initial_prompt"] = previous_text
            with redirect_stdout(sys.stderr):
                result = model.transcribe(wav, **options)
        finally:
            # The scratch file dies with its window; only one is ever on disk.
            try:
                os.remove(wav)
            except OSError:
                pass

        if not detected_language:
            detected_language = str(result.get("language", "") or "")
        segments = split_segments(result.get("segments", []))
        # The tail is a prompt, not a transcript: a few hundred characters is
        # enough context and keeps the prompt well inside Whisper's budget.
        window_text = str(result.get("text", "") or "").strip()
        previous_text = window_text[-400:] if window_text else previous_text
        emit({"type": "segments", "index": index, "startMs": start_ms,
              "segments": segments})

    emit({"type": "done", "language": detected_language or language})
    return 0


def run_single(argv):
    import whisper

    model_name = argv[2] if len(argv) > 2 else os.environ.get(
        "CUTPRO_WHISPER_MODEL", "small")
    language = argv[3] if len(argv) > 3 else "auto"
    with redirect_stdout(sys.stderr):
        result = whisper.load_model(model_name).transcribe(
            argv[1], **transcribe_options(language))
    segments = split_segments(result.get("segments", []))
    emit({"text": result.get("text", ""), "segments": segments,
          "language": result.get("language", language)})
    return 0


def main():
    configure_utf8_streams()
    if len(sys.argv) < 2:
        emit({"error": "usage: whisper_transcribe.py MEDIA [MODEL] [LANGUAGE]"
                       " | --job JOB.json"})
        return 2
    try:
        if sys.argv[1] == "--job":
            if len(sys.argv) < 3:
                emit({"error": "usage: whisper_transcribe.py --job JOB.json"})
                return 2
            return run_job(sys.argv[2])
        return run_single(sys.argv)
    except Exception as error:
        emit({"error": str(error)})
        return 4


if __name__ == "__main__":
    raise SystemExit(main())
