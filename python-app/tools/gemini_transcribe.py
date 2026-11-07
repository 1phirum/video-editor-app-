#!/usr/bin/env python3
"""Gemini worker that writes a JSON transcript to stdout.

Deliberately the same two invocation forms and the same line protocol as
whisper_transcribe.py, so nothing on the Qt side has to know which engine
produced a transcript:

  gemini_transcribe.py MEDIA [MODEL] [LANGUAGE]
      Short overlapping speech requests, one JSON object for the whole file.

  gemini_transcribe.py --job JOB.json
      Windowed path. Same job file, same streamed lines:

        {"type": "window",   "index": i, "count": n, "startMs": ...}
        {"type": "segments", "index": i, "segments": [...]}   window-local times
        {"type": "done",     "language": "en"}
        {"error": "..."}                                      on failure

Windows matter here for a different reason than they do for Whisper. Nothing is
held in memory: the audio is uploaded, so the limits are the request size (20 MB
inline) and how much transcript the model will write in one reply. Both scale
with the window, which is why the planner keeps a Gemini window short.

Audio is re-encoded to 32 kbps mono MP3 before it is sent - about 4 KB per
second against the 32 KB/s of the 16 kHz PCM the app extracts, which is the
difference between a window that fits inline and one that does not. Sections
copy those MP3 packets without another lossy encode.

The API key and the model come from the environment, set by
Backend::transcribeMedia: CUTPRO_TRANSCRIPTION_API_KEYS holds the same keys the
translation provider uses, so a user configures Gemini once.
"""
import base64
import http.client
import json
import math
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

# Windows embedded Python does not include the script directory in sys.path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from persistent_retry import ModelOutputError, failure_policy, generation_rejection, retry_after_seconds, retry_work

# Launched from the GUI under pythonw.exe (no console of its own), so a child
# console program like ffmpeg would otherwise pop up its own black window for a
# moment. CREATE_NO_WINDOW suppresses it while leaving the captured pipes intact.
CREATE_NO_WINDOW = 0x08000000 if os.name == "nt" else 0

API_ROOT = "https://generativelanguage.googleapis.com/v1beta/models"
# urllib's default fingerprint is fine for Google, but the JSON headers are kept
# identical to translate_transcript.py so both workers behave the same way.
API_REQUEST_HEADERS = {
    "Accept": "application/json",
    "User-Agent": "CutPro/0.1",
}

LANGUAGE_NAMES = {
    "en": "English", "zh": "Chinese", "km": "Khmer", "es": "Spanish",
    "fr": "French", "de": "German", "ja": "Japanese", "ko": "Korean",
    "vi": "Vietnamese",
}

# The same backstop the Whisper worker applies, in milliseconds. A model asked
# for "short lines" will still occasionally write a paragraph.
DEFAULT_MAX_MS = 6200
# Whole-clip replies can put later dialogue beyond the source's duration. Keep
# each speech request's clock below a minute so those lines stay on the timeline.
SPEECH_SECTION_SECONDS = 30.0


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


def note(message):
    """Diagnostics go to stderr. stdout carries the protocol and nothing else."""
    sys.stderr.write(str(message) + "\n")
    sys.stderr.flush()


class ProviderRequestError(RuntimeError):
    def __init__(self, message, status=0, retryable=True, retry_after=0):
        super().__init__(message)
        self.status = status
        self.retryable = retryable
        self.retry_after = retry_after


def load_env_file():
    """A .env beside the app, for a developer running the worker by hand."""
    here = os.path.abspath(__file__)
    parents = [os.path.dirname(here)]
    for _ in range(3):
        parents.append(os.path.dirname(parents[-1]))
    for directory in [os.getcwd()] + parents:
        path = os.path.join(directory, ".env")
        if not os.path.isfile(path):
            continue
        try:
            with open(path, "r", encoding="utf-8-sig") as handle:
                lines = handle.read().splitlines()
        except OSError:
            return
        for raw_line in lines:
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            os.environ.setdefault(key.strip(),
                                  value.strip().strip('"').strip("'"))
        return


def split_api_keys(value):
    keys = []
    for candidate in re.split(r"[,;\r\n]+", value or ""):
        key = candidate.strip()
        if key and key not in keys:
            keys.append(key)
    return keys


def provider_config():
    load_env_file()
    model = os.environ.get("CUTPRO_TRANSCRIPTION_MODEL", "").strip()
    configured = (os.environ.get("CUTPRO_TRANSCRIPTION_API_KEYS", "").strip()
                  or os.environ.get("CUTPRO_TRANSLATION_API_KEYS", "").strip()
                  or os.environ.get("GEMINI_API_KEYS", "").strip()
                  or os.environ.get("GEMINI_API_KEY", "").strip())
    return {
        "model": model or "gemini-2.5-flash",
        "api_keys": split_api_keys(configured),
        "active_key_index": 0,
        "ffmpeg": os.environ.get("CUTPRO_FFMPEG", "").strip() or "ffmpeg",
    }


