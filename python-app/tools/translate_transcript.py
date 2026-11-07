#!/usr/bin/env python3
"""Translate Cut Pro transcript segments with free or API providers."""

from __future__ import annotations

import http.client
import json
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, as_completed, wait
from pathlib import Path
from typing import Any, Callable

# Windows embedded Python does not include the script directory in sys.path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from persistent_retry import ModelOutputError, failure_policy, generation_rejection, retry_after_seconds, retry_work

_STATUS_LOCK = threading.Lock()

def report_status(message):
    # Parallel batch retries must emit whole protocol lines.
    with _STATUS_LOCK:
        print(json.dumps({"type": "status", "message": message}), flush=True)

def report_progress(offset: int, texts: list[str]) -> None:
    with _STATUS_LOCK:
        print(json.dumps({"type": "progress", "offset": offset, "texts": texts},
                         ensure_ascii=False), flush=True)

LANGUAGE_NAMES = {
    "en": "English", "zh-CN": "Chinese (Simplified)", "km": "Khmer",
    "es": "Spanish", "fr": "French", "de": "German", "ja": "Japanese",
    "ko": "Korean", "vi": "Vietnamese", "th": "Thai", "id": "Indonesian",
    "pt": "Portuguese", "ru": "Russian", "it": "Italian", "hi": "Hindi",
    "ar": "Arabic",
}
SUPPORTED_TARGETS = set(LANGUAGE_NAMES)
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
# The endpoint is whatever the user pasted, so its display name is derived from
# the host instead of being hard-coded to one vendor. These are only the
# spellings a plain title-case of the host would get wrong.
KNOWN_PROVIDER_NAMES = {
    "openai": "OpenAI",
    "openrouter": "OpenRouter",
    "tabitoken": "Tabitoken",
    "gorouter": "GoRouter",
    "deepseek": "DeepSeek",
    "deepinfra": "DeepInfra",
    "siliconflow": "SiliconFlow",
    "bigmodel": "Zhipu BigModel",
    "dashscope": "Alibaba DashScope",
    "fireworks": "Fireworks AI",
    "together": "Together AI",
    "openrouterai": "OpenRouter",
    "anthropic": "Anthropic",
    "mistral": "Mistral",
    "moonshot": "Moonshot",
    "perplexity": "Perplexity",
    "groq": "Groq",
    "xai": "xAI",
    "localhost": "Local server",
    "127": "Local server",
}
LOCAL_HOSTS = {"localhost", "127.0.0.1", "0.0.0.0", "::1", "[::1]"}

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
    def __init__(self, message: str, status: int = 0, retryable: bool = True, retry_after: float = 0):
        super().__init__(message)
        self.status = status
        self.retryable = retryable
        self.retry_after = retry_after


def split_api_keys(value: str) -> list[str]:
    keys: list[str] = []
    for candidate in re.split(r"[,;\r\n]+", value):
        key = candidate.strip()
        if key and key not in keys:
            keys.append(key)
    return keys


def normalize_base_url(value: str) -> str:
    """Make what a user actually pastes into something urllib can open.

    A base URL copied out of a provider's docs arrives as often without a scheme
    ("gorouter.app/v1") as with one, and with or without a trailing slash. Both
    used to fail at the request, which read as "this app only works with one
    website"."""
    url = (value or "").strip().rstrip("/")
    if not url:
        return ""
    if "://" not in url:
        host = url.split("/", 1)[0].split(":", 1)[0].lower()
        url = ("http://" if host in LOCAL_HOSTS else "https://") + url
    return url


