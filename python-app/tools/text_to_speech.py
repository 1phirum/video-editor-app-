"""Generate one Microsoft neural voice file for every timed subtitle.

Concurrency, not a faster voice. Every segment is one websocket request to the
Microsoft Edge TTS endpoint, so the wall clock here is network latency multiplied
by the number of cues, and it used to be multiplied serially: 19831 cues at half a
second of round trip each is over three hours during which the app can only report
"Generating subtitle voice 22 of 19831". The synthesis itself is unchanged - same
voice, same request, same bytes - it just runs a bounded number of requests at a
time.

Three further properties this worker guarantees, in order of how much they matter:

  * No segment is skipped. A failed request is retried with backoff, and a run
    that still cannot produce audio for a cue fails loudly with that cue named
    instead of quietly returning a short list.
  * Identical text is synthesized once. Cue tracks repeat themselves ("[Music]",
    "Yeah", a recurring name), and the output file is addressed by the hash of
    voice plus text, so the second occurrence is a dictionary hit.
  * A re-run is a resume. Because names are content-addressed, files already on
    disk from a previous attempt are reused, so retrying after a network failure
    costs only the cues that actually failed.
"""

import asyncio
import hashlib
import json
import os
import re
import sys
import time
from pathlib import Path


VOICES = {
    "en": {"female": "en-US-JennyNeural", "male": "en-US-GuyNeural"},
    "zh": {"female": "zh-CN-XiaoxiaoNeural", "male": "zh-CN-YunxiNeural"},
    "km": {"female": "km-KH-SreymomNeural", "male": "km-KH-PisethNeural"},
}

# How many requests are in flight at once. The endpoint throttles well before the
# local machine runs out of sockets, and past roughly sixteen the failure rate
# climbs faster than the throughput does, which costs retries and therefore time.
DEFAULT_CONCURRENCY = 12
MAX_CONCURRENCY = 32
# Per request, not per segment: a segment gets ATTEMPTS chances at this timeout.
REQUEST_TIMEOUT_S = 90.0
ATTEMPTS = 4
RETRY_BASE_DELAY_S = 0.75
PROGRESS_MIN_INTERVAL_S = 0.2
PROGRESS_MIN_STEP = 8


def safe_name(text: str) -> str:
    value = re.sub(r"[^\w\- ]+", " ", text, flags=re.UNICODE).strip()
    return (value[:40].strip() or "voice")


def digest(voice: str, text: str) -> str:
    return hashlib.sha1(f"{voice}\x00{text}".encode("utf-8")).hexdigest()[:20]


def output_path(output_dir: Path, voice: str, text: str) -> Path:
    # Content addressed, so the name is a pure function of what is inside the
    # file. That is what makes both the dedup and the resume work; the readable
    # prefix is only there so the folder can be browsed by a human.
    return output_dir / f"{safe_name(text)}-{digest(voice, text)}.mp3"


def usable(path: Path) -> bool:
    try:
        # A truncated file from a killed run must not be trusted. Anything under a
        # few hundred bytes cannot be a second of speech.
        return path.is_file() and path.stat().st_size > 256
    except OSError:
        return False


def resolve_concurrency(requested) -> int:
    for candidate in (requested, os.environ.get("CUTPRO_TTS_CONCURRENCY")):
        try:
            value = int(candidate)
        except (TypeError, ValueError):
            continue
        if value > 0:
            return min(MAX_CONCURRENCY, value)
    return DEFAULT_CONCURRENCY


class Progress:
    """Throttled PROGRESS lines.

    Completions arrive out of order once requests overlap, so progress is a count
    of finished work rather than an index into the list. The throttle is here
    because the app parses every line it is given, and 19831 of them is a
    measurable cost on the receiving side for no extra information.
    """

    def __init__(self, total: int) -> None:
        self.total = max(1, total)
        self.done = 0
        self._emitted_at = 0.0
        self._emitted_done = -PROGRESS_MIN_STEP

    def advance(self, count: int = 1) -> None:
        self.done += count
        now = time.monotonic()
        if (self.done - self._emitted_done < PROGRESS_MIN_STEP
                and now - self._emitted_at < PROGRESS_MIN_INTERVAL_S
                and self.done < self.total):
            return
        self._emitted_at = now
        self._emitted_done = self.done
        fraction = min(0.95, self.done / self.total * 0.95)
        print(f"PROGRESS {fraction:.4f} {self.done} {self.total}", flush=True)


async def synthesize(text: str, voice: str, output: Path) -> None:
    import edge_tts

    # Written aside and moved into place. A killed run must not leave a partial
    # file behind under a content-addressed name, because the next run would read
    # that name as "already done".
    staging = output.with_name(f".{output.name}.{os.getpid()}.{time.time_ns()}.part")
    try:
        await edge_tts.Communicate(text, voice).save(str(staging))
        if not usable(staging):
            raise RuntimeError("the voice service returned no audio")
        os.replace(staging, output)
    finally:
        if staging.exists():
            try:
                staging.unlink()
            except OSError:
                pass