def request_json(url, payload, timeout=300):
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    request = urllib.request.Request(
        url, data=data,
        headers={"Content-Type": "application/json", **API_REQUEST_HEADERS},
        method="POST" if data is not None else "GET")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace").strip()
        retryable = True
        retry_after = retry_after_seconds(error.headers.get("Retry-After") if error.headers else None)
        try:
            parsed = json.loads(detail)
            retryable = (parsed.get("error") or {}).get("retryable") is not False
            retry_after = max(retry_after, retry_after_seconds(
                (parsed.get("error") or {}).get("retryAfterSeconds")))
            detail = str(parsed.get("error", parsed))
        except Exception:
            pass
        raise ProviderRequestError(
            "API request failed (%d): %s" % (error.code, detail[:500]),
            error.code, retryable, retry_after) from error
    except (urllib.error.URLError, OSError, http.client.HTTPException) as error:
        # urlopen/read can raise RemoteDisconnected or IncompleteRead directly,
        # bypassing URLError. Normalize them so section/key retries can recover
        # without publishing a partial transcript or aborting the whole job.
        raise ProviderRequestError("API connection failed: %s" % error,
                                   0) from error


def can_rotate_key(error):
    if isinstance(error, ProviderRequestError):
        if not error.retryable:
            return False
        if error.status in {401, 402, 403, 408, 409, 429, 500, 502, 503, 504}:
            return True
    message = str(error).lower()
    return any(marker in message for marker in (
        "rate limit", "rate-limit", "quota", "resource_exhausted",
        "billing", "insufficient", "credit", "token limit",
        "temporarily unavailable", "overloaded", "invalid api key",
        "api key not valid", "unauthorized",
    ))


def is_transient_server_error(error):
    """A transport/server hiccup the same key should wait out, not a bad key.

    A dropped connection, timeout or 503 "the model is overloaded / experiencing
    high demand" is not this key's problem: every configured key talks to the
    same model, so rotating to another key cannot clear it. These are retried on
    the same key after a backoff. Key faults (bad credentials, denied project,
    exhausted quota) are NOT transient - those rotate to the next key at once.
    """
    if isinstance(error, ProviderRequestError):
        if not error.retryable:
            return False
        # A 429 is this key's quota, not the model's - let it rotate, not wait.
        if error.status in {0, 408, 500, 502, 503, 504}:
            return True
        if error.status:
            return False
    message = str(error).lower()
    return any(marker in message for marker in (
        "overloaded", "high demand", "experiencing high demand",
        "temporarily unavailable", "try again later", "internal error",
        "unavailable",
    ))


def with_key_fallback(config, operation, transient_retries=3,
                      transient_backoff=1.5):
    """Every configured key is tried before the run is called a failure.

    The keys are shared with the translation provider, where a user pastes
    several precisely so a quota on one does not stop the job. The index that
    worked is remembered, so a long transcription does not walk past a dead key
    once per window.

    A transient server error (an overloaded model, a 5xx) is waited out on the
    same key with a growing backoff before that key is given up on - rotating
    would only hit the same busy model on a different key. A key-specific error
    (bad credentials, denied project, exhausted quota) rotates immediately.
    """
    keys = config.get("api_keys") or []
    if not keys:
        raise ValueError("The Gemini API key is missing.")
    failures = []
    start_index = int(config.get("active_key_index", 0)) % len(keys)
    indexes = list(range(start_index, len(keys))) + list(range(0, start_index))
    for attempt, index in enumerate(indexes):
        for retry in range(transient_retries + 1):
            try:
                result = operation(keys[index])
                config["active_key_index"] = index
                return result
            except Exception as error:
                if is_transient_server_error(error) and retry < transient_retries:
                    wait = transient_backoff * (2 ** retry)
                    note("gemini: temporary request failure on key %d, retrying in %.0fs"
                         % (index + 1, wait))
                    time.sleep(wait)
                    continue
                safe_error = str(error)
                for configured_key in keys:
                    safe_error = safe_error.replace(configured_key, "<hidden>")
                failures.append("Key %d: %s" % (index + 1, safe_error))
                if not can_rotate_key(error) or attempt == len(indexes) - 1:
                    raise RuntimeError("; ".join(failures)) from error
                break  # a key-specific fault - rotate to the next key
    raise RuntimeError("Every configured Gemini API key failed.")


def run_ffmpeg(command, what):
    completed = subprocess.run(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE,
                               creationflags=CREATE_NO_WINDOW)
    if completed.returncode != 0:
        detail = (completed.stderr or b"").decode("utf-8", "replace").strip()
        raise RuntimeError("%s: %s" % (what, detail or "ffmpeg failed"))