def provider_label(base_url: str) -> str:
    """The name shown for the endpoint, taken from its host."""
    normalized = normalize_base_url(base_url)
    if not normalized:
        return "Custom provider"
    host = (urllib.parse.urlsplit(normalized).hostname or "").lower()
    if host.startswith("www."):
        host = host[4:]
    # A gateway host is usually api.<brand>.<tld>; the brand is the interesting
    # part. Only stripped when something is left behind that still has a domain.
    for prefix in ("api.", "gateway.", "llm.", "chat.", "ai."):
        if host.startswith(prefix) and host.count(".") > 1:
            host = host[len(prefix):]
            break
    label = host.split(".")[0]
    if not label:
        return "Custom provider"
    return KNOWN_PROVIDER_NAMES.get(label, label[:1].upper() + label[1:])


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
    return {"provider": provider, "model": model,
            "base_url": normalize_base_url(base_url),
            "api_keys": split_api_keys(configured)}


def clean_http_detail(detail: str, status: int) -> str:
    """A human-readable reason from an error body, not a page of HTML.

    An OpenAI-compatible server returns JSON on error and its "error" message is
    what matters. But a gateway behind Cloudflare (or one whose origin is down)
    answers with an HTML page instead - a 521 "web server is down", a 403 bot
    challenge - and dumping that markup into the status bar tells the user
    nothing. Turn the common cases into one plain sentence so the failure reads
    as "their server", not "your settings".
    """
    text = (detail or "").strip()
    if not text:
        return "the endpoint returned no details."
    try:
        parsed = json.loads(text)
        if isinstance(parsed, dict):
            return str(parsed.get("error", parsed))[:500]
        return str(parsed)[:500]
    except Exception:
        pass
    # Not JSON. If it is HTML, it is an edge/firewall page, not an API reply.
    lowered = text.lower()
    if lowered.startswith("<!doctype") or lowered.startswith("<html") \
            or "<title" in lowered or "cloudflare" in lowered:
        if status in (521, 522, 523, 524):
            return ("the endpoint's origin server is down or unreachable "
                    "(Cloudflare %d). This is the provider's server, not your "
                    "settings - try again later or use another endpoint."
                    % status)
        if status in (403, 503):
            return ("the endpoint blocked the request with a firewall / bot "
                    "challenge page (HTTP %d). The server is protected against "
                    "API clients, or the key is not allowed." % status)
        return ("the endpoint returned a web page, not an API response "
                "(HTTP %d). Check the Base URL points at the API (usually "
                "ending in /v1)." % status)
    return text[:500]


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
        retryable = True
        retry_after = retry_after_seconds(error.headers.get("Retry-After") if error.headers else None)
        try:
            parsed_error = json.loads(detail).get("error") or {}
            retryable = parsed_error.get("retryable") is not False
            retry_after = max(retry_after, retry_after_seconds(parsed_error.get("retryAfterSeconds")))
        except (ValueError, AttributeError):
            pass
        raise ProviderRequestError(
            f"API request failed ({error.code}): "
            f"{clean_http_detail(detail, error.code)}", error.code, retryable, retry_after
        ) from error
    except (urllib.error.URLError, OSError, http.client.HTTPException) as error:
        # Dropped connections and truncated HTTP bodies can bypass URLError.
        # Keep these transport failures in the same bounded retry path as 5xx.
        raise ProviderRequestError(f"API connection failed: {error}", 0) from error


def can_rotate_key(error: Exception) -> bool:
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


def is_transient(error: Exception) -> bool:
    """A temporary provider hiccup worth retrying the SAME request for, as
    opposed to a key problem. Model overload (503 UNAVAILABLE, "high demand")
    and short rate spikes clear on their own in a second or two; rotating to a
    different API key does not help because it is the model, not the key, that
    is busy."""
    if isinstance(error, ProviderRequestError):
        if not error.retryable:
            return False
        if error.status in {0, 408, 429, 500, 502, 503, 504}:
            return True
    message = str(error).lower()
    return any(marker in message for marker in (
        "unavailable", "overloaded", "high demand", "try again",
        "temporarily", "deadline", "timeout", "503", "500",
    ))


