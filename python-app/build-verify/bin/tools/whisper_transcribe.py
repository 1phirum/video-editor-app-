#!/usr/bin/env python3
"""Optional Whisper worker that writes JSON transcript to stdout."""
import json
import os
import sys
from contextlib import redirect_stdout


def main():
    if len(sys.argv) < 2:
        print(json.dumps({"error": "usage: whisper_transcribe.py MEDIA [MODEL] [LANGUAGE]"}))
        return 2
    try:
        import whisper
        model_name = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("CUTPRO_WHISPER_MODEL", "tiny")
        language = sys.argv[3] if len(sys.argv) > 3 else "auto"
        options = {"fp16": False, "verbose": False}
        if language and language != "auto":
            options["language"] = language
        with redirect_stdout(sys.stderr):
            result = whisper.load_model(model_name).transcribe(sys.argv[1], **options)
        segments = [
            {
                "start": s.get("start", 0),
                "end": s.get("end", 0),
                "text": s.get("text", "").strip(),
            }
            for s in result.get("segments", [])
        ]
        print(
            json.dumps(
                {"text": result.get("text", ""), "segments": segments,
                 "language": result.get("language", language)},
                ensure_ascii=False,
            )
        )
        return 0
    except Exception as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False))
        return 4


if __name__ == "__main__":
    raise SystemExit(main())
