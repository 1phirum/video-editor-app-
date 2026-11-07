#!/usr/bin/env python3
"""Write a narration recap of a Cut Pro transcript with Gemini.

Same shape and line protocol as translate_transcript.py so nothing new has to be
configured: it reads the CUTPRO_TRANSLATION_* environment the app already sets for
the Gemini translator (the provider is forced to "gemini" here), and prints one
JSON object to stdout:

  recap_transcript.py JOB.json
      {"recap": "<prose>", "language": "en"}   on success
      {"error": "..."}                          on failure (exit code 1)

The "recap" here is NOT a story summary. It is the same subtitle lines, lightly
polished: each line is kept, in order, one line out per line in, with only a few
words added where a phrase is too terse or broken so it reads more clearly. The
model does not retell, summarize or explain the movie. Output is plain text, one
line per input line, in the transcript's own language - no timestamps, no JSON,
no markdown.

Multi-hour sources produce transcripts far larger than a model will process in one
reply, so the text is cut into windows; each window is polished on its own and the
results are concatenated in order (no stitching pass - lines stay independent).
"""

from __future__ import annotations

import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Callable

LANGUAGE_NAMES = {
    "en": "English", "zh-CN": "Chinese (Simplified)", "zh": "Chinese",
    "km": "Khmer", "es": "Spanish", "fr": "French", "de": "German",
    "ja": "Japanese", "ko": "Korean", "vi": "Vietnamese", "th": "Thai",
    "id": "Indonesian", "pt": "Portuguese", "ru": "Russian", "it": "Italian",
    "hi": "Hindi", "ar": "Arabic",
}

API_REQUEST_HEADERS = {
    "Accept": "application/json",
    "User-Agent": "CutPro/0.1",
}

# How much transcript text goes into one recap request. A window has to leave the
# model room to both read the source and write a few paragraphs back; ~12k chars
# is comfortably inside that on the flash tiers the app defaults to.
WINDOW_CHARS = 12000

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


def load_env_file() -> None:
    script = Path(__file__).resolve()
    candidates = [Path.cwd() / ".env"] + [parent / ".env" for parent in script.parents[:4]]
    for path in candidates:
        if not path.is_file():
            continue
        for raw_line in path.read_text(encoding="utf-8-sig").splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))
        return


class ProviderRequestError(RuntimeError):
    def __init__(self, message: str, status: int = 0):
        super().__init__(message)
        self.status = status


def split_api_keys(value: str) -> list[str]:
    keys: list[str] = []
    for candidate in re.split(r"[,;\r\n]+", value):
        key = candidate.strip()
        if key and key not in keys:
            keys.append(key)
    return keys


def provider_config() -> dict[str, Any]:
    load_env_file()
    model = os.environ.get("CUTPRO_TRANSLATION_MODEL", "").strip()
    configured = (os.environ.get("CUTPRO_TRANSLATION_API_KEYS", "").strip()
                  or os.environ.get("CUTPRO_TRANSLATION_API_KEY", "").strip()
                  or os.environ.get("GEMINI_API_KEYS", "").strip()
                  or os.environ.get("GEMINI_API_KEY", "").strip())
    if not model:
        model = "gemini-2.5-flash"
    return {"model": model, "api_keys": split_api_keys(configured)}


def request_json(url: str, payload: dict[str, Any] | None = None,
                 headers: dict[str, str] | None = None,
                 timeout: int = 90) -> dict[str, Any] | list[Any]:
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url, data=data,
        headers={"Content-Type": "application/json", **API_REQUEST_HEADERS,
                 **(headers or {})},
        method="POST" if data is not None else "GET")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace").strip()
        try:
            parsed = json.loads(detail)
            detail = str(parsed.get("error", parsed))
        except Exception:
            pass
        raise ProviderRequestError(
            f"API request failed ({error.code}): {detail[:500]}", error.code
        ) from error
    except (urllib.error.URLError, TimeoutError) as error:
        raise ProviderRequestError(f"API connection failed: {error}", 0) from error


def can_rotate_key(error: Exception) -> bool:
    if isinstance(error, ProviderRequestError):
        if error.status in {401, 402, 403, 408, 409, 429, 500, 502, 503, 504}:
            return True
    message = str(error).lower()
    return any(marker in message for marker in (
        "rate limit", "rate-limit", "quota", "resource_exhausted",
        "billing", "insufficient", "credit", "token limit",
        "temporarily unavailable", "overloaded", "invalid api key",
        "api key not valid", "unauthorized",
    ))


def is_transient(error: Exception) -> bool:
    if isinstance(error, ProviderRequestError):
        if error.status in {429, 500, 502, 503, 504}:
            return True
    message = str(error).lower()
    return any(marker in message for marker in (
        "unavailable", "overloaded", "high demand", "try again",
        "temporarily", "deadline", "timeout", "503", "500",
    ))


def retry_transient(operation: Callable[[], Any], attempts: int = 3) -> Any:
    last_error: Exception | None = None
    for attempt in range(attempts):
        try:
            return operation()
        except Exception as error:
            last_error = error
            if attempt < attempts - 1 and is_transient(error):
                time.sleep(1.5 * (attempt + 1))
                continue
            raise
    if last_error:
        raise last_error


