#!/usr/bin/env python3
"""Create a speech-reduced track without discarding the original sound bed.

Demucs' accompaniment stem is intentionally conservative for music, but it
can also remove room tone and other centered effects. This helper instead
subtracts the Demucs vocal estimate from the original media, preserving the
original ambience and non-vocal sound.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def ffmpeg_executable() -> str:
    configured = os.environ.get("CUTPRO_FFMPEG", "").strip()
    if configured and Path(configured).is_file():
        return configured
    sibling = Path(__file__).resolve().parent.parent / "ffmpeg.exe"
    if sibling.is_file():
        return str(sibling)
    return shutil.which("ffmpeg") or "ffmpeg"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Subtract a Demucs vocal estimate from the original audio."
    )
    parser.add_argument("media", type=Path)
    parser.add_argument("vocals", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--vocal-gain",
        type=float,
        default=float(os.environ.get("CUTPRO_VOCAL_GAIN", "1.0")),
        help="Vocal subtraction amount; 1.0 removes the estimated vocal fully.",
    )
    args = parser.parse_args()
    if not args.media.is_file():
        print(f"Original media does not exist: {args.media}", file=sys.stderr)
        return 2
    if not args.vocals.is_file():
        print(f"Vocal stem does not exist: {args.vocals}", file=sys.stderr)
        return 2
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.stem + ".tmp" + args.output.suffix)
    gain = max(0.0, min(1.25, args.vocal_gain))
    # Both inputs are normalized only to a common format. The original signal
    # remains at its native level; the vocal estimate is phase-inverted and
    # subtracted, so non-vocal ambience is retained instead of re-synthesized.
    filter_complex = (
        "[0:a]aresample=48000,aformat=sample_fmts=fltp:"
        "channel_layouts=stereo[original];"
        "[1:a]aresample=48000,aformat=sample_fmts=fltp:"
        "channel_layouts=stereo,volume=-%1f[vocal_negative];"
        "[original][vocal_negative]amix=inputs=2:duration=first:"
        "dropout_transition=0:normalize=0,alimiter=limit=0.99:"
        "attack=5:release=50[clean]"
    ) % gain
    command = [
        ffmpeg_executable(),
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-i",
        str(args.media),
        "-i",
        str(args.vocals),
        "-filter_complex",
        filter_complex,
        "-map",
        "[clean]",
        "-c:a",
        "pcm_s16le",
        "-ar",
        "48000",
        "-ac",
        "2",
        "-vn",
        str(temporary),
    ]
    try:
        completed = subprocess.run(command, capture_output=True, text=True)
    except OSError as exc:
        print(f"Could not start FFmpeg: {exc}", file=sys.stderr)
        return 1
    if completed.returncode != 0 or not temporary.is_file():
        if temporary.exists():
            temporary.unlink()
        details = completed.stderr.strip() or "FFmpeg vocal cleanup failed."
        print(details[-1200:], file=sys.stderr)
        return completed.returncode or 1
    temporary.replace(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