def retry_transient(operation: Callable[[], Any], attempts: int = 3) -> Any:
    """Run operation, retrying a few times with backoff on transient errors so a
    momentary model spike does not surface as 'Translation failed'."""
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
                raise ModelOutputError("The free translation service returned empty text.")
            return result
        except Exception as error:
            last_error = error
            if attempt < 2:
                time.sleep(0.5 * (attempt + 1))
    raise RuntimeError(str(last_error)) from last_error


def openai_completion(config: dict[str, Any], prompt: str) -> str:
    base_url = normalize_base_url(config["base_url"]) or "https://api.openai.com/v1"
    model = config["model"]
    if not model:
        raise ValueError("Choose an OpenAI-compatible model.")
    payload = {"model": model, "messages": [
        {"role": "system", "content": "You translate subtitles accurately and preserve meaning, tone, and names."},
        {"role": "user", "content": prompt}], "temperature": 0.1}
    def request_with_key(api_key: str) -> str:
        result = retry_transient(lambda: request_json(
            f"{base_url}/chat/completions", payload,
            {"Authorization": f"Bearer {api_key}"}))
        return str(result["choices"][0]["message"]["content"]).strip()
    return with_key_fallback(config, request_with_key)


def gemini_text(result):
    reason, permanent = generation_rejection(result)
    if reason:
        message = ("The model declined the translation (%s)" if permanent else
                   "The model returned no translation (%s); waiting to retry") % reason
        raise ProviderRequestError(message, 422 if permanent else 503, False)
    candidates = result.get("candidates") or []
    if not candidates:
        raise ModelOutputError("The model returned no translation text.")
    return "".join(str(part.get("text", "")) for part in
                   (candidates[0].get("content") or {}).get("parts") or []).strip()


def gemini_completion(config: dict[str, Any], prompt: str) -> str:
    model = config["model"]
    if not model:
        raise ValueError("Choose a Gemini model.")
    # The account pool (when the app started its Core) speaks the identical
    # request shape and rotates signed-in Google accounts; the configured keys
    # stay as the fallback for a run the pool could not serve.
    pool = "" if os.environ.get("CUTPRO_POOL_MODE") == "0" else os.environ.get("CUTPRO_POOL_ENDPOINT", "").strip().rstrip("/")
    if config.get("persistent_work") and not pool and os.environ.get("CUTPRO_POOL_MODE") == "1":
        raise ValueError("The Account Pool address is unavailable. Start the pool and retry this job.")
    if pool:
        try:
            result = retry_transient(lambda: request_json(
                f"{pool}/v1beta/models/"
                f"{urllib.parse.quote(model, safe='-._')}:generateContent",
                {"contents": [{"parts": [{"text": prompt}]}],
                 "generationConfig": {"temperature": 0.1}}, timeout=300))
            return gemini_text(result)
        except Exception as error:
            if isinstance(error, ProviderRequestError) and not error.retryable:
                raise
            if config.get("persistent_work") and failure_policy(error)[0]:
                raise
            if not config.get("api_keys"):
                raise RuntimeError(f"Pool translation failed: {error}") from error
            print(f"pool: falling back to configured API keys ({error})",
                  file=sys.stderr)
    def request_with_key(api_key: str) -> str:
        url = ("https://generativelanguage.googleapis.com/v1beta/models/"
               f"{urllib.parse.quote(model, safe='-._')}:generateContent?key="
               f"{urllib.parse.quote(api_key, safe='')}")
        def call() -> dict[str, Any]:
            return request_json(url, {"contents": [{"parts": [{"text": prompt}]}],
                                      "generationConfig": {"temperature": 0.1}})
        # Retry the same model on a transient spike before giving up on this key.
        result = retry_transient(call)
        return gemini_text(result)
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
        raise ModelOutputError("The model did not return a JSON translation array.")
    values = json.loads(cleaned[start:end + 1])
    if not isinstance(values, list) or not all(isinstance(v, str) for v in values):
        raise ModelOutputError("The model returned an invalid translation array.")
    return [value.strip() for value in values]