def encode_for_upload(ffmpeg, source, destination, start_ms=None,
                      length_ms=None):
    """One audio stream -> 32 kbps mono 16 kHz MP3, the form that is uploaded.

    -ss before -i so a seek into a multi-gigabyte source is a container seek
    rather than a decode, and -vn so the video stream is never touched.
    """
    command = [ffmpeg or "ffmpeg", "-nostdin", "-hide_banner",
               "-loglevel", "error", "-y"]
    if start_ms is not None:
        command += ["-ss", "%.3f" % (max(0, start_ms) / 1000.0)]
    if length_ms is not None:
        command += ["-t", "%.3f" % (max(1, length_ms) / 1000.0)]
    command += ["-i", source, "-map", "0:a:0", "-vn", "-sn",
                "-ac", "1", "-ar", "16000", "-b:a", "32k",
                "-f", "mp3", destination]
    run_ffmpeg(command, "Could not prepare audio for transcription")
    if not os.path.exists(destination) or os.path.getsize(destination) == 0:
        raise RuntimeError("Could not prepare audio for transcription: the "
                           "source has no audio track.")
    return destination


def prefetched_window(temp_dir, index, timeout_seconds):
    """Pick up the window audio the Qt side is extracting ahead of this loop.

    The file appears atomically - written as .part and renamed - so its presence
    means it is complete. A .failed marker, or the timeout, means this process
    extracts the window itself.
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


def recap_enabled():
    return os.environ.get("CUTPRO_TRANSCRIPTION_RECAP", "").strip() not in (
        "", "0", "false", "False")


def transcription_prompt(language, max_ms, audio_seconds=0.0):
    # Transcription is always verbatim and accurate, whether or not Recap movie
    # is on. Recap is a second, text-only pass over these accurate lines
    # (enrich_segments) - so the transcript stays complete and correctly timed,
    # and the recap is built on top of it rather than replacing it.
    target = LANGUAGE_NAMES.get(language, "")
    spoken = ("The expected spoken language is %s. Keep speech in its original "
              "language; do not translate." % target) if target else (
        "Detect the spoken language and transcribe in it. Do not translate.")
    duration = ("This audio section is %.3f seconds long. Its clock starts at 0; "
                "every start and end must be between 0 and %.3f seconds.\n"
                % (audio_seconds, audio_seconds)) if audio_seconds > 0 else ""
    return (
        "Produce a complete, verbatim transcript of the audible speech. "
        "Listen to the entire audio section before returning the result.\n"
        "%s\n"
        "%s"
        "Return ONLY a JSON array. Each item must be an object with exactly "
        'these keys: "start" (seconds from the beginning of this audio, a '
        'number), "end" (seconds, a number) and "text" (the words spoken, '
        "verbatim).\n"
        "Rules:\n"
        "- Transcribe the ENTIRE audio, from the first word to the last. Keep "
        "going to the very end - do not stop partway.\n"
        "- Include EVERY spoken line. Do not skip, drop or summarise anything, "
        "including short interjections, single words, names and exclamations "
        '("Hey", "Hmm", "You", "What?") and lines that repeat. When two people '
        "talk over each other, write a separate line for each.\n"
        "- Follow the actual speaker's wording, including narration, quiet "
        "speech, corrections and unfinished sentences. Never rewrite dialogue "
        "as a recap or infer speech from the plot. Keep clear words and mark "
        "only genuinely unintelligible speech as [inaudible]; do not invent it.\n"
        "- Use normal spelling and punctuation in the original script, not "
        "phonetic syllables. For Mandarin, write Chinese characters without "
        "inserting spaces between individual characters.\n"
        "- Break lines at speaker changes, pauses or sentence endings. If a "
        "line exceeds %.1f seconds or about 84 characters, split it into "
        "additional lines WITHOUT removing or shortening any spoken words.\n"
        "- Use decimal SECONDS relative to this audio section, never "
        "milliseconds or a minute:second number. Match each timestamp to the "
        "actual onset and end of that utterance, not evenly spaced intervals.\n"
        "- Transcribe only what is said. No summaries, no speaker labels, no "
        "sound descriptions, no commentary, no markdown fences.\n"
        "- Leave real silence as gaps. If nothing is spoken, return [].\n"
        "Before returning, check the beginning, middle and end for omitted "
        "speech. Include dialogue after long pauses and verify every timestamp "
        "against this audio section's duration. Return only the JSON array."
        % (spoken, duration, max_ms / 1000.0))


def parse_timestamp(value):
    """Seconds from a number, or from an "mm:ss.mmm" / "hh:mm:ss,mmm" string."""
    if isinstance(value, (int, float)):
        return float(value)
    text = str(value or "").strip().replace(",", ".")
    if not text:
        return None
    try:
        return float(text)
    except ValueError:
        pass
    parts = text.split(":")
    if not 2 <= len(parts) <= 3:
        return None
    try:
        seconds = float(parts[-1])
        seconds += float(parts[-2]) * 60.0
        if len(parts) == 3:
            seconds += float(parts[0]) * 3600.0
    except ValueError:
        return None
    return seconds


def parse_segment_array(text):
    """The model's reply -> a list of dicts, or a RuntimeError explaining why not."""
    cleaned = str(text or "").strip()
    fenced = re.search(r"```(?:json)?\s*(.*?)\s*```", cleaned, re.DOTALL)
    if fenced:
        cleaned = fenced.group(1).strip()
    start, end = cleaned.find("["), cleaned.rfind("]")
    if start < 0 or end < start:
        raise ModelOutputError("The model did not return a JSON segment array.")
    values = json.loads(cleaned[start:end + 1])
    if not isinstance(values, list):
        raise ModelOutputError("The model did not return a JSON segment array.")
    return values


