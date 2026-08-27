#!/usr/bin/env python3
"""Separate vocals from media with Demucs and keep the accompaniment stem."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def configure_utf8_streams() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            reconfigure(encoding="utf-8", errors="replace")


def emit_error(message: str) -> int:
    # Keep the structured error on stdout for callers, but mirror it to
    # stderr because the Qt process monitor consumes worker diagnostics there.
    payload = json.dumps({"error": message}, ensure_ascii=False)
    print(payload)
    print(payload, file=sys.stderr, flush=True)
    return 1


def cpu_profile() -> tuple[int, int]:
    """Return conservative Torch threads and Demucs chunk workers for laptops."""
    logical = os.cpu_count() or 4
    # Two workers keep the i5 busy without exhausting the 16 GB laptop RAM.
    workers = 2 if logical >= 8 else 1
    threads = max(1, min(4, logical // workers))
    return threads, workers


def main() -> int:
    configure_utf8_streams()
    parser = argparse.ArgumentParser(
        description="Separate vocals with Demucs and return the accompaniment stem."
    )
    parser.add_argument("media", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument(
        "--model",
        default=os.environ.get("CUTPRO_DEMUCS_MODEL", "htdemucs"),
        help="Demucs model name (default: htdemucs).",
    )
    parser.add_argument(
        "--device",
        default=os.environ.get("CUTPRO_DEMUCS_DEVICE", "cpu"),
        help="Demucs device, for example cpu or cuda (default: cpu).",
    )
    args = parser.parse_args()

    media = args.media.resolve()
    output_dir = args.output_dir.resolve()
    if not media.is_file():
        return emit_error(f"Media file does not exist: {media}")
    if not args.model.strip():
        return emit_error("Demucs model cannot be empty.")
    if not args.device.strip():
        return emit_error("Demucs device cannot be empty.")

    threads, workers = cpu_profile()
    if args.device.lower() == "cpu":
        os.environ.setdefault("OMP_NUM_THREADS", str(threads))
        os.environ.setdefault("MKL_NUM_THREADS", str(threads))
        os.environ.setdefault("OPENBLAS_NUM_THREADS", str(threads))

    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cutpro-demucs-") as temporary:
        temporary_dir = Path(temporary)
        command = [
            sys.executable,
            "-m",
            "demucs.separate",
            "--name",
            args.model,
            "--device",
            args.device,
            "--shifts",
            os.environ.get("CUTPRO_DEMUCS_SHIFTS", "1"),
            "--overlap",
            os.environ.get("CUTPRO_DEMUCS_OVERLAP", "0.25"),
            "--jobs",
            str(workers if args.device.lower() == "cpu" else 0),
            "--other-method",
            "add",
            "--clip-mode",
            "rescale",
            "--two-stems",
            "vocals",
            "--out",
            str(temporary_dir),
            str(media),
        ]
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        output_lines: list[str] = []
        assert process.stdout is not None
        for line in process.stdout:
            line = line.rstrip()
            if line:
                output_lines.append(line)
                print(line, file=sys.stderr, flush=True)
        return_code = process.wait()
        if return_code != 0:
            details = output_lines[-1] if output_lines else "Demucs failed."
            return emit_error(details)

        stem_dir = temporary_dir / args.model / media.stem
        accompaniment = stem_dir / "no_vocals.wav"
        vocals = stem_dir / "vocals.wav"
        if not accompaniment.is_file():
            return emit_error("Demucs completed but no accompaniment stem was produced.")

        output_path = output_dir / "no_vocals.wav"
        vocals_path = output_dir / "vocals.wav"
        if vocals.is_file():
            shutil.copy2(vocals, vocals_path)
            # Use the model's direct accompaniment estimate. The fine-tuned
            # ensemble is substantially cleaner for speech-heavy sources than
            # phase-subtracting its vocal estimate from the original AAC.
            shutil.copy2(accompaniment, output_path)
        else:
            shutil.copy2(accompaniment, output_path)

    print(
        json.dumps(
            {
                "input": str(media),
                "model": args.model,
                "device": args.device,
                "accompaniment": str(output_path),
                "vocals": str(vocals_path) if vocals_path.is_file() else None,
            },
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
