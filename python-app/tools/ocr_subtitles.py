#!/usr/bin/env python3
"""Read burned-in subtitles off the video with local OCR, writing a JSON
transcript to stdout in the exact shape the Whisper and Gemini workers use, so
the Qt side needs no engine-specific handling.

Two invocation forms:

  ocr_subtitles.py MEDIA [MODEL] [LANGUAGE]
      Reads the whole file in one pass and prints one JSON object. Short-source
      path, same as the other workers.

  ocr_subtitles.py --job JOB.json
      Windowed path for long sources. The job names the source, the ffmpeg
      executable, a scratch directory, an optional "ocr" config block and a list
      of windows. Results stream as one JSON object per line:

        {"type": "window",   "index": i, "count": n, "startMs": ...}
        {"type": "segments", "index": i, "segments": [...]}   window-local times
        {"type": "done",     "language": "en"}
        {"error": "..."}                                      on failure

Unlike the audio workers this reads the *picture*, not the sound: it samples
frames a few times a second, crops the subtitle band at the bottom, and only
runs OCR when that band visibly changes - so the cost is roughly one OCR call
per subtitle line, not per frame. Runs of frames carrying the same text collapse
into one {start, end, text} segment.

The recognition step - turning a changed band into text - runs on Gemini vision
when a Gemini API key is configured (the same key the translator uses), which is
far faster than the bundled CPU OCR and is what turns this mode from a long
background job into a quick one. With no key it uses the local RapidOCR engine.
"""
import base64
import json
import os
import re
import subprocess
import sys
import urllib.parse
from contextlib import redirect_stdout

# The Gemini plumbing (HTTP, key rotation, key parsing) is shared with the audio
# transcription worker sitting next to this file, so there is one retry/rotation
# policy rather than two that drift apart. If it is missing - a stripped bundle -
# reading falls back to the local RapidOCR engine.
try:
    from gemini_transcribe import (API_ROOT, load_env_file, request_json,
                                   split_api_keys, with_key_fallback)
    _GEMINI_AVAILABLE = True
except Exception:  # pragma: no cover - only when the sibling worker is absent
    _GEMINI_AVAILABLE = False

# The subtitle band, as fractions of frame height. Burned-in subtitles sit near
# the bottom; cropping to that strip cuts OCR work and stray on-screen text.
DEFAULT_CONFIG = {
    "sampleFps": 4.0,        # frames inspected per second (timing precision)
    "bandTop": 0.66,         # top of the crop, fraction of height
    "bandBottom": 1.0,       # bottom of the crop, fraction of height
    "diffThreshold": 8.0,    # mean 0-255 change that counts as "the band moved"
    "minScore": 0.5,         # drop OCR boxes below this confidence
    "minMs": 300,            # drop flicker: subtitles shown briefer than this
}

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def emit(payload):
    """One JSON object per line, flushed so the Qt side sees it immediately."""
    sys.stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def load_config(overrides=None):
    config = dict(DEFAULT_CONFIG)
    env_keys = {
        "sampleFps": "CUTPRO_OCR_SAMPLE_FPS",
        "bandTop": "CUTPRO_OCR_BAND_TOP",
        "bandBottom": "CUTPRO_OCR_BAND_BOTTOM",
        "diffThreshold": "CUTPRO_OCR_DIFF_THRESHOLD",
        "minScore": "CUTPRO_OCR_MIN_SCORE",
        "minMs": "CUTPRO_OCR_MIN_MS",
    }
    for key, env in env_keys.items():
        raw = os.environ.get(env, "").strip()
        if raw:
            try:
                config[key] = float(raw)
            except ValueError:
                pass
    for key, value in (overrides or {}).items():
        if key in config:
            try:
                config[key] = float(value)
            except (TypeError, ValueError):
                pass
    # A band that starts below where it ends would crop nothing.
    config["bandTop"] = min(max(0.0, config["bandTop"]), 0.95)
    config["bandBottom"] = min(max(config["bandTop"] + 0.05, config["bandBottom"]), 1.0)
    config["sampleFps"] = min(max(0.5, config["sampleFps"]), 15.0)
    return config