def shape_segments(values, audio_seconds, max_ms):
    """What the model wrote -> what the app can store.

    The app's segment contract is narrow: start < end, in order, never
    overlapping, non-empty text, inside the audio. A model reply satisfies it
    most of the time and has to be made to satisfy it always, because these
    numbers become subtitle clips and an export crop of the timeline.
    """
    limit = max(0.001, float(audio_seconds)) if audio_seconds else 0.0
    segments = []
    for value in values:
        if not isinstance(value, dict):
            continue
        text = re.sub(r"\s+", " ", str(value.get("text", ""))).strip()
        start = parse_timestamp(value.get("start"))
        end = parse_timestamp(value.get("end"))
        if not text or start is None or end is None:
            continue
        start = max(0.0, start)
        end = max(start, end)
        if limit:
            start = min(start, limit)
            end = min(max(end, start), limit)
        if end - start < 0.02:
            # A zero-length line cannot be shown. Give it the smaller of the
            # backstop and what is left of the audio, so it is still readable.
            end = min(start + 1.2, limit) if limit else start + 1.2
        if end <= start:
            continue
        segments.append({"start": start, "end": end, "text": text})

    segments.sort(key=lambda item: item["start"])
    ceiling = max_ms / 1000.0
    # A line trimmed to nothing by overlap resolution must still keep its words:
    # the transcript feeds translation and text-to-speech, where losing a line
    # loses what a character said. So a collapsed line is never discarded - it
    # keeps a readable minimum and the next line starts after it instead.
    min_visible = 0.30
    shaped = []
    for segment in segments:
        # Overlaps are the model's most common timing mistake. The later line
        # keeps its start and the earlier one gives way - but only down to a
        # readable minimum; below that the earlier line holds its minimum slice
        # and the later line is pushed to start after it, so neither is dropped.
        if shaped:
            previous = shaped[-1]
            if segment["start"] < previous["end"]:
                trimmed_end = max(previous["start"], segment["start"])
                if trimmed_end - previous["start"] >= min_visible:
                    previous["end"] = trimmed_end
                else:
                    previous["end"] = previous["start"] + min_visible
                    if segment["start"] < previous["end"]:
                        shift = previous["end"] - segment["start"]
                        segment["start"] += shift
                        segment["end"] += shift
        if segment["end"] - segment["start"] < min_visible:
            segment["end"] = segment["start"] + min_visible
        if segment["end"] - segment["start"] > ceiling * 2.5:
            segment["end"] = segment["start"] + ceiling * 2.5
        if limit:
            if segment["start"] >= limit:
                raise ModelOutputError("Gemini speech overlaps cannot fit inside the audio section.")
            segment["end"] = min(segment["end"], limit)
        shaped.append(segment)
    return shaped


def request_transcript(config, audio_path, language, max_ms, plain=False,
                       via_pool=False, audio_seconds=0.0):
    """One audio-section upload, one complete JSON reply.

    The audio goes inline rather than through the Files API: a window is a few
    megabytes at 32 kbps, which fits the inline limit comfortably, and an inline
    request needs no upload handle, no lifetime and no cleanup on a run the user
    cancels halfway through.

    `plain` drops responseMimeType and asks for nothing but a temperature, which
    is the shape translate_transcript.py has always sent and is therefore the one
    shape every offered model is known to accept. The retry uses it so that a
    model which rejects the JSON mime type still gets a second, simpler chance -
    parse_segment_array already copes with a fenced or chatty reply.

    `via_pool` posts the same payload to the local account pool instead of
    Google with a key: the pool endpoint accepts the identical request shape
    and rotates signed-in Google accounts on its side.
    """
    with open(audio_path, "rb") as handle:
        encoded = base64.b64encode(handle.read()).decode("ascii")
    prompt = transcription_prompt(language, max_ms, audio_seconds)
    # No maxOutputTokens: the models offered here cap output anywhere from 8192
    # tokens upwards, and naming a ceiling above a model's own is a 400 rather
    # than a longer reply. Omitting it gives each model its own maximum, which is
    # the most a request can get.
    generation = {"temperature": 0.0}
    if not plain:
        generation["responseMimeType"] = "application/json"
    payload = {
        "contents": [{"parts": [
            {"text": prompt},
            {"inline_data": {"mime_type": "audio/mp3", "data": encoded}},
        ]}],
        "generationConfig": generation,
    }
    model = urllib.parse.quote(config["model"], safe="-._")

    def ask(api_key):
        if via_pool:
            url = "%s/v1beta/models/%s:generateContent" % (
                pool_endpoint(), model)
        else:
            url = "%s/%s:generateContent?key=%s" % (
                API_ROOT, model, urllib.parse.quote(api_key, safe=""))
        result = request_json(url, payload)
        reason, permanent = generation_rejection(result)
        if reason:
            message = ("The model declined the transcript (%s)" if permanent else
                       "The model returned no transcript (%s); waiting to retry") % reason
            raise ProviderRequestError(message, 422 if permanent else 503, False)
        candidates = result.get("candidates") or []
        if not candidates:
            raise ModelOutputError("The model returned no transcript")
        candidate = candidates[0]
        parts = (candidate.get("content") or {}).get("parts") or []
        text = "".join(str(part.get("text", "")) for part in parts)
        if candidate.get("finishReason") in ("MAX_TOKENS", "LENGTH"):
            # A syntactically valid partial array is still missing speech. Never
            # let it be published as a completed transcript by the Qt exit handler.
            raise ModelOutputError("The Gemini transcript is incomplete (%s)."
                               % candidate["finishReason"])
        return text

    if via_pool:
        # The pool rotates accounts on its side; there is no key to walk.
        return ask("")
    return with_key_fallback(config, ask)


