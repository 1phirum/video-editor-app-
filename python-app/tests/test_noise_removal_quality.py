"""Prepare downloaded speech/noise fixtures, then score actual app outputs.

Run --prepare, run cutpro_noise_removal_tests with CUTPRO_NOISE_FIXTURES set to
the output directory, then run --evaluate. Requires numpy, scipy, pystoi 0.4.1.
Downloads are for local verification only; test recordings are not shipped.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
import urllib.request
from pathlib import Path

import numpy as np
from scipy.signal import correlate


def decode(ffmpeg: str, path: Path) -> np.ndarray:
    data = subprocess.check_output([
        ffmpeg, "-v", "error", "-i", str(path), "-f", "f32le",
        "-ac", "1", "-ar", "48000", "-",
    ])
    return np.frombuffer(data, dtype="<f4").astype(np.float64)


def write(ffmpeg: str, path: Path, samples: np.ndarray) -> None:
    subprocess.run([
        ffmpeg, "-v", "error", "-y", "-f", "f32le", "-ar", "48000",
        "-ac", "1", "-i", "-", "-c:a", "pcm_s24le", str(path),
    ], input=samples.astype("<f4").tobytes(), check=True)


def prepare(ffmpeg: str, root: Path) -> None:
    urls = {
        "libri1.ogg": "https://librosa.org/data/audio/5703-47212-0000.ogg",
        "libri2.ogg": "https://librosa.org/data/audio/198-209-0000.ogg",
        "esc50.csv": "https://raw.githubusercontent.com/karolpiczak/ESC-50/master/meta/esc50.csv",
    }
    for filename, url in urls.items():
        path = root / filename
        if not path.exists():
            with urllib.request.urlopen(url, timeout=60) as response:
                path.write_bytes(response.read())
    rows = list(csv.DictReader((root / "esc50.csv").read_text().splitlines()))
    categories = ["vacuum_cleaner", "rain", "wind", "keyboard_typing"]
    for category in categories:
        row = next(r for r in rows if r["category"] == category)
        url = "https://raw.githubusercontent.com/karolpiczak/ESC-50/master/audio/" + row["filename"]
        urls[category + ".wav"] = url
        path = root / (category + ".wav")
        if not path.exists():
            with urllib.request.urlopen(url, timeout=60) as response:
                path.write_bytes(response.read())
    rng = np.random.default_rng(1234)
    cases = []
    for speaker in ["libri1", "libri2"]:
        raw = decode(ffmpeg, root / (speaker + ".ogg"))[:480000]
        raw *= 0.65 / np.max(np.abs(raw))
        speech = np.r_[np.zeros(48000), raw, np.zeros(48000)]
        write(ffmpeg, root / (speaker + "-reference.wav"), speech)
        write(ffmpeg, root / (speaker + "-clean-input.wav"), speech)
        cases.append({"name": speaker + "-clean", "reference": speaker + "-reference.wav", "clean": True})
        for category in ["hiss", *categories]:
            noise = rng.normal(size=len(speech)) if category == "hiss" else np.resize(
                decode(ffmpeg, root / (category + ".wav")), len(speech))
            noise *= np.sqrt(np.mean(raw ** 2)) / np.sqrt(np.mean(noise ** 2)) / 10 ** (5 / 20)
            mixture = speech + noise
            # Leave headroom so the fixture itself never clips the voice.
            gain = min(1.0, 0.95 / np.max(np.abs(mixture)))
            name = speaker + "-" + category
            write(ffmpeg, root / (name + "-input.wav"), mixture * gain)
            write(ffmpeg, root / (name + "-reference.wav"), speech * gain)
            cases.append({"name": name, "reference": name + "-reference.wav", "clean": False})
    (root / "fixtures.json").write_text(json.dumps(cases, indent=2))
    sources = {name: {"url": url, "sha256": hashlib.sha256((root / name).read_bytes()).hexdigest()}
               for name, url in urls.items()}
    (root / "sources.json").write_text(json.dumps(sources, indent=2))
    print(f"Prepared {len(cases)} paired recordings: two speakers, clean speech and five noise types.")


def si_sdr(reference: np.ndarray, output: np.ndarray) -> float:
    projection = reference * np.dot(reference, output) / np.dot(reference, reference)
    return float(10 * np.log10(np.sum(projection ** 2) /
                              max(1e-20, np.sum((output - projection) ** 2))))


def evaluate(ffmpeg: str, root: Path) -> None:
    from pystoi import stoi
    results = []
    failures = []
    for case in json.loads((root / "fixtures.json").read_text()):
        name = case["name"]
        reference = decode(ffmpeg, root / case["reference"])
        noisy = decode(ffmpeg, root / (name + "-input.wav"))
        output = decode(ffmpeg, root / "app-outputs" / (name + "-output.wav"))
        if len(output) != len(reference):
            failures.append(f"{name}: duration changed by {len(output) - len(reference)} samples")
        length = min(len(reference), len(output))
        reference, noisy, output = reference[:length], noisy[:length], output[:length]
        # Measure residual latency; never align away a sync error when scoring.
        correlation = correlate(output, reference, method="fft")
        mid = length - 1
        delay = int(np.argmax(correlation[mid - 960:mid + 961]) - 960)
        gain = np.dot(reference, output) / np.dot(reference, reference)
        # Interior of the final noise-only interval, after speech filter decay.
        quiet = slice(length - 24000, length - 4800)
        reduction = float(10 * np.log10(max(1e-20, np.mean(noisy[quiet] ** 2)) /
                                       max(1e-20, np.mean(output[quiet] ** 2))))
        result = {"sample": name, "clean_input": case["clean"], "samples": length,
                  "delay_samples": delay, "speech_gain_db": float(20 * np.log10(abs(gain) + 1e-20)),
                  "input_stoi": float(stoi(reference, noisy, 48000)),
                  "output_stoi": float(stoi(reference, output, 48000)),
                  "output_si_sdr_db": si_sdr(reference, output),
                  "noise_only_reduction_db": reduction, "peak": float(np.max(np.abs(output)))}
        if not case["clean"]:
            result["input_si_sdr_db"] = si_sdr(reference, noisy)
            if result["output_si_sdr_db"] < result["input_si_sdr_db"] + 1:
                failures.append(f"{name}: insufficient speech/noise improvement")
            if result["output_stoi"] < result["input_stoi"] - 0.01:
                failures.append(f"{name}: intelligibility worsened")
            if reduction < 20:
                failures.append(f"{name}: noise-only reduction below 20 dB")
        elif result["output_stoi"] < 0.95:
            failures.append(f"{name}: clean speech intelligibility below 0.95")
        if abs(delay) > 2:
            failures.append(f"{name}: uncompensated audio latency ({delay} samples)")
        if not -3 < result["speech_gain_db"] < 1 or result["peak"] >= 1:
            failures.append(f"{name}: voice level lost or output clipped")
        results.append(result)
        print(name, "STOI", round(result["input_stoi"], 3), "->", round(result["output_stoi"], 3),
              "pause suppression", round(reduction, 1), "dB", "delay", delay, "samples")
    (root / "quality-results.json").write_text(json.dumps({"results": results, "failures": failures}, indent=2))
    if failures:
        raise SystemExit("Quality checks failed:\n" + "\n".join(failures))
    print(f"PASS: {len(results)} real app outputs preserve duration, timing, speech and reduce noise.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--evaluate", action="store_true")
    parser.add_argument("--quality-deps", type=Path)
    args = parser.parse_args()
    if args.quality_deps:
        sys.path.insert(0, str(args.quality_deps.resolve()))
    args.output.mkdir(parents=True, exist_ok=True)
    if args.prepare:
        prepare(args.ffmpeg, args.output)
    if args.evaluate:
        evaluate(args.ffmpeg, args.output)