def ffprobe_path(ffmpeg):
    """ffprobe sits next to ffmpeg in the bundle; fall back to PATH."""
    if ffmpeg:
        base = os.path.basename(ffmpeg)
        probe = os.path.join(os.path.dirname(ffmpeg),
                             base.replace("ffmpeg", "ffprobe"))
        if os.path.exists(probe):
            return probe
    return "ffprobe"


def probe_dimensions(ffmpeg, source):
    """(width, height) of the first video stream, via ffprobe."""
    command = [ffprobe_path(ffmpeg), "-v", "error",
               "-select_streams", "v:0",
               "-show_entries", "stream=width,height",
               "-of", "csv=p=0:s=x", source]
    completed = subprocess.run(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               creationflags=NO_WINDOW)
    text = (completed.stdout or b"").decode("utf-8", "replace").strip()
    for token in text.replace(",", "x").split():
        parts = token.split("x")
        if len(parts) >= 2:
            try:
                return int(parts[0]), int(parts[1])
            except ValueError:
                continue
    raise RuntimeError("Could not read the video size for OCR.")


def make_engine():
    """RapidOCR loads its ONNX models on construction; do it once, and keep the
    library's own chatter off stdout so it never corrupts the JSON stream."""
    with redirect_stdout(sys.stderr):
        from rapidocr_onnxruntime import RapidOCR
        return RapidOCR()


def ocr_text(engine, frame, min_score):
    """The subtitle band -> one line of text, top-to-bottom, low-confidence
    boxes dropped. Empty string means no subtitle is on screen."""
    with redirect_stdout(sys.stderr):
        result, _ = engine(frame)
    if not result:
        return ""
    boxes = []
    for item in result:
        # RapidOCR rows are [box, text, score]; box[0] is the top-left point.
        try:
            box, text, score = item[0], item[1], item[2]
        except (IndexError, TypeError, ValueError):
            continue
        if float(score) < min_score:
            continue
        text = str(text).strip()
        if not text:
            continue
        try:
            top = min(point[1] for point in box)
            left = min(point[0] for point in box)
        except (TypeError, ValueError):
            top, left = 0, 0
        boxes.append((top, left, text))
    boxes.sort(key=lambda entry: (round(entry[0] / 10.0), entry[1]))
    return normalize_text(" ".join(entry[2] for entry in boxes))


def normalize_text(text):
    import re
    return re.sub(r"[ \t]+", " ", str(text)).strip()


def texts_similar(a, b):
    """Whether two reads are the same subtitle line seen twice.

    A line fades in and out, so the same subtitle is read several times as its
    opacity changes - often partially ("want" then "I won't know which one you
    want") or with a stray OCR slip ("Grandpa" vs "Granapa"). Those should stay
    one segment, while a genuinely different next line should not. Token overlap
    catches word-order wobble; the sequence ratio catches spaced and unspaced
    (CJK) text alike; a clean substring is always the same growing line."""
    if not a or not b:
        return a == b
    if a in b or b in a:
        return True
    tokens_a, tokens_b = set(a.split()), set(b.split())
    if len(tokens_a) > 1 and len(tokens_b) > 1:
        union = tokens_a | tokens_b
        if union and len(tokens_a & tokens_b) / len(union) >= 0.5:
            return True
    import difflib
    return difflib.SequenceMatcher(None, a, b).ratio() >= 0.6


def read_exact(stream, count):
    """Raw frames arrive on a pipe that may hand back short reads; block until a
    whole frame is in hand, or None at end of stream."""
    buffer = bytearray()
    while len(buffer) < count:
        chunk = stream.read(count - len(buffer))
        if not chunk:
            return None
        buffer.extend(chunk)
    return bytes(buffer)


def note(message):
    """Diagnostics to stderr; stdout is the JSON protocol and nothing else."""
    sys.stderr.write(str(message) + "\n")
    sys.stderr.flush()