def pool_endpoint():
    """The local account-pool Core, when the app has one running."""
    if os.environ.get("CUTPRO_POOL_MODE") == "0":
        return ""
    return os.environ.get("CUTPRO_POOL_ENDPOINT", "").strip().rstrip("/")


def pool_parallelism(count):
    if not pool_endpoint():
        return 1
    try:
        return max(1, min(8, count, int(os.environ.get("CUTPRO_POOL_PARALLELISM", "1"))))
    except ValueError:
        return 1


def validate_api_fallback(config):
    """Pool model IDs need not exist in the public API-key catalogue."""
    if config.get("api_model_checked") == config["model"]:
        return
    model = urllib.parse.quote(config["model"], safe="-._")

    def check(api_key):
        result = request_json("%s/%s?key=%s" % (
            API_ROOT, model, urllib.parse.quote(api_key, safe="")), None,
            timeout=30)
        if "generateContent" not in (result.get("supportedGenerationMethods") or []):
            raise ValueError("This model does not support generateContent with Gemini API keys.")

    with_key_fallback(config, check)
    config["api_model_checked"] = config["model"]


def transcribe_section(config, audio_path, language, max_ms, audio_seconds):
    """A request plus one retry, because the failure is usually the JSON.

    A model that wrote prose, incomplete output or invalid timestamps is rejected
    before any segments are published. Asking again
    at temperature 0 with the same audio usually produces a clean reply. The
    retry also drops responseMimeType, so a model that rejected the request
    outright over that field is not asked the same impossible question twice. A
    second failure is real and is reported.

    When the account pool is running (CUTPRO_POOL_ENDPOINT), its requests go
    there first - the pool rotates signed-in Google accounts itself and speaks
    the same request shape - and the configured API keys remain the fallback
    for a run the pool could not serve, if the model supports API-key requests.
    """
    pool = pool_endpoint()
    if config.get("persistent_work") and not pool and os.environ.get("CUTPRO_POOL_MODE") == "1":
        raise ValueError("The Account Pool address is unavailable. Start the pool and retry this job.")
    last_pool_error = None
    if pool:
        plain = False
        for attempt in range(2):
            try:
                reply = request_transcript(config, audio_path, language, max_ms,
                                           plain=plain, via_pool=True,
                                           audio_seconds=audio_seconds)
                values = parse_segment_array(reply)
                validate_timestamps(values, audio_seconds)
                return shape_segments(values, audio_seconds, max_ms)
            except (RuntimeError, ValueError, json.JSONDecodeError) as error:
                if isinstance(error, ProviderRequestError) and not error.retryable:
                    raise
                if config.get("persistent_work") and isinstance(error, ProviderRequestError) and failure_policy(error)[0]:
                    raise
                last_pool_error = error
                # A lost local HTTP response must retry the identical payload
                # so the Core can return its cached result without uploading
                # the audio twice. Only a malformed model reply changes shape.
                if not isinstance(error, ProviderRequestError) or error.status != 0:
                    plain = True
                note("pool: attempt %d failed: %s" % (attempt + 1, error))
                if attempt == 0:
                    time.sleep(1.5)
    if pool and config.get("persistent_work") and last_pool_error is not None:
        raise last_pool_error
    if pool or os.environ.get("CUTPRO_POOL_MODE") == "1":
        if last_pool_error is None:
            last_pool_error = RuntimeError("The Account Pool is not running or has no ready accounts.")
        if not config.get("api_keys"):
            # Pool-only users have no fallback key; retain the actual failure
            # instead of replacing it with the misleading 'API key is missing'.
            raise RuntimeError("Pool transcription failed: %s" % last_pool_error) from last_pool_error
        try:
            validate_api_fallback(config)
        except (RuntimeError, ValueError) as error:
            raise RuntimeError(
                "Pool transcription failed: %s. API-key fallback is unavailable for %s: %s. "
                "Retry with the Account Pool or choose a Gemini model available to your API keys."
                % (last_pool_error, config["model"], error)) from error
        note("pool: falling back to configured API keys (%s)" % last_pool_error)
    last_error = None
    for attempt in range(2):
        try:
            reply = request_transcript(config, audio_path, language, max_ms,
                                       plain=attempt > 0,
                                       audio_seconds=audio_seconds)
            values = parse_segment_array(reply)
            validate_timestamps(values, audio_seconds)
            return shape_segments(values, audio_seconds, max_ms)
        except (RuntimeError, ValueError, json.JSONDecodeError) as error:
            last_error = error
            if isinstance(error, ValueError) and "key is missing" in str(error):
                raise
            note("gemini: attempt %d failed: %s" % (attempt + 1, error))
            if attempt == 0:
                time.sleep(1.5)
    raise RuntimeError(str(last_error)) from last_error


