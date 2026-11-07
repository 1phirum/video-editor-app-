#!/usr/bin/env python3
"""Separate vocals from media with MDX-Net (audio-separator / ONNX) and keep the
accompaniment (instrumental) stem.

Same command-line and stdout contract as demucs_separate.py so the C++ side can
drive either engine through one code path:
    argv: <media> <output_dir>
    on success: prints one JSON object with an "accompaniment" path and copies
                the instrumental to <output_dir>/no_vocals.wav (and vocals to
                vocals.wav when produced).
    on failure: prints {"error": "..."} to stdout and stderr, exits non-zero.

MDX-Net runs on ONNX Runtime (CPU) rather than torch, so it is markedly lighter
and faster than Demucs on a weak machine, with quality close to it for
vocal/instrumental splits.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# The model file is fetched once by audio-separator and cached in its model dir.
# Inst_HQ_3 is a strong, CPU-friendly vocal/instrumental MDX-Net model.
MODEL_NAME = os.environ.get("CUTPRO_MDXNET_MODEL", "UVR-MDX-NET-Inst_HQ_3.onnx")

# Speed knobs (env-overridable so they can be tuned without a rebuild - this
# worker is a loose .py deployed next to the exe). MDX cost is dominated by how
# many overlapping windows it runs: audio-separator defaults overlap to 0.25
# (step 0.75). Dropping it to 0.05 (step 0.95) runs ~20% fewer windows for a
# vocal/instrumental split with no audible edge artefacts. segment_size is left
# at the model default (256): raising it is faster but uses more RAM per
# inference, which the weak machines MDX targets can least afford. batch_size
# stays 1 on CPU (higher only helps a GPU and costs RAM).
MDX_OVERLAP = float(os.environ.get("CUTPRO_MDXNET_OVERLAP", "0.05"))
MDX_SEGMENT = int(os.environ.get("CUTPRO_MDXNET_SEGMENT", "256"))
MDX_BATCH = int(os.environ.get("CUTPRO_MDXNET_BATCH", "1"))


def configure_utf8_streams() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            reconfigure(encoding="utf-8", errors="replace")


def emit_error(message: str) -> int:
    payload = json.dumps({"error": message}, ensure_ascii=False)
    print(payload)
    print(payload, file=sys.stderr, flush=True)
    return 1


def ffmpeg_binary() -> str:
    """The bundled ffmpeg the app passes as CUTPRO_FFMPEG, else whatever is on PATH."""
    return os.environ.get("CUTPRO_FFMPEG") or shutil.which("ffmpeg") or "ffmpeg"


def ensure_ffmpeg_on_path() -> None:
    """Put the bundled ffmpeg's folder on PATH so audio-separator's own shell-outs
    to ffmpeg resolve even when it is not otherwise on the system PATH."""
    ffmpeg = os.environ.get("CUTPRO_FFMPEG")
    if ffmpeg and os.path.isfile(ffmpeg):
        os.environ["PATH"] = (
            os.path.dirname(ffmpeg) + os.pathsep + os.environ.get("PATH", "")
        )


def decode_to_wav(media: Path, work_dir: Path) -> Path | None:
    """Decode the source to a plain 44.1 kHz stereo WAV with ffmpeg.

    audio-separator reads audio through libsndfile, which cannot open a video
    container (.mp4/.mkv/.mov/...) and reports "Format not recognised". Handing
    it a WAV sidesteps that: ffmpeg decodes any container Demucs' torchaudio path
    can. The WAV lands in a "src" subdir so it is never mistaken for a produced
    stem. Returns the WAV path, or None if decoding was not possible so the
    caller can fall back to the media file itself.
    """
    src_dir = work_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)
    wav = src_dir / "source.wav"
    try:
        subprocess.run(
            [
                ffmpeg_binary(), "-y", "-i", str(media),
                "-vn", "-ac", "2", "-ar", "44100", "-c:a", "pcm_s16le", str(wav),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
    except Exception:  # noqa: BLE001 - fall back to the original media path
        return None
    return wav if wav.is_file() else None


def patch_no_window() -> None:
    """Force CREATE_NO_WINDOW on every child process this worker spawns.

    audio-separator shells out to ffmpeg to read/convert audio. Launched from the
    GUI under pythonw.exe there is no console, so each ffmpeg child would flash
    its own black window. Patching Popen here covers them all (Windows only).
    """
    if os.name != "nt":
        return
    original = subprocess.Popen.__init__

    def patched(self, *args, **kwargs):  # type: ignore[no-untyped-def]
        kwargs["creationflags"] = kwargs.get("creationflags", 0) | 0x08000000
        original(self, *args, **kwargs)

    subprocess.Popen.__init__ = patched  # type: ignore[assignment]


def patch_progress_reporting() -> None:
    """Make MDX-Net report real progress to the GUI.

    audio-separator shows its separation progress with a tqdm bar, but tqdm
    auto-hides on a non-TTY pipe, so launched under the GUI it prints nothing and
    the C++ progress bar (which scrapes 'NN%' from stderr, exactly like it does
    for Demucs' tqdm output) has no number to show and stalls at its synthetic
    warm-up cap. Force tqdm on and emit a plain 'NN%' line on each step. Any
    failure here is swallowed - progress reporting must never break separation.
    Must run before audio_separator is imported so its `from tqdm import tqdm`
    binds the wrapped class.
    """
    try:
        import tqdm as tqdm_mod

        base = tqdm_mod.std.tqdm

        class EmitTqdm(base):  # type: ignore[misc, valid-type]
            def __init__(self, *a, **k):
                k["disable"] = False  # override tqdm's non-TTY auto-disable
                self._last_pct = -1
                super().__init__(*a, **k)

            def update(self, n=1):
                result = super().update(n)
                try:
                    if self.total:
                        pct = int(self.n * 100 / self.total)
                        if pct != self._last_pct:
                            self._last_pct = pct
                            print(f"{pct}%", file=sys.stderr, flush=True)
                except Exception:  # noqa: BLE001
                    pass
                return result

        # Cover every name audio-separator might import tqdm under.
        tqdm_mod.tqdm = EmitTqdm
        tqdm_mod.std.tqdm = EmitTqdm
        try:
            tqdm_mod.auto.tqdm = EmitTqdm
        except Exception:  # noqa: BLE001 - tqdm.auto may not be importable
            pass
    except Exception:  # noqa: BLE001 - never let progress plumbing stop the run
        return


def is_vocal_name(name: str) -> bool:
    lowered = name.lower()
    return "(vocals)" in lowered or "_vocals" in lowered or "vocals." in lowered


def main() -> int:
    configure_utf8_streams()
    patch_no_window()
    ensure_ffmpeg_on_path()
    patch_progress_reporting()

    if len(sys.argv) < 3:
        return emit_error("Usage: mdxnet_separate.py <media> <output_dir>")

    media = Path(sys.argv[1]).resolve()
    output_dir = Path(sys.argv[2]).resolve()
    if not media.is_file():
        return emit_error(f"Media file does not exist: {media}")

    try:
        from audio_separator.separator import Separator
    except Exception as exc:  # noqa: BLE001 - report any import failure cleanly
        return emit_error(
            "MDX-Net engine is not installed. Open Settings > Requirements and "
            f"click Download next to MDX-Net. ({exc})"
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cutpro-mdxnet-") as temporary:
        temporary_dir = Path(temporary)
        print("Loading MDX-Net model...", file=sys.stderr, flush=True)
        try:
            # audio-separator otherwise caps the output peak at 0.9 and never
            # boosts a quiet stem, so its instrumental comes out noticeably
            # quieter than Demucs. Raise the ceiling to 0.99 and amplify any
            # stem below 0.97 up to 0.97: this is pure gain (scale factor on the
            # samples), so it makes the background as loud as Demucs without
            # clipping and without any loss of quality.
            # amplification_threshold=0.97, plus mdx_params speed knobs (see the
            # module constants). mdx_params is a newer audio-separator kwarg, so
            # fall back to the plain constructor if this build predates it.
            common = dict(
                output_dir=str(temporary_dir),
                normalization_threshold=0.99,
                amplification_threshold=0.97,
            )
            mdx_params = {
                "hop_length": 1024,
                "segment_size": MDX_SEGMENT,
                "overlap": MDX_OVERLAP,
                "batch_size": MDX_BATCH,
                "enable_denoise": False,
            }
            try:
                separator = Separator(mdx_params=mdx_params, **common)
            except TypeError:
                separator = Separator(**common)
            separator.load_model(model_filename=MODEL_NAME)
            print("Separating vocals...", file=sys.stderr, flush=True)
            # Feed the separator a decoded WAV rather than the raw container:
            # its libsndfile reader cannot open .mp4 and friends. Fall back to
            # the media path if ffmpeg is somehow unavailable.
            source = decode_to_wav(media, temporary_dir) or media
            outputs = separator.separate(str(source))
        except Exception as exc:  # noqa: BLE001
            return emit_error(f"MDX-Net separation failed: {exc}")

        # audio-separator returns the produced file names (relative to output_dir).
        produced = [temporary_dir / Path(name).name for name in (outputs or [])]
        produced = [p for p in produced if p.is_file()]
        if not produced:
            # Fall back to whatever landed in the temp dir.
            produced = [p for p in temporary_dir.iterdir() if p.is_file()]
        if not produced:
            return emit_error("MDX-Net completed but produced no output files.")

        vocals = next((p for p in produced if is_vocal_name(p.name)), None)
        instrumental = next((p for p in produced if p is not vocals), None)
        if instrumental is None:
            return emit_error(
                "MDX-Net produced no instrumental stem to keep."
            )

        output_path = output_dir / "no_vocals.wav"
        shutil.copy2(instrumental, output_path)
        vocals_path = output_dir / "vocals.wav"
        if vocals is not None and vocals.is_file():
            shutil.copy2(vocals, vocals_path)

    print(
        json.dumps(
            {
                "input": str(media),
                "model": MODEL_NAME,
                "engine": "mdxnet",
                "accompaniment": str(output_path),
                "vocals": str(vocals_path) if vocals_path.is_file() else None,
            },
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