def gemini_ocr_config():
    """Keys, model and batch size for reading the subtitle band with Gemini.

    The keys are the same ones the translator and the audio Gemini worker use,
    handed down by Backend::transcribeMedia so a user configures Gemini once. No
    keys (or no sibling worker) means the caller falls back to local RapidOCR."""
    if _GEMINI_AVAILABLE:
        load_env_file()
    configured = (os.environ.get("CUTPRO_TRANSCRIPTION_API_KEYS", "").strip()
                  or os.environ.get("CUTPRO_TRANSLATION_API_KEYS", "").strip()
                  or os.environ.get("GEMINI_API_KEYS", "").strip()
                  or os.environ.get("GEMINI_API_KEY", "").strip())
    keys = split_api_keys(configured) if _GEMINI_AVAILABLE else []
    model = (os.environ.get("CUTPRO_OCR_GEMINI_MODEL", "").strip()
             or "gemini-2.5-flash")
    try:
        batch = max(1, int(float(os.environ.get("CUTPRO_OCR_GEMINI_BATCH",
                                                "16"))))
    except ValueError:
        batch = 16
    return {"api_keys": keys, "active_key_index": 0, "model": model,
            "batch": batch}


def ocr_batch_prompt(count):
    return (
        "Each image is the cropped bottom strip of a video frame, given in "
        "order and labelled Image 1..%d. Read the burned-in subtitle or "
        "caption text shown in each image, exactly as written, in its own "
        "language. If an image shows no subtitle text, use an empty string for "
        "it.\n"
        "Return ONLY a JSON array of exactly %d strings - one per image, in the "
        "same order. No objects, no numbering, no markdown fences, no other "
        "text." % (count, count))


def parse_string_array(reply, count):
    """The model's reply -> exactly `count` strings, padding/truncating to fit."""
    cleaned = str(reply or "").strip()
    fenced = re.search(r"```(?:json)?\s*(.*?)\s*```", cleaned, re.DOTALL)
    if fenced:
        cleaned = fenced.group(1).strip()
    start, end = cleaned.find("["), cleaned.rfind("]")
    values = []
    if 0 <= start < end:
        try:
            parsed = json.loads(cleaned[start:end + 1])
        except (ValueError, json.JSONDecodeError):
            parsed = None
        if isinstance(parsed, list):
            values = ["" if item is None else str(item) for item in parsed]
    if len(values) < count:
        values += [""] * (count - len(values))
    return values[:count]


def gemini_read_images(config, images):
    """A list of JPEG subtitle-band crops -> one text per image, in order.

    Frames are sent in batches, one request each, so a subtitle line costs a
    fraction of a request rather than the ~1.6s a CPU OCR pass takes per frame.
    A batch that fails to send or parse yields empty reads for its frames rather
    than losing the whole window."""
    if not images:
        return []
    texts = [""] * len(images)
    batch = int(config.get("batch", 16))
    model = urllib.parse.quote(config["model"], safe="-._")
    for base in range(0, len(images), batch):
        chunk = images[base:base + batch]
        parts = [{"text": ocr_batch_prompt(len(chunk))}]
        for number, jpeg in enumerate(chunk, 1):
            parts.append({"text": "Image %d:" % number})
            parts.append({"inline_data": {
                "mime_type": "image/jpeg",
                "data": base64.b64encode(jpeg).decode("ascii")}})
        payload = {"contents": [{"parts": parts}],
                   "generationConfig": {"temperature": 0.0}}

        def ask(api_key, payload=payload):
            url = "%s/%s:generateContent?key=%s" % (
                API_ROOT, model, urllib.parse.quote(api_key, safe=""))
            result = request_json(url, payload)
            candidates = result.get("candidates") or []
            if not candidates:
                feedback = result.get("promptFeedback") or {}
                blocked = feedback.get("blockReason") or ""
                raise RuntimeError("The model returned no text%s"
                                   % (" (%s)" % blocked if blocked else ""))
            reply_parts = (candidates[0].get("content") or {}).get("parts") or []
            return "".join(str(part.get("text", "")) for part in reply_parts)

        try:
            reply = with_key_fallback(config, ask)
            values = parse_string_array(reply, len(chunk))
        except Exception as error:
            note("ocr-gemini: batch at frame %d failed: %s" % (base, error))
            values = [""] * len(chunk)
        for offset, value in enumerate(values):
            texts[base + offset] = normalize_text(value)
    return texts