def audio_duration(ffmpeg, audio_path):
    probe = os.path.join(os.path.dirname(ffmpeg or ""),
                         "ffprobe.exe" if os.name == "nt" else "ffprobe")
    result = subprocess.run(
        [probe, "-v", "error", "-show_entries", "format=duration", "-of",
         "json", audio_path], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, creationflags=CREATE_NO_WINDOW)
    try:
        seconds = float(json.loads(result.stdout)["format"]["duration"])
    except (ValueError, KeyError, TypeError):
        raise RuntimeError("Could not determine the audio duration.") from None
    if result.returncode or not math.isfinite(seconds) or seconds <= 0:
        raise RuntimeError("Could not determine the audio duration.")
    return seconds


def validate_timestamps(values, audio_seconds):
    # In the reported 113 s clip, the last 12 lines arrived at 119-150 s and
    # disappeared from the sequence. Reject that reply instead of clamping all
    # of its missing dialogue onto the last frame or silently discarding it.
    for value in values:
        if not isinstance(value, dict) or not str(value.get("text", "")).strip():
            continue
        start, end = parse_timestamp(value.get("start")), parse_timestamp(value.get("end"))
        if (start is None or end is None or not math.isfinite(start)
                or not math.isfinite(end) or start < 0 or end <= start
                or end > audio_seconds + 0.15):
            raise ModelOutputError("Gemini returned a speech timestamp outside the "
                               "%.3f-second audio section." % audio_seconds)


def transcribe_audio(config, audio_path, language, max_ms, audio_seconds):
    # Single-file and planner-window jobs share this path; the Qt protocol and
    # its resume offsets stay unchanged. Probe even single-file jobs, whose old
    # duration=0 disabled bounds checking and let invalid source times through.
    seconds = audio_seconds or audio_duration(config["ffmpeg"], audio_path)
    work_config = dict(config, persistent_work=True)
    def section_work(path, duration):
        result = retry_work(
            lambda: transcribe_section(work_config, path, language, max_ms, duration),
            lambda message: emit({"type": "status", "message": message}),
            "Transcription", pool=bool(pool_endpoint()) or os.environ.get("CUTPRO_POOL_MODE") == "1")
        if "active_key_index" in work_config:
            config["active_key_index"] = work_config["active_key_index"]
        return result
    if seconds <= SPEECH_SECTION_SECONDS:
        return section_work(audio_path, seconds)
    overlap = max(3.0, max_ms / 2000.0 + 0.25)
    combined = []
    with tempfile.TemporaryDirectory(prefix="cutpro-gemini-speech-") as folder:
        for index in range(math.ceil(seconds / SPEECH_SECTION_SECONDS)):
            core_start = index * SPEECH_SECTION_SECONDS
            core_end = min(seconds, core_start + SPEECH_SECTION_SECONDS)
            audio_start = max(0.0, core_start - overlap)
            audio_end = min(seconds, core_end + overlap)
            section = os.path.join(folder, "s%04d.mp3" % index)
            # The upload is already MP3. Copy its packets to avoid another lossy
            # encode of quiet speech; local timestamps can differ by one packet.
            run_ffmpeg([config["ffmpeg"], "-nostdin", "-hide_banner", "-loglevel",
                        "error", "-y", "-ss", "%.3f" % audio_start, "-i", audio_path,
                        "-t", "%.3f" % (audio_end - audio_start), "-map", "0:a:0",
                        "-c:a", "copy", "-f", "mp3", section],
                       "Could not prepare speech section")
            section_seconds = audio_duration(config["ffmpeg"], section)
            lines = section_work(section, section_seconds)
            for line in lines:
                start, end = line["start"] + audio_start, line["end"] + audio_start
                # Both sides include context, so a whole utterance belongs to
                # exactly one section according to its midpoint, not a cut word.
                midpoint = (start + end) / 2.0
                if midpoint < core_start or midpoint >= core_end:
                    continue
                combined.append({"start": start, "end": min(seconds, end),
                                 "text": line["text"]})
    return shape_segments(combined, seconds, max_ms)