def with_key_fallback(config: dict[str, Any], operation: Callable[[str], Any]) -> Any:
    keys = config.get("api_keys", [])
    if not keys:
        raise ValueError("The API key is missing.")
    failures: list[str] = []
    start_index = int(config.get("active_key_index", 0)) % len(keys)
    indexes = list(range(start_index, len(keys))) + list(range(0, start_index))
    for attempt, index in enumerate(indexes):
        key = keys[index]
        try:
            result = operation(key)
            config["active_key_index"] = index
            return result
        except Exception as error:
            safe_error = str(error)
            for configured_key in keys:
                safe_error = safe_error.replace(configured_key, "<hidden>")
            failures.append(f"Key {index + 1}: {safe_error}")
            if not can_rotate_key(error) or attempt == len(indexes) - 1:
                raise RuntimeError("; ".join(failures)) from error
    raise RuntimeError("Every configured API key failed.")


def gemini_completion(config: dict[str, Any], prompt: str) -> str:
    model = config["model"]
    if not model:
        raise ValueError("Choose a Gemini model.")

    def request_with_key(api_key: str) -> str:
        url = ("https://generativelanguage.googleapis.com/v1beta/models/"
               f"{urllib.parse.quote(model, safe='-._')}:generateContent?key="
               f"{urllib.parse.quote(api_key, safe='')}")

        def call() -> dict[str, Any]:
            # A recap is meant to read well, not to be deterministic, so the
            # temperature is higher than the translator's near-zero.
            return request_json(url, {"contents": [{"parts": [{"text": prompt}]}],
                                      "generationConfig": {"temperature": 0.6}})

        result = retry_transient(call)
        return "".join(str(part.get("text", "")) for part in
                       result["candidates"][0]["content"]["parts"]).strip()

    return with_key_fallback(config, request_with_key)


def strip_fences(text: str) -> str:
    """A model occasionally wraps prose in a ``` block even when asked not to."""
    cleaned = text.strip()
    fenced = re.search(r"```(?:[a-zA-Z]*)?\s*(.*?)\s*```", cleaned, re.DOTALL)
    if fenced:
        return fenced.group(1).strip()
    return cleaned


def language_name(code: str) -> str:
    return LANGUAGE_NAMES.get(code) or LANGUAGE_NAMES.get(
        code.split("-")[0]) or "the same language as the transcript"


def split_windows(text: str, limit: int = WINDOW_CHARS) -> list[str]:
    """Cut the transcript into windows on line boundaries so a quoted line is
    never severed mid-sentence."""
    if len(text) <= limit:
        return [text]
    windows: list[str] = []
    current: list[str] = []
    size = 0
    for line in text.splitlines():
        piece = len(line) + 1
        if size + piece > limit and current:
            windows.append("\n".join(current))
            current, size = [], 0
        current.append(line)
        size += piece
    if current:
        windows.append("\n".join(current))
    return windows


def polish_window(config: dict[str, Any], text: str, language: str,
                  title: str) -> str:
    context = f' The video is titled "{title}".' if title else ""
    prompt = (
        f"Below are subtitle lines in {language_name(language)}, one per line, in "
        f"order.{context}\n\n"
        "Rewrite each line so it is clear, natural and easy to read while staying "
        "faithful to what the line says. Fix broken or awkward phrasing, and add a "
        "few words only where a line is too short or unclear to make sense on its "
        "own. Keep it simple and accurate: do NOT summarize, do NOT retell or "
        "explain the story, do NOT add commentary, do NOT merge or split lines, and "
        "do NOT invent anything. Return exactly one rewritten line for each input "
        "line, in the same order. Output only the lines - no numbering, no "
        "timestamps, no speaker names, no quotation marks, no markdown.\n\n"
        "SUBTITLE LINES:\n" + text
    )
    return strip_fences(gemini_completion(config, prompt))


def build_recap(config: dict[str, Any], text: str, language: str, title: str) -> str:
    windows = split_windows(text)
    # Each line is polished on its own terms, so windows are independent: polish
    # each and concatenate in order. No stitching pass - there is no narrative
    # through-line to smooth over.
    polished = [polish_window(config, window, language, title)
                for window in windows]
    return "\n".join(part for part in polished if part).strip()


def main() -> int:
    config = provider_config()
    if len(sys.argv) != 2:
        raise ValueError("Expected a transcript JSON file.")
    payload: dict[str, Any] = json.loads(
        Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
    segments = payload.get("segments", [])
    language = str(payload.get("language", "") or "en").strip() or "en"
    title = str(payload.get("title", "") or "").strip()
    if not isinstance(segments, list) or not segments:
        raise ValueError("There is no transcript to recap.")
    lines = [str(segment.get("text", "")).strip()
             for segment in segments if str(segment.get("text", "")).strip()]
    if not lines:
        raise ValueError("The transcript has no text to recap.")
    recap = build_recap(config, "\n".join(lines), language, title)
    if not recap:
        raise RuntimeError("The model returned an empty recap.")
    print(json.dumps({"recap": recap, "language": language}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False))
        raise SystemExit(1)