def drain(frame_iter, on_frame):
    """Pull every (timestamp, frame) the generator yields, calling on_frame for
    each, and return the window-end time the generator hands back at the end."""
    iterator = iter(frame_iter)
    window_end = 0.0
    while True:
        try:
            timestamp, frame = next(iterator)
        except StopIteration as stop:
            if stop.value is not None:
                window_end = stop.value
            break
        on_frame(timestamp, frame)
    return window_end


def iter_changed_frames(ffmpeg, source, start_ms, length_ms, config,
                        width, height):
    """Stream the subtitle band, yielding (timestamp_seconds, frame) only when
    the band visibly changes - a static line is one yield however long it stays
    up. The window's end time comes back as the generator's return value."""
    import numpy as np
    import cv2

    fps = config["sampleFps"]
    crop_top = int(round(height * config["bandTop"]))
    crop_bottom = int(round(height * config["bandBottom"]))
    band_h = max(2, crop_bottom - crop_top)
    frame_bytes = width * band_h * 3

    command = [
        ffmpeg or "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
    ]
    if start_ms > 0:
        command += ["-ss", "%.3f" % (start_ms / 1000.0)]
    if length_ms > 0:
        command += ["-t", "%.3f" % (length_ms / 1000.0)]
    command += [
        "-i", source,
        "-vf", "fps=%s,crop=%d:%d:0:%d" % (fps, width, band_h, crop_top),
        "-pix_fmt", "bgr24", "-f", "rawvideo", "pipe:1",
    ]
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               creationflags=NO_WINDOW)

    previous_signature = None
    threshold = config["diffThreshold"]
    index = 0
    try:
        while True:
            raw = read_exact(process.stdout, frame_bytes)
            if raw is None:
                break
            timestamp = index / fps
            index += 1
            frame = np.frombuffer(raw, np.uint8).reshape(band_h, width, 3)
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            signature = cv2.resize(gray, (64, 16),
                                   interpolation=cv2.INTER_AREA).astype(np.int16)
            changed = (previous_signature is None or
                       float(np.abs(signature - previous_signature).mean())
                       > threshold)
            previous_signature = signature
            if changed:
                yield timestamp, frame
    finally:
        try:
            process.stdout.close()
        except OSError:
            pass
        stderr = process.stderr.read() if process.stderr else b""
        process.wait()

    if process.returncode not in (0, None) and index == 0:
        detail = stderr.decode("utf-8", "replace").strip()
        raise RuntimeError("Could not read frames for OCR: %s"
                           % (detail or "ffmpeg failed"))
    return (length_ms / 1000.0) if length_ms > 0 else (index / fps)


def build_segments_from_reads(reads, window_end, config):
    """Ordered (timestamp, text) reads -> {start, end, text} segments.

    Consecutive frames carrying the same text are one segment; a line read
    repeatedly through its fade (partially, or with a slip) is folded into one
    via texts_similar, keeping the fuller read. Flicker shorter than minMs is
    dropped, and a line still up at the window edge runs to it."""
    min_seconds = config["minMs"] / 1000.0
    segments = []
    current_text = ""
    current_start = 0.0
    for timestamp, text in reads:
        if text == current_text:
            continue
        if current_text and texts_similar(text, current_text):
            if len(text) > len(current_text):
                current_text = text
            continue
        if current_text and timestamp - current_start >= min_seconds:
            segments.append({"start": current_start, "end": timestamp,
                             "text": current_text})
        current_text = text
        current_start = timestamp
    if current_text and window_end - current_start >= min_seconds:
        segments.append({"start": current_start, "end": window_end,
                         "text": current_text})
    return segments


