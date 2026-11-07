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
import re
import subprocess
import sys
import time
from contextlib import redirect_stdout

# A line ends here no matter what: the sentence is over. Khmer khan/bariyoosan and
# the CJK full stops are included because Whisper writes them for those languages
# and they carry exactly the same weight as a period.
SENTENCE_ENDINGS = ".?!…។៕。！？"
# Worth taking only when the line is already long. A comma is a breath, not a stop.
CLAUSE_ENDINGS = ",;:៖、，；："

# How the word stream is cut into subtitle lines. Every value is milliseconds
# except the character counts.
DEFAULT_LIMITS = {
    # The speaker stopped. This is the boundary the whole thing is built on: a
    # gap this long between two words is the end of an utterance, not a breath.
    "pauseMs": 600,
    # A silence this long is never inside one utterance, so it cuts even when the
    # line so far is too short to stand on its own - which is what separates a
    # one-word answer from the question that follows it.
    "hardPauseMs": 1100,
    # Backstops. A line is never longer than this even if nobody ever stops.
    "maxMs": 6200,
    "maxChars": 84,
    # Floors. Below both of these a line is a fragment, and a fragment is glued
    # to its neighbour rather than shown on its own.
    "minMs": 900,
    "minChars": 12,
}


def segmentation_limits(overrides=None):
    limits = dict(DEFAULT_LIMITS)
    for key, environment in (("maxMs", "CUTPRO_SEGMENT_MAX_MS"),
                             ("pauseMs", "CUTPRO_SEGMENT_PAUSE_MS")):
        raw = os.environ.get(environment, "").strip()
        if raw:
            try:
                limits[key] = max(1, int(float(raw)))
            except ValueError:
                pass
    for key, value in (overrides or {}).items():
        if key in limits:
            try:
                limits[key] = max(1, int(float(value)))
            except (TypeError, ValueError):
                pass
    # A hard pause below the ordinary one would make the ordinary one unreachable.
    limits["hardPauseMs"] = max(limits["hardPauseMs"], limits["pauseMs"])
    return limits


def usable_words(raw_segment):
    """The timed words of one Whisper segment, or [] when it has none."""
    words = []
    for word in raw_segment.get("words") or []:
        start = word.get("start")
        end = word.get("end")
        text = str(word.get("word", ""))
        if start is None or end is None or not text.strip():
            continue
        start = float(start)
        end = float(end)
        if end < start:
            continue
        words.append({"start": start, "end": end, "word": text})
    return words


def joined_text(group):
    """The words back into a line, with Whisper's own spacing.

    Concatenated rather than " ".join()-ed: Whisper puts the leading space inside
    each token for space-delimited languages, so this reproduces the original
    text, while Khmer and Chinese - which have no word spaces - stay unspaced
    instead of gaining one between every syllable.
    """
    return re.sub(r"[ \t]+", " ", "".join(item["word"] for item in group)).strip()


def make_piece(group):
    text = joined_text(group)
    if not text:
        return None
    return {
        "start": float(group[0]["start"]),
        "end": float(group[-1]["end"]),
        "text": text,
        # Kept only until pack_word_times() below turns it into the compact
        # "wordTimes" string. Karaoke needs to know when each word was said.
        "_words": list(group),
    }


def pack_word_times(segment):
    """The line's word timings as "start,end start,end" ms from its own start.

    A string, not a list of objects: a multi-hour transcript has tens of
    thousands of lines, and this is a few bytes a word in the project file
    instead of a map per word. Dropped when a word count and a word count of
    text disagree - the caller re-splits nothing, so they always agree here.
    """
    words = segment.pop("_words", None)
    if not words:
        return
    start = float(segment["start"])
    limit = max(1, int(round((float(segment["end"]) - start) * 1000.0)))
    packed = []
    for word in words:
        begin = min(limit, max(0, int(round((word["start"] - start) * 1000.0))))
        finish = min(limit, max(begin, int(round((word["end"] - start) * 1000.0))))
        packed.append("%d,%d" % (begin, finish))
    # One entry per whitespace-separated word is what the app expects, so a token
    # Whisper split inside a word ("do" + "n't") would light up the wrong one.
    if len(packed) == len(segment["text"].split()):
        segment["wordTimes"] = " ".join(packed)


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