def translate_model_batch(config: dict[str, Any], texts: list[str], target: str) -> list[str]:
    prompt = (f"Translate every item in this JSON array into {LANGUAGE_NAMES[target]}. "
              "Return only a JSON array of translated strings in the exact same order "
              "and with the exact same number of items. Do not add explanations.\n\n" +
              json.dumps(texts, ensure_ascii=False))
    try:
        reply = completion(config, prompt)
    except (KeyError, IndexError, TypeError) as error:
        raise ModelOutputError("The model returned no translation text.") from error
    values = parse_json_array(reply)
    if len(values) != len(texts) or any(not value for value in values):
        raise ModelOutputError("The model returned the wrong number of translations.")
    return values


def translate_segments(config: dict[str, Any], texts: list[str], target: str,
                       on_translated: Callable[[int, list[str]], None] | None = None) -> list[str]:
    translated = [""] * len(texts)

    def completed(offset: int, values: list[str]) -> None:
        translated[offset:offset + len(values)] = values
        if on_translated is not None:
            on_translated(offset, values)

    if config["provider"] == "free":
        with ThreadPoolExecutor(max_workers=4) as executor:
            pending = {executor.submit(retry_work,
                lambda text=text: translate_free(text, target), report_status, "Translation"): index
                for index, text in enumerate(texts)}
            for future in as_completed(pending):
                completed(pending[future], [future.result()])
        return translated
    batches = [texts[offset:offset + 40] for offset in range(0, len(texts), 40)]
    parallel = 1
    if config["provider"] == "gemini" and os.environ.get("CUTPRO_POOL_ENDPOINT") and os.environ.get("CUTPRO_POOL_MODE") != "0":
        try:
            parallel = max(1, min(8, len(batches), int(os.environ.get("CUTPRO_POOL_PARALLELISM", "1"))))
        except ValueError:
            pass
    if parallel > 1:
        print(f"pool: translating {parallel} batches concurrently", file=sys.stderr)
    with ThreadPoolExecutor(max_workers=parallel) as executor:
        pending = {}
        def submit(index):
            local = config if parallel == 1 else dict(config)
            local["persistent_work"] = True
            future = executor.submit(retry_work,
                lambda: translate_model_batch(local, batches[index], target),
                report_status, "Translation batch %d/%d" % (index + 1, len(batches)),
                pool=config["provider"] == "gemini" and os.environ.get("CUTPRO_POOL_MODE") != "0"
                     and bool(os.environ.get("CUTPRO_POOL_ENDPOINT") or os.environ.get("CUTPRO_POOL_MODE") == "1"))
            pending[future] = index
        for index in range(min(parallel, len(batches))):
            submit(index)
        next_index = len(pending)
        while pending:
            ready, _ = wait(pending, return_when=FIRST_COMPLETED)
            for future in ready:
                index = pending.pop(future)
                completed(index * 40, future.result())
                if next_index < len(batches):
                    submit(next_index)
                    next_index += 1
    return translated


def model_list_urls(base_url: str) -> list[str]:
    """Where an OpenAI-compatible server publishes its catalogue.

    Nearly all of them answer GET <base>/models. The second candidate covers the
    user who pasted the site root instead of the versioned path."""
    base = normalize_base_url(base_url) or "https://api.openai.com/v1"
    urls = [f"{base}/models"]
    if not re.search(r"/v\d+[a-z]*$", base):
        urls.append(f"{base}/v1/models")
    return urls


def extract_model_ids(payload: Any) -> list[str]:
    """Pull model ids out of any of the shapes these endpoints return."""
    entries: list[Any] = payload if isinstance(payload, list) else []
    if isinstance(payload, dict):
        for key in ("data", "models", "result", "items"):
            if isinstance(payload.get(key), list):
                entries = payload[key]
                break
    models: list[str] = []
    for entry in entries:
        if isinstance(entry, str):
            name = entry.strip()
        elif isinstance(entry, dict):
            # Gemini lists embedding and tuning-only models next to the chat
            # ones; a model that cannot answer a prompt is not a choice here.
            methods = entry.get("supportedGenerationMethods")
            if isinstance(methods, list) and "generateContent" not in methods:
                continue
            name = str(entry.get("id") or entry.get("name")
                       or entry.get("model") or "").strip()
        else:
            continue
        if name.startswith("models/"):
            name = name[len("models/"):]
        if name and name not in models:
            models.append(name)
    models.sort(key=str.lower)
    return models


