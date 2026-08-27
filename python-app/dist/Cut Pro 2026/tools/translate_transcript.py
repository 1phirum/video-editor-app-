#!/usr/bin/env python3
"""Translate Cut Pro transcript segments with free or API providers."""

from __future__ import annotations

import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any, Callable

SUPPORTED_TARGETS = {"en", "zh-CN", "km", "es"}
LANGUAGE_NAMES = {"en": "English", "zh-CN": "Chinese (Simplified)", "km": "Khmer", "es": "Spanish"}
FREE_TRANSLATE_URL = "https://translate.googleapis.com/translate_a/single"
# Tabitoken is protected by Cloudflare and rejects Python urllib's default
# client fingerprint with HTTP 403 / error 1010. These normal JSON API headers
# match the request accepted by its OpenAI-compatible endpoint while keeping
# this worker dependency-free when the optional openai package is unavailable.
API_REQUEST_HEADERS = {
    "Accept": "application/json",
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/140.0 Safari/537.36 CutPro/0.1"
    ),
}

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
    provider = os.environ.get("CUTPRO_TRANSLATION_PROVIDER", "free").strip()
    model = os.environ.get("CUTPRO_TRANSLATION_MODEL", "").strip()
    base_url = os.environ.get("CUTPRO_TRANSLATION_BASE_URL", "").strip()
    configured = (os.environ.get("CUTPRO_TRANSLATION_API_KEYS", "").strip()
                  or os.environ.get("CUTPRO_TRANSLATION_API_KEY", "").strip())
    if provider == "gemini" and not configured:
        configured = (os.environ.get("GEMINI_API_KEYS", "").strip()
                      or os.environ.get("GEMINI_API_KEY", "").strip())
    if provider == "openai_compatible" and not configured:
        configured = (os.environ.get("TABITOKEN_API_KEYS", "").strip()
                      or os.environ.get("TABITOKEN_API_KEY", "").strip()
                      or os.environ.get("OPENAI_API_KEYS", "").strip()
                      or os.environ.get("OPENAI_API_KEY", "").strip()
                      or os.environ.get("API", "").strip())
    return {"provider": provider, "model": model, "base_url": base_url,
            "api_keys": split_api_keys(configured)}


def request_json(url: str, payload: dict[str, Any] | None = None,
                headers: dict[str, str] | None = None,
                timeout: int = 45) -> dict[str, Any] | list[Any]:
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


def with_key_fallback(config: dict[str, Any], operation: Callable[[str], str]) -> str:
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


def translate_free(text: str, target: str) -> str:
    query = urllib.parse.urlencode({"client": "gtx", "sl": "auto", "tl": target, "dt": "t", "q": text})
    request = urllib.request.Request(f"{FREE_TRANSLATE_URL}?{query}", headers={"User-Agent": "CutPro/0.1"})
    last_error: Exception | None = None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                payload = json.loads(response.read().decode("utf-8"))
            result = "".join(part[0] for part in payload[0] if part and part[0] is not None).strip()
            if not result:
                raise RuntimeError("The free translation service returned empty text.")
            return result
        except Exception as error:
            last_error = error
            if attempt < 2:
                time.sleep(0.5 * (attempt + 1))
    raise RuntimeError(str(last_error))


def openai_completion(config: dict[str, Any], prompt: str) -> str:
    base_url = (config["base_url"] or "https://api.openai.com/v1").rstrip("/")
    model = config["model"]
    if not model:
        raise ValueError("Choose an OpenAI-compatible model.")
    payload = {"model": model, "messages": [
        {"role": "system", "content": "You translate subtitles accurately and preserve meaning, tone, and names."},
        {"role": "user", "content": prompt}], "temperature": 0.1}
    def request_with_key(api_key: str) -> str:
        result = request_json(f"{base_url}/chat/completions", payload,
                              {"Authorization": f"Bearer {api_key}"})
        return str(result["choices"][0]["message"]["content"]).strip()
    return with_key_fallback(config, request_with_key)


