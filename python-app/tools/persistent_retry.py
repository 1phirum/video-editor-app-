"""Retry one AI work unit without repeating already completed work."""
import http.client
import json
import math
import time
import urllib.error
from email.utils import parsedate_to_datetime


class ModelOutputError(RuntimeError):
    """The model answered, but its output cannot be used safely."""


def generation_rejection(response):
    """Return (reason, permanent) for an unusable Gemini generation.

    OTHER gives no specific rejection reason. Retry the identical request;
    explicit policy reasons still require the user to change their request.
    """
    reason = (response.get("promptFeedback") or {}).get("blockReason") or ""
    if reason not in ("", "OTHER", "BLOCK_REASON_UNSPECIFIED"):
        return reason, True
    for candidate in response.get("candidates") or []:
        finish = candidate.get("finishReason") or ""
        if finish in ("SAFETY", "RECITATION", "BLOCKLIST", "PROHIBITED_CONTENT", "SPII",
                      "IMAGE_SAFETY", "IMAGE_PROHIBITED_CONTENT", "MODEL_ARMOR"):
            return finish, True
    if reason == "OTHER":
        return reason, False
    for candidate in response.get("candidates") or []:
        if candidate.get("finishReason") == "OTHER" and not any(
                part.get("text") for part in (candidate.get("content") or {}).get("parts") or []):
            return "OTHER", False
    return "", False


def retry_after_seconds(value):
    try:
        seconds = float(value)
    except (TypeError, ValueError):
        try:
            seconds = parsedate_to_datetime(value).timestamp() - time.time()
        except (TypeError, ValueError, OverflowError):
            return 0.0
    return max(0.0, seconds) if math.isfinite(seconds) else 0.0


def failure_policy(error):
    """Walk wrapped failures; configuration and provider denials stay terminal.

    The pool's retryable=False ends its bounded HTTP attempt. Temporary status
    codes can still resume this work unit after backoff or pool cooldown.
    """
    seen = set()
    delay = 0.0
    while error is not None and id(error) not in seen:
        seen.add(id(error))
        delay = max(delay, retry_after_seconds(getattr(error, "retry_after", 0)))
        status = getattr(error, "status", None)
        if status is None and isinstance(error, urllib.error.HTTPError):
            status = error.code
            delay = max(delay, retry_after_seconds(error.headers.get("Retry-After")
                                                  if error.headers else None))
        if status is not None:
            return status in {0, 408, 429, 500, 502, 503, 504, 521, 522, 523, 524}, delay
        if isinstance(error, (ModelOutputError, json.JSONDecodeError,
                              urllib.error.URLError, http.client.HTTPException)):
            return True, delay
        error = error.__cause__
    return False, delay


def retry_work(operation, report, label, pool=False):
    """Keep a failed section/batch alive until it succeeds or is cancelled.

    These out-of-process workers are killed by Qt on Cancel, including while
    sleeping. Never retry local file preparation or the entire job here.
    """
    failures = 0
    while True:
        try:
            result = operation()
        except Exception as error:
            recoverable, provider_delay = failure_policy(error)
            if not recoverable:
                raise
            failures += 1
            delay = max(provider_delay, min(60.0, 5.0 * (2 ** min(failures - 1, 4))))
            message = "%s: waiting %ds before retry %d" % (label, math.ceil(delay), failures)
            if pool:
                message += "; waiting for a free enabled account"
            report(message)
            time.sleep(delay)
            report("%s: retry %d queued" % (label, failures))
            continue
        if failures:
            report("%s: retry succeeded; continuing" % label)
        return result