def list_openai_models(config: dict[str, Any]) -> list[str]:
    urls = model_list_urls(config["base_url"])

    def fetch(api_key: str) -> list[str]:
        headers = {"Authorization": f"Bearer {api_key}"} if api_key else None
        last: Exception | None = None
        for url in urls:
            try:
                return extract_model_ids(request_json(url, None, headers, timeout=30))
            except ProviderRequestError as error:
                last = error
                # Only a missing catalogue is worth trying the next path for; a
                # rejected key has to reach with_key_fallback so the next key runs.
                if error.status not in {404, 405}:
                    raise
        raise last or ProviderRequestError("This endpoint publishes no model list.")

    # A local server (LM Studio, llama.cpp, Ollama) needs no key, and refusing to
    # look because none is configured would be wrong.
    if not config.get("api_keys"):
        return fetch("")
    return with_key_fallback(config, fetch)


def list_gemini_models(config: dict[str, Any]) -> list[str]:
    def fetch(api_key: str) -> list[str]:
        url = ("https://generativelanguage.googleapis.com/v1beta/models"
               f"?pageSize=200&key={urllib.parse.quote(api_key, safe='')}")
        return extract_model_ids(request_json(url, None, None, timeout=30))
    return with_key_fallback(config, fetch)


def available_models(config: dict[str, Any]) -> dict[str, Any]:
    if config["provider"] == "gemini":
        models, label, base = list_gemini_models(config), "Gemini", ""
    elif config["provider"] == "openai_compatible":
        models = list_openai_models(config)
        label = provider_label(config["base_url"])
        base = normalize_base_url(config["base_url"])
    else:
        raise ValueError("The free translator has no model list to check.")
    if not models:
        raise RuntimeError("The provider returned an empty model list.")
    return {"ok": True, "models": models, "label": label, "baseUrl": base,
            "message": f"{label}: {len(models)} model"
                       + ("s" if len(models) != 1 else "") + " available"}


def test_provider(config: dict[str, Any]) -> str:
    if config["provider"] == "free":
        if not translate_free("Connection test", "es"):
            raise RuntimeError("The free translator returned no text.")
        return "Free translation service is working"
    response = completion(config, "Reply with exactly OK.")
    if not response:
        raise RuntimeError("The provider returned an empty response.")
    label = ("Gemini" if config["provider"] == "gemini"
             else provider_label(config["base_url"]))
    return f"{label} connection successful ({config['model']})"


def main() -> int:
    config = provider_config()
    if len(sys.argv) == 2 and sys.argv[1] == "--test":
        print(json.dumps({"ok": True, "message": test_provider(config)}))
        return 0
    if len(sys.argv) == 2 and sys.argv[1] == "--models":
        print(json.dumps(available_models(config), ensure_ascii=False))
        return 0
    if len(sys.argv) != 2:
        raise ValueError("Expected a transcript JSON file, --test or --models.")
    payload: dict[str, Any] = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
    target, segments = str(payload.get("target", "")), payload.get("segments", [])
    if target not in SUPPORTED_TARGETS:
        raise ValueError("Unsupported translation language.")
    if not isinstance(segments, list) or not segments:
        raise ValueError("There are no transcript segments to translate.")
    texts = [str(segment.get("text", "")).strip() for segment in segments]
    if any(not text for text in texts):
        raise ValueError("A transcript segment has no text.")
    for segment, translated in zip(segments, translate_segments(config, texts, target, report_progress), strict=True):
        segment["text"] = translated
    print(json.dumps({"segments": segments, "language": target}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False))
        raise SystemExit(1)