def gemini_completion(config: dict[str, Any], prompt: str) -> str:
    model = config["model"]
    if not model:
        raise ValueError("Choose a Gemini model.")
    def request_with_key(api_key: str) -> str:
        url = ("https://generativelanguage.googleapis.com/v1beta/models/"
               f"{urllib.parse.quote(model, safe='-._')}:generateContent?key="
               f"{urllib.parse.quote(api_key, safe='')}")
        result = request_json(url, {"contents": [{"parts": [{"text": prompt}]}],
                                    "generationConfig": {"temperature": 0.1}})
        return "".join(str(part.get("text", "")) for part in
                       result["candidates"][0]["content"]["parts"]).strip()
    return with_key_fallback(config, request_with_key)


def completion(config: dict[str, Any], prompt: str) -> str:
    if config["provider"] == "gemini":
        return gemini_completion(config, prompt)
    if config["provider"] == "openai_compatible":
        return openai_completion(config, prompt)
    raise ValueError("The selected provider does not use an API model.")


def parse_json_array(text: str) -> list[str]:
    cleaned = text.strip()
    fenced = re.search(r"```(?:json)?\s*(.*?)\s*```", cleaned, re.DOTALL)
    if fenced:
        cleaned = fenced.group(1).strip()
    start, end = cleaned.find("["), cleaned.rfind("]")
    if start < 0 or end < start:
        raise RuntimeError("The model did not return a JSON translation array.")
    values = json.loads(cleaned[start:end + 1])
    if not isinstance(values, list) or not all(isinstance(v, str) for v in values):
        raise RuntimeError("The model returned an invalid translation array.")
    return [value.strip() for value in values]


def translate_model_batch(config: dict[str, Any], texts: list[str], target: str) -> list[str]:
    prompt = (f"Translate every item in this JSON array into {LANGUAGE_NAMES[target]}. "
              "Return only a JSON array of translated strings in the exact same order "
              "and with the exact same number of items. Do not add explanations.\n\n" +
              json.dumps(texts, ensure_ascii=False))
    values = parse_json_array(completion(config, prompt))
    if len(values) != len(texts) or any(not value for value in values):
        raise RuntimeError("The model returned the wrong number of translations.")
    return values


def translate_segments(config: dict[str, Any], texts: list[str], target: str) -> list[str]:
    if config["provider"] == "free":
        with ThreadPoolExecutor(max_workers=4) as executor:
            return list(executor.map(lambda text: translate_free(text, target), texts))
    translated: list[str] = []
    for offset in range(0, len(texts), 40):
        translated.extend(translate_model_batch(config, texts[offset:offset + 40], target))
    return translated


def test_provider(config: dict[str, Any]) -> str:
    if config["provider"] == "free":
        if not translate_free("Connection test", "es"):
            raise RuntimeError("The free translator returned no text.")
        return "Free translation service is working"
    response = completion(config, "Reply with exactly OK.")
    if not response:
        raise RuntimeError("The provider returned an empty response.")
    label = "Gemini" if config["provider"] == "gemini" else "Tabitoken"
    return f"{label} connection successful ({config['model']})"


def main() -> int:
    config = provider_config()
    if len(sys.argv) == 2 and sys.argv[1] == "--test":
        print(json.dumps({"ok": True, "message": test_provider(config)}))
        return 0
    if len(sys.argv) != 2:
        raise ValueError("Expected a transcript JSON file or --test.")
    payload: dict[str, Any] = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
    target, segments = str(payload.get("target", "")), payload.get("segments", [])
    if target not in SUPPORTED_TARGETS:
        raise ValueError("Unsupported translation language.")
    if not isinstance(segments, list) or not segments:
        raise ValueError("There are no transcript segments to translate.")
    texts = [str(segment.get("text", "")).strip() for segment in segments]
    if any(not text for text in texts):
        raise ValueError("A transcript segment has no text.")
    for segment, translated in zip(segments, translate_segments(config, texts, target), strict=True):
        segment["text"] = translated
    print(json.dumps({"segments": segments, "language": target}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False))
        raise SystemExit(1)