def should_cut(group, gap_ms, limits):
    """Whether the line ends between this group's last word and the next one.

    The rules are ordered, and the order is the whole design:

    Whisper's own segment boundaries are not consulted at all. They are attention
    windows, not utterances - it will happily end a segment in the middle of a
    clause with no pause there ("No point burning alive and" / "here with us,"),
    and the old code only ever split those further, so a boundary it invented
    could never be undone. Here the words are one continuous stream and every
    boundary has to be earned by a silence or by punctuation.
    """
    text = joined_text(group)
    if not text:
        return False
    duration_ms = (group[-1]["end"] - group[0]["start"]) * 1000.0
    # A long silence. Cut before the floors are consulted, so a one-word answer
    # is never glued to the line that follows it.
    if gap_ms >= limits["hardPauseMs"]:
        return True
    # Backstops: unreadably long, or wider than the panel. Cut at this word
    # boundary rather than let the line grow.
    if duration_ms >= limits["maxMs"] or len(text) >= limits["maxChars"]:
        return True
    # Too short to stand alone. Keep going, even past a pause: a two-word line
    # costs more to read than a line that ends slightly late.
    if duration_ms < limits["minMs"] and len(text) < limits["minChars"]:
        return False
    if text[-1] in SENTENCE_ENDINGS:
        return True
    # The speaker stopped.
    if gap_ms >= limits["pauseMs"]:
        return True
    # A comma is only a boundary when the line is nearly full anyway.
    if text[-1] in CLAUSE_ENDINGS and len(text) >= limits["maxChars"] * 0.6:
        return True
    return False


def merge_fragments(pieces, limits):
    """Glue any line that is still a fragment onto the one before it.

    The backstop cuts above are length-driven, so they can leave a short tail
    behind ("here with us,"). A fragment is merged back unless a real silence
    separates it, in which case it is a genuine short utterance and stays.
    """
    merged = []
    for piece in pieces:
        if not merged:
            merged.append(piece)
            continue
        previous = merged[-1]
        duration_ms = (piece["end"] - piece["start"]) * 1000.0
        gap_ms = (piece["start"] - previous["end"]) * 1000.0
        fragment = (duration_ms < limits["minMs"]
                    and len(piece["text"]) < limits["minChars"])
        combined_ms = (piece["end"] - previous["start"]) * 1000.0
        if (fragment and gap_ms < limits["hardPauseMs"]
                and combined_ms <= limits["maxMs"] * 1.35
                and len(previous["text"]) + len(piece["text"]) + 1
                <= limits["maxChars"] * 1.25):
            separator = "" if piece["text"][:1] in CLAUSE_ENDINGS else " "
            previous["text"] = (previous["text"] + separator
                                + piece["text"]).strip()
            previous["end"] = piece["end"]
            previous["_words"] = (previous.get("_words") or []) \
                + (piece.get("_words") or [])
            continue
        merged.append(piece)
    return merged


def split_stream(words, limits):
    """One run of timed words -> subtitle lines cut where the speaker stopped."""
    if not words:
        return []
    pieces = []
    group = [words[0]]
    for previous, word in zip(words, words[1:]):
        gap_ms = (word["start"] - previous["end"]) * 1000.0
        if should_cut(group, gap_ms, limits):
            piece = make_piece(group)
            if piece:
                pieces.append(piece)
            group = [word]
        else:
            group.append(word)
    piece = make_piece(group)
    if piece:
        pieces.append(piece)
    return merge_fragments(pieces, limits)