def enrich_prompt(language, count):
    target = LANGUAGE_NAMES.get(language, "")
    tongue = ("Write every line in %s." % target) if target else (
        "Write every line in the same language as the input. Do not translate.")
    return (
        "You are a movie recap writer. Below are %d subtitle lines taken "
        "verbatim from a video, numbered in order. Rewrite them into a recap "
        "narration that is more engaging and meaningful to a viewer.\n"
        "%s\n"
        "Return ONLY a JSON array of exactly %d strings - one rewritten line "
        "for each numbered input, in the same order. No objects, no numbering "
        "in the output, no markdown fences.\n"
        "Rules:\n"
        "- Keep the meaning of every line. Do not drop, merge or reorder "
        "lines, and do not add new ones. The output must have exactly %d "
        "items.\n"
        "- Rephrase each line so it reads well as recap narration and carries "
        "the feeling of the moment. When the line is dialogue and the speaker "
        "is clear from context, you may name them, e.g. \"Rey: We have to "
        "run!\".\n"
        "- Keep each line roughly the same length as its input so it still "
        "fits the same on-screen time. Stay faithful - do not invent events."
        % (count, tongue, count, count))


def enrich_segments(config, segments, language):
    """Recap pass: rewrite verbatim lines into richer narration, 1:1.

    This is text-only - no audio is uploaded again - so turning Recap movie on
    costs a single small extra request per window, not a second transcription.
    The timings are the accurate ones from the transcription pass and are left
    untouched; only each line's text is replaced. If the model returns the
    wrong number of lines (or anything unparseable), the verbatim segments are
    kept as-is rather than risking a misaligned recap.
    """
    lines = [str(seg.get("text", "")) for seg in segments]
    if not any(line.strip() for line in lines):
        return segments
    numbered = "\n".join("%d. %s" % (i + 1, line)
                         for i, line in enumerate(lines))
    prompt = enrich_prompt(language, len(lines))
    model = urllib.parse.quote(config["model"], safe="-._")

    def ask(api_key):
        url = "%s/%s:generateContent?key=%s" % (
            API_ROOT, model, urllib.parse.quote(api_key, safe=""))
        payload = {
            "contents": [{"parts": [{"text": prompt + "\n\n" + numbered}]}],
            "generationConfig": {"temperature": 0.6},
        }
        result = request_json(url, payload)
        candidates = result.get("candidates") or []
        if not candidates:
            raise RuntimeError("The model returned no recap.")
        parts = (candidates[0].get("content") or {}).get("parts") or []
        return "".join(str(part.get("text", "")) for part in parts)

    try:
        reply = None
        if pool_endpoint():
            try:
                result = request_json("%s/v1beta/models/%s:generateContent" % (pool_endpoint(), model), {
                    "contents": [{"parts": [{"text": prompt + "\n\n" + numbered}]}],
                    "generationConfig": {"temperature": 0.6},
                })
                reply = "".join(str(part.get("text", "")) for part in result["candidates"][0]["content"]["parts"])
            except Exception as error:
                if isinstance(error, ProviderRequestError) and not error.retryable:
                    raise
                note("pool: recap fallback to configured keys (%s)" % error)
        if reply is None:
            if pool_endpoint() or os.environ.get("CUTPRO_POOL_MODE") == "1":
                validate_api_fallback(config)
            reply = with_key_fallback(config, ask)
        cleaned = reply.strip()
        fenced = re.search(r"```(?:json)?\s*(.*?)\s*```", cleaned, re.DOTALL)
        if fenced:
            cleaned = fenced.group(1).strip()
        start, end = cleaned.find("["), cleaned.rfind("]")
        if start < 0 or end < start:
            raise RuntimeError("recap reply was not a JSON array")
        values = json.loads(cleaned[start:end + 1])
        if not isinstance(values, list) or len(values) != len(segments):
            raise RuntimeError("recap line count did not match (%d vs %d)"
                               % (len(values) if isinstance(values, list)
                                  else -1, len(segments)))
    except Exception as error:
        note("gemini: recap enrich skipped: %s" % error)
        return segments

    enriched = []
    for seg, value in zip(segments, values):
        text = re.sub(r"\s+", " ", str(value)).strip()
        new_seg = dict(seg)
        if text:
            new_seg["text"] = text
        enriched.append(new_seg)
    return enriched


def segment_ceiling(job_limits):
    for value in (
            (job_limits or {}).get("maxMs"),
            os.environ.get("CUTPRO_SEGMENT_MAX_MS", "").strip(),
    ):
        try:
            if value not in (None, ""):
                return max(1000, int(float(value)))
        except (TypeError, ValueError):
            continue
    return DEFAULT_MAX_MS