async def synthesize_one(text: str, voice: str, output: Path) -> None:
    """One unique piece of text, retried until it exists or the attempts run out."""
    if usable(output):
        return
    last_error: Exception | None = None
    for attempt in range(ATTEMPTS):
        try:
            await asyncio.wait_for(synthesize(text, voice, output),
                                   timeout=REQUEST_TIMEOUT_S)
            return
        except asyncio.CancelledError:
            raise
        except Exception as exc:  # network, service, and timeout errors alike
            last_error = exc
            if usable(output):
                # A concurrent worker on the same text won the race.
                return
            if attempt + 1 < ATTEMPTS:
                await asyncio.sleep(RETRY_BASE_DELAY_S * (2 ** attempt))
    raise RuntimeError(
        f"could not generate voice for {text[:60]!r}: {last_error}")


async def synthesize_segments(segments, voice: str, output_dir: Path,
                              concurrency: int):
    """Every cue, a bounded number of requests at a time."""
    # Pass one: the plan. Cues that cannot produce audio are dropped here, and the
    # original index rides along so the app can still match a file to its cue.
    planned = []
    unique: dict[Path, str] = {}
    uses: dict[Path, int] = {}
    for index, segment in enumerate(segments):
        text = str(segment.get("text", "")).strip()
        start_ms = int(segment.get("startMs", 0) or 0)
        end_ms = int(segment.get("endMs", 0) or 0)
        if not text or end_ms <= start_ms:
            continue
        output = output_path(output_dir, voice, text)
        planned.append({"path": str(output), "startMs": start_ms,
                        "endMs": end_ms, "text": text, "index": index})
        unique.setdefault(output, text)
        uses[output] = uses.get(output, 0) + 1

    # Pass two: one request per distinct piece of text, and none at all for text
    # already on disk from an earlier attempt.
    pending = [(path, text) for path, text in unique.items() if not usable(path)]
    # Counted in cues, not in requests, so the app's "voice N of M" still refers to
    # the subtitles the user selected rather than to the deduplicated work.
    progress = Progress(len(planned))
    progress.advance(sum(count for path, count in uses.items() if usable(path)))
    queue: asyncio.Queue = asyncio.Queue()
    for item in pending:
        queue.put_nowait(item)

    async def worker() -> None:
        while True:
            try:
                path, text = queue.get_nowait()
            except asyncio.QueueEmpty:
                return
            await synthesize_one(text, voice, path)
            progress.advance(uses.get(path, 1))

    workers = [asyncio.create_task(worker())
               for _ in range(max(1, min(concurrency, len(pending) or 1)))]
    try:
        # gather(), so the first failure propagates. A run that cannot produce audio
        # for a cue has to fail with that cue named, not return a shorter list.
        await asyncio.gather(*workers)
    except BaseException:
        for task in workers:
            task.cancel()
        await asyncio.gather(*workers, return_exceptions=True)
        raise

    # The promise, checked rather than assumed: every file the manifest will point
    # at exists and is big enough to be speech.
    missing = [str(path) for path in unique if not usable(path)]
    if missing:
        raise RuntimeError(
            f"{len(missing)} voice file(s) missing after synthesis, first: {missing[0]}")
    return planned


def fail(message: str) -> int:
    print(json.dumps({"ok": False, "error": message}), flush=True)
    return 1


def main() -> int:
    if len(sys.argv) < 3:
        return fail("usage: text_to_speech.py <request.json> <output-dir>")
    try:
        request = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    except Exception as exc:
        return fail(f"could not read the request file: {exc}")

    segments = request.get("segments") or []
    language = str(request.get("language") or "en")
    gender = str(request.get("gender") or "female")
    voices = VOICES.get(language) or VOICES["en"]
    voice = voices.get(gender) or voices["female"]

    output_dir = Path(sys.argv[2])
    try:
        output_dir.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        return fail(f"could not create the output folder: {exc}")

    try:
        outputs = asyncio.run(synthesize_segments(
            segments, voice, output_dir, resolve_concurrency(request.get("concurrency"))))
    except ModuleNotFoundError:
        return fail("edge-tts is not installed for the configured Python. "
                    "Run: python -m pip install edge-tts")
    except KeyboardInterrupt:
        return fail("cancelled")
    except Exception as exc:
        return fail(str(exc))

    # The manifest goes to a file and only its path goes down the pipe. Twenty
    # thousand entries is several megabytes of JSON, and the reader had to locate
    # that one line inside its whole accumulated stdout buffer to find it.
    manifest = output_dir / "tts-manifest.json"
    try:
        manifest.write_text(json.dumps({"ok": True, "outputs": outputs},
                                       ensure_ascii=False), encoding="utf-8")
    except OSError as exc:
        return fail(f"could not write the manifest: {exc}")
    print(f"MANIFEST {manifest}", flush=True)
    print(json.dumps({"ok": True, "manifest": str(manifest),
                      "count": len(outputs)}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