def shape_segments(raw_segments, limits):
    """Whisper's raw output -> one line per utterance.

    Word timings are used here to cut the lines, and then kept in the compact
    form pack_word_times() writes. That much a karaoke caption needs: it has to
    light each word as it is said, and only the transcriber ever knows when that
    was.
    """
    segments = []
    stream = []
    for raw in raw_segments:
        words = usable_words(raw)
        if words:
            stream.extend(words)
            continue
        # No timings on this one, so it cannot be re-cut. Flush what is queued
        # and keep it exactly as Whisper wrote it.
        segments.extend(split_stream(stream, limits))
        stream = []
        text = str(raw.get("text", "")).strip()
        start = float(raw.get("start", 0) or 0)
        end = float(raw.get("end", start) or start)
        if text and end > start:
            segments.append({"start": start, "end": end, "text": text})
    segments.extend(split_stream(stream, limits))

    # Never let one subtitle overlap the next after rounding to milliseconds.
    for index in range(len(segments) - 1):
        next_start = float(segments[index + 1]["start"])
        if segments[index]["end"] > next_start:
            segments[index]["end"] = max(
                segments[index]["start"] + 0.001, next_start
            )
    # After the ends are final, so a clamped line's word timings stay inside it.
    for segment in segments:
        pack_word_times(segment)
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
                               stderr=subprocess.PIPE,
                               creationflags=getattr(subprocess,
                                                     "CREATE_NO_WINDOW", 0))
    if completed.returncode != 0 or not os.path.exists(destination):
        detail = (completed.stderr or b"").decode("utf-8", "replace").strip()
        raise RuntimeError("Could not extract audio for transcription: %s"
                           % (detail or "ffmpeg failed"))
    return destination


def prefetched_window(temp_dir, index, timeout_seconds):
    """Pick up the window audio the Qt side is extracting ahead of this loop.

    The file appears atomically - it is written as .part and renamed - so its
    presence means it is complete. A .failed marker means that extraction did not
    work and this process should do it itself; so does the timeout, which is the
    backstop for the app having gone away mid-run.
    """
    wav = os.path.join(temp_dir, "w%04d.wav" % index)
    failed = os.path.join(temp_dir, "w%04d.failed" % index)
    deadline = time.time() + max(1.0, timeout_seconds)
    while time.time() < deadline:
        if os.path.exists(wav):
            return wav
        if os.path.exists(failed):
            return None
        time.sleep(0.05)
    return None


def run_job(job_path):
    import whisper

    with open(job_path, "r", encoding="utf-8") as handle:
        job = json.load(handle)

    source = job["source"]
    language = job.get("language", "auto")
    ffmpeg = job.get("ffmpeg", "")
    temp_dir = job.get("tempDir") or os.path.dirname(job_path)
    windows = job.get("windows") or []
    prefetch = bool(job.get("prefetch"))
    prefetch_timeout = float(job.get("prefetchTimeoutSeconds", 240))
    limits = segmentation_limits(job.get("segmentation"))
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

        wav = prefetched_window(temp_dir, index, prefetch_timeout) \
            if prefetch else None
        # Owned means this process made the file and this process removes it. A
        # prefetched window belongs to the app, which drops it once the segments
        # for that window have been read.
        owned = wav is None
        if owned:
            wav = os.path.join(temp_dir, "s%04d.wav" % index)
        try:
            if owned:
                extract_window(ffmpeg, source, start_ms - lead_in_ms,
                               length_ms + lead_in_ms, wav)
            options = transcribe_options(detected_language or language)
            if previous_text:
                options["initial_prompt"] = previous_text
            with redirect_stdout(sys.stderr):
                result = model.transcribe(wav, **options)
        finally:
            if owned:
                try:
                    os.remove(wav)
                except OSError:
                    pass

        if not detected_language:
            detected_language = str(result.get("language", "") or "")
        segments = shape_segments(result.get("segments", []), limits)
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
    segments = shape_segments(result.get("segments", []),
                              segmentation_limits())
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