def run_job(job_path):
    with open(job_path, "r", encoding="utf-8") as handle:
        job = json.load(handle)

    config = provider_config()
    if job.get("model"):
        config["model"] = str(job["model"])
    if job.get("ffmpeg"):
        config["ffmpeg"] = str(job["ffmpeg"])
    source = job["source"]
    language = job.get("language", "auto")
    temp_dir = job.get("tempDir") or os.path.dirname(job_path)
    windows = job.get("windows") or []
    prefetch = bool(job.get("prefetch"))
    prefetch_timeout = float(job.get("prefetchTimeoutSeconds", 240))
    max_ms = segment_ceiling(job.get("segmentation"))
    os.makedirs(temp_dir, exist_ok=True)

    def process_window(index):
        window = windows[index]
        start_ms = int(window.get("startMs", 0))
        length_ms = int(window.get("lengthMs", 0))
        lead_in_ms = int(window.get("leadInMs", 0))
        local_config = config if parallel == 1 else dict(config)
        wav = prefetched_window(temp_dir, index, prefetch_timeout) \
            if prefetch else None
        upload = os.path.join(temp_dir, "g%04d.mp3" % index)
        try:
            if wav:
                # The app already decoded this window; only the re-encode is
                # left. Its WAV belongs to the app, which drops it once the
                # segments for the window have been read.
                encode_for_upload(local_config["ffmpeg"], wav, upload)
            else:
                encode_for_upload(local_config["ffmpeg"], source, upload,
                                  start_ms - lead_in_ms,
                                  length_ms + lead_in_ms)
            audio_seconds = (length_ms + lead_in_ms) / 1000.0
            segments = transcribe_audio(local_config, upload, language, max_ms,
                                        audio_seconds)
        finally:
            try:
                os.remove(upload)
            except OSError:
                pass

        # Recap movie: rewrite this window's accurate lines into richer
        # narration in place. Text-only, so a long source is still one audio
        # upload per window, plus one small recap request per window.
        if recap_enabled() and segments:
            segments = enrich_segments(local_config, segments, language)

        return segments

    parallel = pool_parallelism(len(windows))
    if parallel > 1:
        note("pool: processing %d transcription windows concurrently" % parallel)
    with ThreadPoolExecutor(max_workers=parallel) as executor:
        pending = {}
        def submit(index):
            # Prefetch audio without moving progress past the contiguous result.
            if parallel > 1:
                emit({"type": "prefetch", "index": index})
            pending[index] = executor.submit(process_window, index)
        for index in range(min(parallel, len(windows))):
            submit(index)
        for index, window in enumerate(windows):
            start_ms = int(window.get("startMs", 0))
            emit({"type": "window", "index": index, "count": len(windows),
                  "startMs": start_ms, "parallelism": parallel})
            segments = pending.pop(index).result()
            emit({"type": "segments", "index": index, "startMs": start_ms,
                  "segments": segments})
            following = index + parallel
            if following < len(windows):
                submit(following)

    emit({"type": "done",
          "language": "" if language == "auto" else language})
    return 0


def run_single(argv):
    config = provider_config()
    if len(argv) > 2 and argv[2].strip():
        config["model"] = argv[2].strip()
    language = argv[3].strip() if len(argv) > 3 and argv[3].strip() else "auto"
    max_ms = segment_ceiling(None)
    upload = os.path.join(
        os.environ.get("TEMP") or os.environ.get("TMPDIR") or ".",
        "cutpro-gemini-%d.mp3" % os.getpid())
    try:
        # The one-pass path is a single network request with no sub-progress, so
        # the app's percentage bar cannot move. These status lines let the panel
        # show what is happening instead of a frozen 0%. They carry a "type" the
        # backend treats as status-only, so they do NOT flip the run onto the
        # windowed code path.
        emit({"type": "status", "message": "Preparing audio..."})
        encode_for_upload(config["ffmpeg"], argv[1], upload)
        emit({"type": "status", "message": "Transcribing with Gemini..."})
        segments = transcribe_audio(config, upload, language, max_ms, 0.0)
    finally:
        try:
            os.remove(upload)
        except OSError:
            pass
    if recap_enabled() and segments:
        emit({"type": "status", "message": "Writing recap..."})
        segments = enrich_segments(config, segments, language)
    emit({"text": " ".join(segment["text"] for segment in segments),
          "segments": segments,
          "language": "" if language == "auto" else language})
    return 0


def main():
    configure_utf8_streams()
    if len(sys.argv) < 2:
        emit({"error": "usage: gemini_transcribe.py MEDIA [MODEL] [LANGUAGE]"
                       " | --job JOB.json"})
        return 2
    try:
        if sys.argv[1] == "--job":
            if len(sys.argv) < 3:
                emit({"error": "usage: gemini_transcribe.py --job JOB.json"})
                return 2
            return run_job(sys.argv[2])
        return run_single(sys.argv)
    except Exception as error:
        emit({"error": str(error)})
        return 4


if __name__ == "__main__":
    raise SystemExit(main())
