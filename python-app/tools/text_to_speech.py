"""Generate one Microsoft neural voice file for every timed subtitle."""

import asyncio
import json
import re
import sys
import time
from pathlib import Path


VOICES = {
    "en": {"female": "en-US-JennyNeural", "male": "en-US-GuyNeural"},
    "zh": {"female": "zh-CN-XiaoxiaoNeural", "male": "zh-CN-YunxiNeural"},
    "km": {"female": "km-KH-SreymomNeural", "male": "km-KH-PisethNeural"},
}


def safe_name(text: str) -> str:
    value = re.sub(r"[^\w\- ]+", " ", text, flags=re.UNICODE).strip()
    return (value[:48].strip() or "voice")


async def synthesize(text: str, voice: str, output: Path) -> None:
    import edge_tts

    await edge_tts.Communicate(text, voice).save(str(output))


async def synthesize_segments(segments, voice: str, output_dir: Path):
    outputs = []
    total = len(segments)
    for index, segment in enumerate(segments):
        text = str(segment.get("text", "")).strip()
        start_ms = int(segment.get("startMs", 0) or 0)
        end_ms = int(segment.get("endMs", 0) or 0)
        if not text or end_ms <= start_ms:
            continue
        output = output_dir / (
            f"subtitle-{index:04d}-{safe_name(text)}-{time.time_ns()}.mp3"
        )
        await synthesize(text, voice, output)
        outputs.append({
            "path": str(output),
            "startMs": start_ms,
            "endMs": end_ms,
            "text": text,
            "index": index,
        })
        print(f"PROGRESS {min(0.95, (index + 1) / max(1, total) * 0.95):.4f}", flush=True)
    return outputs


def main() -> int:
    if len(sys.argv) != 3:
        print(json.dumps({"ok": False, "error": "Invalid text-to-speech arguments."}))
        return 2
    request = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    segments = request.get("segments") or []
    language = str(request.get("language", ""))
    gender = str(request.get("gender", ""))
    output_dir = sys.argv[2]
    language = language.lower()
    gender = gender.lower()
    voice = VOICES.get(language, {}).get(gender)
    if not voice:
        print(json.dumps({"ok": False, "error": "Unsupported language or voice."}))
        return 2
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=True)
    try:
        outputs = asyncio.run(synthesize_segments(segments, voice, directory))
        if not outputs:
            print(json.dumps({"ok": False, "error": "No valid subtitle segments."}), flush=True)
            return 1
        print(json.dumps({"ok": True, "outputs": outputs}), flush=True)
        return 0
    except ModuleNotFoundError:
        print(json.dumps({
            "ok": False,
            "error": "edge-tts is not installed for the configured Python. Run: python -m pip install edge-tts",
        }), flush=True)
        return 1
    except Exception as exc:  # edge-tts reports network and service errors here.
        print(json.dumps({"ok": False, "error": str(exc)}), flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