class LocalRecognizer:
    """Reads each changed frame with the bundled RapidOCR engine, inline."""
    name = "local OCR"

    def __init__(self, engine, config):
        self.engine = engine
        self.min_score = config["minScore"]

    def read_window(self, frame_iter):
        reads = []
        window_end = drain(frame_iter, lambda ts, frame: reads.append(
            (ts, ocr_text(self.engine, frame, self.min_score))))
        return reads, window_end


class GeminiRecognizer:
    """Sends the changed frames' subtitle bands to Gemini vision, batched."""
    name = "Gemini"

    def __init__(self, config):
        self.config = config

    def read_window(self, frame_iter):
        import cv2
        timestamps = []
        images = []

        def collect(timestamp, frame):
            ok, buffer = cv2.imencode(".jpg", frame,
                                      [int(cv2.IMWRITE_JPEG_QUALITY), 85])
            if ok:
                timestamps.append(timestamp)
                images.append(bytes(buffer))

        window_end = drain(frame_iter, collect)
        texts = gemini_read_images(self.config, images)
        return list(zip(timestamps, texts)), window_end


def build_recognizer(config):
    """Gemini vision when a key is configured (fast, no local CPU cost), else
    the bundled RapidOCR engine (slower, but needs no key and no network)."""
    gemini = gemini_ocr_config()
    if gemini["api_keys"]:
        return GeminiRecognizer(gemini)
    return LocalRecognizer(make_engine(), config)


def process_window(recognizer, ffmpeg, source, start_ms, length_ms, config,
                   width, height):
    """One window of video -> subtitle segments in window-local seconds."""
    reads, window_end = recognizer.read_window(
        iter_changed_frames(ffmpeg, source, start_ms, length_ms, config,
                            width, height))
    return build_segments_from_reads(reads, window_end, config)


def run_job(job_path):
    with open(job_path, "r", encoding="utf-8") as handle:
        job = json.load(handle)

    source = job["source"]
    language = job.get("language", "auto")
    ffmpeg = job.get("ffmpeg", "") or os.environ.get("CUTPRO_FFMPEG", "")
    windows = job.get("windows") or []
    config = load_config(job.get("ocr"))

    recognizer = build_recognizer(config)
    width, height = probe_dimensions(ffmpeg, source)

    for index, window in enumerate(windows):
        start_ms = int(window.get("startMs", 0))
        length_ms = int(window.get("lengthMs", 0))
        emit({"type": "window", "index": index, "count": len(windows),
              "startMs": start_ms})
        segments = process_window(recognizer, ffmpeg, source, start_ms,
                                  length_ms, config, width, height)
        emit({"type": "segments", "index": index, "startMs": start_ms,
              "segments": segments})

    emit({"type": "done", "language": "" if language == "auto" else language})
    return 0


def run_single(argv):
    source = argv[1]
    language = argv[3] if len(argv) > 3 else "auto"
    ffmpeg = os.environ.get("CUTPRO_FFMPEG", "")
    config = load_config()

    recognizer = build_recognizer(config)
    # One request has no sub-progress, so the panel's bar cannot move; a status
    # line keeps it from a frozen 0%. "type":"status" is treated as status-only
    # by the backend and does not flip the run onto the windowed path.
    emit({"type": "status",
          "message": "Reading on-screen subtitles with %s..." % recognizer.name})
    width, height = probe_dimensions(ffmpeg, source)
    segments = process_window(recognizer, ffmpeg, source, 0, 0, config,
                              width, height)
    text = " ".join(segment["text"] for segment in segments)
    emit({"text": text, "segments": segments,
          "language": "" if language == "auto" else language})
    return 0


def main():
    if len(sys.argv) < 2:
        emit({"error": "usage: ocr_subtitles.py MEDIA [MODEL] [LANGUAGE]"
                       " | --job JOB.json"})
        return 2
    try:
        if sys.argv[1] == "--job":
            if len(sys.argv) < 3:
                emit({"error": "usage: ocr_subtitles.py --job JOB.json"})
                return 2
            return run_job(sys.argv[2])
        return run_single(sys.argv)
    except Exception as error:
        emit({"error": str(error)})
        return 4


if __name__ == "__main__":
    raise SystemExit(main())
