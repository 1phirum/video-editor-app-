#!/usr/bin/env python3
"""Translate Cut Pro transcript segments while preserving SRT timing."""

from __future__ import annotations

import json
import sys
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any


SUPPORTED_TARGETS = {"en", "zh-CN", "km", "es"}
TRANSLATE_URL = "https://translate.googleapis.com/translate_a/single"

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


def translate_text(text: str, target: str) -> str:
    query = urllib.parse.urlencode(
        {"client": "gtx", "sl": "auto", "tl": target, "dt": "t", "q": text}
    )
    request = urllib.request.Request(
        f"{TRANSLATE_URL}?{query}",
        headers={"User-Agent": "CutPro/0.1"},
    )
    last_error: Exception | None = None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                payload = json.loads(response.read().decode("utf-8"))
            translated = "".join(
                part[0] for part in payload[0] if part and part[0] is not None
            ).strip()
            if not translated:
                raise RuntimeError("The translation service returned empty text.")
            return translated
        except Exception as error:  # Network errors vary by platform.
            last_error = error
            if attempt < 2:
                time.sleep(0.5 * (attempt + 1))
    raise RuntimeError(str(last_error))


def main() -> int:
    if len(sys.argv) != 2:
        raise ValueError("Expected a transcript JSON file.")
    payload: dict[str, Any] = json.loads(
        Path(sys.argv[1]).read_text(encoding="utf-8-sig")
    )
    target = str(payload.get("target", ""))
    segments = payload.get("segments", [])
    if target not in SUPPORTED_TARGETS:
        raise ValueError("Unsupported translation language.")
    if not isinstance(segments, list) or not segments:
        raise ValueError("There are no transcript segments to translate.")

    texts = [str(segment.get("text", "")).strip() for segment in segments]
    if any(not text for text in texts):
        raise ValueError("A transcript segment has no text.")
    with ThreadPoolExecutor(max_workers=4) as executor:
        translated_texts = list(
            executor.map(lambda text: translate_text(text, target), texts)
        )
    for segment, translated in zip(segments, translated_texts, strict=True):
        segment["text"] = translated

    print(json.dumps({"segments": segments, "language": target}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False))
        raise SystemExit(1)
