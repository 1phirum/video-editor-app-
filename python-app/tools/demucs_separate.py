#!/usr/bin/env python3
"""Separate vocals from media with Demucs and keep the accompaniment stem."""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

EXCEPTION_LINE = re.compile(r"^[\w.]*(?:Error|Exception)\b")


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


def child_environment() -> dict[str, str]:
    """Return an environment that keeps the Demucs child on UTF-8 streams.

    Demucs prints the track path it is working on. When the app launches this
    worker there is no console, so the child's stdout is a pipe and Python
    picks the ANSI code page for it - cp1252 on a Western Windows install.
    A Japanese, Khmer or Chinese filename then raises UnicodeEncodeError and
    Demucs dies before separating anything. Popen's own encoding= argument only
    decodes this side of the pipe, so the child has to be told in its
    environment instead.
    """
    environment = dict(os.environ)
    environment["PYTHONIOENCODING"] = "utf-8"
    environment["PYTHONUTF8"] = "1"
    return environment


def demucs_failure(lines: list[str]) -> str:
    """Pick the line that explains a failure, not just the last one printed.

    Demucs keeps printing after an exception unwinds, so the final line is
    usually a progress bar or the output path - neither of which tells the
    caller anything. The exception is what a caller can act on.
    """
    for line in reversed(lines):
        if EXCEPTION_LINE.match(line.strip()):
            return line.strip()
    for line in reversed(lines):
        if "%|" not in line and line.strip():
            return line.strip()
    return "Demucs failed."


def main() -> int:
    configure_utf8_streams()
    parser = argparse.ArgumentParser(
        description="Separate vocals with Demucs and return the accompaniment stem."
    )
    parser.add_argument("media", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument(
        "--model",
        default=os.environ.get("CUTPRO_DEMUCS_MODEL", "htdemucs_ft"),
        help="Demucs model name (default: htdemucs_ft, the fine-tuned "
             "4-model ensemble - noticeably cleaner separation than plain "
             "htdemucs on speech-heavy sources).",
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
        # Demucs shells out to ffmpeg/ffprobe to read the source. Those
        # grandchildren would each flash their own console window on Windows -
        # our own CREATE_NO_WINDOW only covers the demucs process itself, not
        # what it spawns. So run demucs through a bootstrap that patches
        # subprocess.Popen inside that process to force CREATE_NO_WINDOW on
        # everything it launches. runpy runs demucs.separate exactly as
        # "-m demucs.separate" would (as __main__), reading its args from argv.
        bootstrap = (
            "import os, subprocess, runpy\n"
            "if os.name == 'nt':\n"
            "    _orig = subprocess.Popen.__init__\n"
            "    def _patched(self, *a, **k):\n"
            "        k['creationflags'] = k.get('creationflags', 0) | 0x08000000\n"
            "        _orig(self, *a, **k)\n"
            "    subprocess.Popen.__init__ = _patched\n"
            "runpy.run_module('demucs.separate', run_name='__main__', alter_sys=True)\n"
        )
        command = [
            sys.executable,
            "-c",
            bootstrap,
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
            env=child_environment(),
            # Launched from the GUI under pythonw.exe, so this child would flash
            # its own console window without this flag (Windows only).
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
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
            return emit_error(demucs_failure(output_lines))

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
