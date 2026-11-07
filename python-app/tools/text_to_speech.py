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

Every manifest entry also carries the real length of the audio behind it. It used
to carry only the cue, and the app then trusted the cue: the clip it placed was as
long as the subtitle, so a line the voice needed 3.4 s to say inside a 2.1 s cue
was cut off mid-sentence in the monitor and again on export. The measured length
goes down the pipe instead, and the app places a clip that long.

The voice is spoken at its natural pace. Re-timing a line to fit the gap before the
next one is what made a generated track sound hurried - most lines overran only
slightly, and speeding all of them up to rescue the few that overran badly made the
whole dub worse - so MAX_SPEED is 1.0 and nothing is re-encoded unless a request
asks for it by name. A line that needs more room than it has is not shortened and
not sped up: it reports requiredSpeed, the app moves it to the next free audio track
so it is not talking over its neighbour, and it is listed where the user can see it.

The room a line has is its *slot* - from its own start to the next cue's start - and
not its cue. Those differ by whatever silence follows the line, which in a transcript
is most of the time, so the slot is what requiredSpeed is measured against.

Finished cues also leave over the pipe one at a time, as `CUE {json}` lines, and
not only in the manifest at the end. A cue is finished the moment its audio exists
and has been measured and re-timed, so the app can place it while the rest of the
track is still being spoken. That matters twice over: a twenty-thousand-cue run no
longer has to hold the whole manifest, its parsed copy and every clip built from it
in memory at the same moment, and progress can be honest, because the measure and
re-time passes are now inside the meter instead of behind a 95% that sat still for
minutes. The manifest is still written, for an older app build that only reads it.
"""

import asyncio
import hashlib
import json
import os
import random
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


# The CUE lines this worker prints carry the subtitle's own text, and the trailing
# status lines carry ffmpeg's, so both can hold characters - an em dash, a curly
# quote, a whole Khmer or Chinese line - that Windows' default console codec (cp1252)
# cannot encode. Printing one then raises "'charmap' codec can't encode characters",
# the run dies, and the app shows that in place of a voice track. Standard output and
# error are pinned to UTF-8, which is what the app already reads them as; errors are
# replaced rather than raised so one stray byte can never take a whole dub down again.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


VOICES = {
    "en": {"female": "en-US-JennyNeural", "male": "en-US-GuyNeural"},
    "zh": {"female": "zh-CN-XiaoxiaoNeural", "male": "zh-CN-YunxiNeural"},
    "km": {"female": "km-KH-SreymomNeural", "male": "km-KH-PisethNeural"},
    "es": {"female": "es-ES-ElviraNeural", "male": "es-ES-AlvaroNeural"},
    "fr": {"female": "fr-FR-DeniseNeural", "male": "fr-FR-HenriNeural"},
    "de": {"female": "de-DE-KatjaNeural", "male": "de-DE-ConradNeural"},
    "ja": {"female": "ja-JP-NanamiNeural", "male": "ja-JP-KeitaNeural"},
    "ko": {"female": "ko-KR-SunHiNeural", "male": "ko-KR-InJoonNeural"},
    "vi": {"female": "vi-VN-HoaiMyNeural", "male": "vi-VN-NamMinhNeural"},
    "th": {"female": "th-TH-PremwadeeNeural", "male": "th-TH-NiwatNeural"},
    "id": {"female": "id-ID-GadisNeural", "male": "id-ID-ArdiNeural"},
    "pt": {"female": "pt-BR-FranciscaNeural", "male": "pt-BR-AntonioNeural"},
    "ru": {"female": "ru-RU-SvetlanaNeural", "male": "ru-RU-DmitryNeural"},
    "it": {"female": "it-IT-ElsaNeural", "male": "it-IT-DiegoNeural"},
    "hi": {"female": "hi-IN-SwaraNeural", "male": "hi-IN-MadhurNeural"},
    "ar": {"female": "ar-SA-ZariyahNeural", "male": "ar-SA-HamedNeural"},
}

# How many requests are in flight at once. The endpoint throttles well before the
# local machine runs out of sockets, and past roughly sixteen the failure rate
# climbs faster than the throughput does, which costs retries and therefore time.
DEFAULT_CONCURRENCY = 12
MAX_CONCURRENCY = 32
# Per request, not per segment: a segment gets ATTEMPTS chances at this timeout.
REQUEST_TIMEOUT_S = 90.0
# Six rather than four, because the four were spent inside about five seconds
# (0.75 + 1.5 + 3.0) and the thing being waited out is longer than that. The voice
# endpoint answers a request it does not feel like serving by closing the stream
# without sending any audio, and for a low-traffic voice - Khmer, say - that can
# persist for a good few seconds. Six attempts spread the wait over about half a
# minute, which is the difference between a line that comes back and a line that is
# reported as impossible.
ATTEMPTS = 6
RETRY_BASE_DELAY_S = 0.75
PROGRESS_MIN_INTERVAL_S = 0.2
PROGRESS_MIN_STEP = 8

# How much a line may be sped up to fit the room it has. 1.0 means never: the voice
# is spoken at its natural pace and a line that runs long simply runs long, which the
# app answers by moving it to the next free audio track. Anything above about 1.2 is
# audibly rushed - that is what this used to do (1.5) and why a whole dub came back
# sounding hurried. A request may still raise it through maxSpeed, up to 3.0, for a
# dub that has to stay glued to the picture.
MAX_SPEED = 1.0
# Below this the re-encode is not worth its own artefacts. A max_speed under this is
# therefore also the switch that turns re-timing off altogether.
MIN_SPEED = 1.02
# Aimed at this much short of the next line rather than exactly at it. atempo does
# not land on a length to the millisecond - mp3 frames are 24 ms at this sample rate
# and the filter rounds - so a line fitted to exactly its slot comes out a few
# milliseconds long about half the time, overlaps by those few milliseconds, and gets
# moved down a track for it. Aiming short absorbs that, and a breath between lines is
# an improvement anyway. Never more than a tenth of the slot, so a very short gap is
# not turned into a much higher speed by the padding alone.
FIT_PAD_MS = 40
# Re-timing is one ffmpeg process per overlong cue, so they overlap. They are
# short, CPU-bound and local, unlike the synthesis requests above.
FIT_CONCURRENCY = 8
FIT_TIMEOUT_S = 120.0
# Head and tail silence comes off every line before it is measured. The voice service
# brackets each utterance with a moment of room tone - about a tenth of a second in
# front, often more behind a full stop - which is inaudible on its own and ruinous in
# bulk: across a 2581-line transcript it is tens of minutes of silence that the chain
# then has to find room for, and every millisecond of it pushes the end of the dub
# further past the last frame of the picture. Cutting it removes nothing that can be
# heard, shortens the dub without speaking a single word of it faster, and lowers
# every line's requiredSpeed, so fewer lines need re-timing at all.
TRIM_THRESHOLD_DB = -45
# Left in at each end rather than cut flush. A cut at the first sample over the
# threshold clips the attack of a plosive, and two lines butted directly against each
# other sound like a splice.
TRIM_KEEP_MS = 40
# The tail is trimmed more gently than the head. A word's onset is loud (a plosive,
# a vowel attack), so leading room tone can be cut close. A word's *release* is not:
# many endings - a Khmer final consonant, a trailing vowel, a soft "s" - decay
# gradually through the quiet range before they stop, and at -45 dB that decay reads
# as silence, so the last word came back clipped or "faded early". A lower threshold
# keeps that decay as speech, and more kept padding leaves the natural release and a
# breath intact. This is the "generate full, no cut" fix; a little extra room tone at
# the end of a line is inaudible where a missing final word is broken.
TAIL_THRESHOLD_DB = -55
TAIL_KEEP_MS = 220
# edge-tts returns a fairly quiet stream (around -20 LUFS), so the generated dub
# sat well below the rest of the mix. Every line is loudness-normalised to a
# consistent, punchy target as part of the trim pass (one ffmpeg encode, no extra
# process). -14 LUFS with a -1 dBTP ceiling is the streaming-loud target used by
# YouTube/Spotify; it lifts the voice to full level without letting peaks clip.
LOUDNORM_FILTER = "loudnorm=I=-14:TP=-1.0:LRA=11"
# The other half of the silence is *inside* the line - edge-tts holds a long pause at
# every sentence end, and a recap's cues are sentence fragments, so most lines carry
# one or two of these gaps. It is left in place. Compressing it reliably means finding
# each silent interval and cutting it on its own; a single silenceremove pass over the
# whole stream, measured against a known file, took out less than trimming the two ends
# already does. The ends are the dependable half of the win, so the ends are what
# run_trim takes. See run_trim.
# A floor under the whole thing. silenceremove classifies anything below the
# threshold as silence, and a line spoken very quietly, or one ffmpeg option behaving
# differently than expected on some build, could in principle come back gutted. A
# result under this fraction of the original is not trusted: the untrimmed file is
# kept instead, because a dub that is slightly too long is a nuisance and a dub with
# the words cut out of it is broken.
TRIM_MIN_RATIO = 0.5

# MPEG audio frame tables, enough of them to add up the frames of a Layer III file.
# Measured in-process on purpose: the alternative is one ffprobe per file, and a
# subtitle track brings twenty thousand of them.
_BITRATES_V1 = (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0)
_BITRATES_V2 = (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0)
_SAMPLE_RATES = {3: (44100, 48000, 32000),
                 2: (22050, 24000, 16000),
                 0: (11025, 12000, 8000)}


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


def audio_duration_ms(path: Path) -> int:
    """The real length of an mp3, by adding up its frames.

    edge-tts hands back a bare MPEG-2 Layer III stream, so there is no container
    to ask and no duration field to read: the length is however many frames are in
    the file. Returns 0 when the bytes cannot be read as frames at all, which the
    caller treats as "unknown" rather than as "empty".
    """
    try:
        data = path.read_bytes()
    except OSError:
        return 0
    offset = 0
    # An ID3v2 tag is not audio. Its size is stored seven bits per byte.
    if data[:3] == b"ID3" and len(data) >= 10:
        offset = 10 + (((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14) |
                       ((data[8] & 0x7F) << 7) | (data[9] & 0x7F))
        if data[5] & 0x10:
            offset += 10
    total_s = 0.0
    limit = len(data)
    while offset + 4 <= limit:
        if data[offset] != 0xFF or (data[offset + 1] & 0xE0) != 0xE0:
            offset += 1
            continue
        version = (data[offset + 1] >> 3) & 0x03  # 3 MPEG1, 2 MPEG2, 0 MPEG2.5
        layer = (data[offset + 1] >> 1) & 0x03    # 1 is Layer III
        rate_index = (data[offset + 2] >> 2) & 0x03
        if version == 1 or layer != 1 or rate_index == 3:
            offset += 1
            continue
        table = _BITRATES_V1 if version == 3 else _BITRATES_V2
        bitrate = table[(data[offset + 2] >> 4) & 0x0F] * 1000
        sample_rate = _SAMPLE_RATES[version][rate_index]
        if bitrate <= 0 or sample_rate <= 0:
            offset += 1
            continue
        samples = 1152 if version == 3 else 576
        length = (samples // 8) * bitrate // sample_rate + \
            ((data[offset + 2] >> 1) & 0x01)
        if length <= 4:
            offset += 1
            continue
        total_s += samples / sample_rate
        offset += length
    return int(round(total_s * 1000))


def fit_budget_ms(budget_ms: int) -> int:
    """The length a fitted line is actually aimed at. See FIT_PAD_MS."""
    if budget_ms <= 0:
        return 0
    return max(budget_ms - FIT_PAD_MS, int(budget_ms * 0.9), 1)


def fit_path(source: Path, speed: float) -> Path:
    # Keyed on the speed that was applied, because that - with the source file - is
    # the whole of what is in the fitted file. Two cues that repeat the text and end
    # up at the same speed share one re-encode, and a rebuild at a different speed
    # limit writes a different name instead of silently reusing audio made under the
    # old limit. The name has to agree with the atempo argument to the millisecond of
    # speed, so both are taken from the same rounded number.
    #
    # The marker is -s and not -x because every -x file already on disk was built by
    # re-timing the *untrimmed* original (see finalize_file): right name, wrong audio,
    # about a second of room tone per line too long. usable() would have handed those
    # straight back, so the fix would not have shown until the cache was cleared. A new
    # marker retires them without touching anyone's cache; the old files are orphans and
    # Clear cache in Settings reclaims them.
    return source.with_name(f"{source.stem}-s{int(round(speed * 1000))}.mp3")


def publish_file(staging: Path, target: Path) -> None:
    """Publish completed audio, including Windows redirected/encrypted folders."""
    try:
        os.replace(staging, target)
    except OSError as exc:
        if os.name != "nt" or getattr(exc, "winerror", None) != 17:
            raise
        # MoveFileEx without COPY_ALLOWED can report ERROR_NOT_SAME_DEVICE even
        # for sibling paths in a redirected app-data folder. Finish the Windows
        # copy/move synchronously before publishing the cue or trusting its cache.
        import ctypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        move = kernel32.MoveFileExW
        move.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint]
        move.restype = ctypes.c_int
        if not move(str(staging), str(target), 0x1 | 0x2 | 0x8):
            raise ctypes.WinError(ctypes.get_last_error()) from exc


def run_filter(ffmpeg: str, source: Path, target: Path, filters: str) -> bool:
    """One ffmpeg audio-filter pass, written aside and moved into place.

    Staged under a unique name for the same reason synthesis is: these names are
    content-addressed, so a killed run must not leave a partial file where the next
    run would read the name as "already done". False leaves the caller holding the
    source file, which is what every user of this degrades to.
    """
    staging = target.with_name(f".{target.name}.{os.getpid()}.{time.time_ns()}.part.mp3")
    command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
               "-filter:a", filters, "-vn"]
    for codec in (["-c:a", "libmp3lame", "-q:a", "5"], ["-c:a", "mp3"]):
        try:
            completed = subprocess.run(command + codec + [str(staging)],
                                       stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL,
                                       timeout=FIT_TIMEOUT_S,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except (OSError, subprocess.SubprocessError):
            break
        if completed.returncode == 0 and usable(staging):
            try:
                publish_file(staging, target)
                return True
            except OSError:
                break
    try:
        if staging.exists():
            staging.unlink()
    except OSError:
        pass
    return False


def run_fit(ffmpeg: str, source: Path, target: Path, speed: float) -> bool:
    """One atempo pass. False leaves the caller with the unfitted file, which is
    the fallback the whole feature degrades to."""
    return run_filter(ffmpeg, source, target, f"atempo={speed:.6f}")


def trim_path(source: Path) -> Path:
    # Its own name, so the untrimmed original stays on disk as the fallback and a
    # re-run reuses the trim instead of doing it again. Fitted names are derived from
    # this one, which also keeps them clear of fits made before trimming existed.
    return source.with_name(f"{source.stem}-t.mp3")


def run_trim(ffmpeg: str, source: Path, target: Path, natural_ms: int) -> int:
    """Cut the room tone off the front and back of a line.

    Returns the trimmed length in ms, or 0 when nothing usable came back and the
    caller should keep the original. silenceremove trims a stream's *start*, so the
    tail is done by reversing the audio, trimming its new start, and reversing it
    back. Both ends keep TRIM_KEEP_MS so a cut never clips the attack of a word and
    two lines butted together do not sound like a splice.

    The pauses *inside* a line are deliberately left alone. Compressing them needs the
    silence found and cut interval by interval, and a single silenceremove pass over
    the whole stream does it neither predictably nor well - measured, it removed less
    than trimming the two ends does. Room tone at the ends is the reliable, audible-
    to-no-one half of the win, and it is the half that is taken here.

    A result under TRIM_MIN_RATIO of the original is never trusted: a line spoken very
    quietly, or an ffmpeg build reading an option differently, could come back gutted,
    and a dub slightly too long is a nuisance where a dub with words cut out is broken.
    """
    keep = f"{TRIM_KEEP_MS / 1000.0:.3f}"
    head = (f"silenceremove=start_periods=1"
            f":start_threshold={TRIM_THRESHOLD_DB}dB"
            f":start_silence={keep}"
            f":detection=peak")
    # The tail pass runs on the reversed stream, so its "start" is the end of the
    # line. It uses a lower threshold and keeps more, so the release of the last
    # word is never mistaken for room tone and cut. See TAIL_THRESHOLD_DB.
    tail_keep = f"{TAIL_KEEP_MS / 1000.0:.3f}"
    tail = (f"silenceremove=start_periods=1"
            f":start_threshold={TAIL_THRESHOLD_DB}dB"
            f":start_silence={tail_keep}"
            f":detection=peak")
    # Loudness-normalise in the same pass that trims, so every placed line comes
    # out at full, consistent level with no extra encode.
    chain = f"{head},areverse,{tail},areverse,{LOUDNORM_FILTER}"
    if run_filter(ffmpeg, source, target, chain):
        result = audio_duration_ms(target)
        if natural_ms * TRIM_MIN_RATIO <= result < natural_ms:
            return result
    try:
        if target.exists():
            target.unlink()
    except OSError:
        pass
    return 0


def finalize_file(path: Path, entries, ffmpeg: str, max_speed: float,
                  fixed_speed: float = 1.0) -> list:
    """Give every cue that uses *path* the length the app will place it at.

    Called as soon as that one file exists, and off the event loop, because it reads
    the whole file to measure it and may run ffmpeg. Each entry ends up with the
    length of the audio the app will actually place. The app needs it: it is the clip
    length, and a clip shorter than its audio is a sentence cut off.

    Re-timing is off unless the request asks for it (MAX_SPEED = 1.0). Fitting every
    line to the gap before the next one is what made a dub sound rushed, and speeding
    up the many lines that only just overran to save the few that badly did was the
    worst of the trade: the whole track paid for it. With no maxSpeed in the request
    every entry keeps speed 1.0, and a line that needs more room than it has says so
    through requiredSpeed - the app lists those and moves what overlaps onto the next
    free audio track.

    A request that does pass maxSpeed buys the timeline back: each line is sped up by
    exactly what it needs and no more, up to that limit, so a line that only just
    overran is barely touched while the worst one takes the whole limit. Every line
    whose requiredSpeed is at or under the limit then ends before the next one starts
    and the whole track fits on A1. requiredSpeed is still reported unchanged, so the
    app can say which lines the limit did not save.

    requiredSpeed is measured against the *slot*, not the cue: from this cue's start
    to the next cue's start, which is where the following voice has to begin. The cue
    ignores the silence after the line, which in a transcript is most of the time.

    All of it is measured on the *trimmed* file. Cutting the room tone the voice
    service puts around each utterance is the one way to shorten a dub that costs
    nothing audible, so it happens first and unconditionally, and the numbers that
    leave here describe the line as it will be heard. See TRIM_THRESHOLD_DB.
    """
    natural_ms = audio_duration_ms(path)
    # Before anything is measured or re-timed: the line without the silence the voice
    # service wrapped it in. Everything downstream then works on the shorter length -
    # the clip the app places, and requiredSpeed with it - so a line that overran its
    # slot by less than its own dead air stops overrunning without being touched.
    source = path
    if ffmpeg:
        trimmed = trim_path(path)
        # A trim an earlier run already made and that still clears the floor is reused
        # as-is; otherwise it is (re)made now. run_trim owns the floor and the
        # aggressive-then-safe fallback, and returns 0 when nothing usable came back.
        trimmed_ms = audio_duration_ms(trimmed) if usable(trimmed) else 0
        if not (natural_ms * TRIM_MIN_RATIO <= trimmed_ms < natural_ms):
            trimmed_ms = run_trim(ffmpeg, path, trimmed, natural_ms)
        if trimmed_ms > 0:
            source = trimmed
            natural_ms = trimmed_ms
    measured: dict = {}
    for entry in entries:
        entry["path"] = str(source)
        entry["audioMs"] = natural_ms
        entry["speed"] = 1.0
        cue_ms = entry["endMs"] - entry["startMs"]
        # slotMs is 0 for the last cue: nothing follows it, so nothing to collide
        # with, and its own cue length is all the guidance there is.
        slot_ms = int(entry.get("slotMs") or 0)
        budget_ms = fit_budget_ms(slot_ms if slot_ms > 0 else cue_ms)
        # Reported whether or not anything is done about it: how much faster than
        # natural this line would have to be spoken to end before the next one
        # starts. 1.0 or less means it fits. This is the number behind the app's
        # "speaks too fast to fit" list, and it is also the speed limit at which
        # this line stops overlapping - the app offers the largest of them as the
        # limit that clears the whole track.
        entry["requiredSpeed"] = (round(natural_ms / budget_ms, 4)
                                  if budget_ms > 0 and natural_ms > 0 else 1.0)
        # Fixed speed: every line is spoken at exactly this rate, whether or not
        # it needed speeding up to fit. This is the "all voices 1.15x" control -
        # a uniform pace across the whole dub - and it takes precedence over the
        # per-line auto-fit below. requiredSpeed is still the fit number, so the
        # voice report can still say which lines overrun at this pace.
        if fixed_speed >= MIN_SPEED and ffmpeg and natural_ms > 0:
            speed = round(min(fixed_speed, 3.0), 3)
            target = fit_path(source, speed)
            if target not in measured:
                if usable(target) or run_fit(ffmpeg, source, target, speed):
                    measured[target] = audio_duration_ms(target)
                else:
                    measured[target] = 0
            if measured[target] > 0:
                entry["path"] = str(target)
                entry["audioMs"] = measured[target]
                entry["speed"] = speed
            continue
        if max_speed < MIN_SPEED:
            # Natural pace, by request. Nothing is re-encoded and nothing is cut.
            continue
        if not ffmpeg or natural_ms <= 0 or budget_ms <= 0:
            continue
        speed = natural_ms / budget_ms
        if speed < MIN_SPEED:
            continue
        # Only as much as this line needs, never more, and never past the limit the
        # request asked for. A line that only just overran is barely touched.
        speed = round(min(speed, max_speed), 3)
        target = fit_path(source, speed)
        if target not in measured:
            # source, not path: the trimmed file is what natural_ms, requiredSpeed and
            # speed were all measured on, and what this target is named after. Passing
            # the untrimmed original here re-timed the wrong audio - the line kept the
            # room tone the trim had already found and removed, and every number the app
            # placed it by described a shorter file than the one it got. On a 2581-line
            # transcript that was half an hour of silence in the dub, sped up: the trim
            # was computed, measured, believed, and then discarded.
            if usable(target) or run_fit(ffmpeg, source, target, speed):
                measured[target] = audio_duration_ms(target)
            else:
                measured[target] = 0
        if measured[target] <= 0:
            # The re-time did not happen. The natural length stays, and the app
            # gives the clip room for it rather than cutting it.
            continue
        entry["path"] = str(target)
        entry["audioMs"] = measured[target]
        entry["speed"] = speed
    return entries


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


class VoiceServiceRejected(RuntimeError):
    """A client-wide rejection, which retrying every subtitle cannot repair."""


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
        publish_file(staging, output)
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
            # edge-tts already retries 403 once after correcting clock skew.
            # A remaining authentication rejection affects every cue, so abort
            # the run instead of spending all six attempts on every subtitle.
            from aiohttp import ClientResponseError
            if isinstance(exc, ClientResponseError) and exc.status in (401, 403):
                raise VoiceServiceRejected(
                    f"Microsoft rejected the voice client (HTTP {exc.status}). "
                    "Update edge-tts for the configured Python and try again.") from exc
            last_error = exc
            if usable(output):
                # A concurrent worker on the same text won the race.
                return
            if attempt + 1 < ATTEMPTS:
                # Jittered, because every worker that lost the same hiccup would
                # otherwise come back at the same instant and lose it again.
                delay = RETRY_BASE_DELAY_S * (2 ** attempt)
                await asyncio.sleep(delay * (0.7 + 0.6 * random.random()))
    raise RuntimeError(
        f"could not generate voice for {text[:60]!r}: {last_error}")


async def synthesize_segments(segments, voice: str, output_dir: Path,
                              concurrency: int, ffmpeg: str = "",
                              max_speed: float = MAX_SPEED,
                              fixed_speed: float = 1.0):
    """Every cue, a bounded number of requests at a time, printed as it finishes."""
    # Pass one: the plan. Cues that cannot produce audio are dropped here, and the
    # original index rides along so the app can still match a file to its cue.
    planned = []
    unique: dict[Path, str] = {}
    cues: dict[Path, list] = {}
    for index, segment in enumerate(segments):
        text = str(segment.get("text", "")).strip()
        start_ms = int(segment.get("startMs", 0) or 0)
        end_ms = int(segment.get("endMs", 0) or 0)
        if not text or end_ms <= start_ms:
            continue
        output = output_path(output_dir, voice, text)
        entry = {"path": str(output), "startMs": start_ms, "endMs": end_ms,
                 "text": text, "index": index}
        planned.append(entry)
        unique.setdefault(output, text)
        cues.setdefault(output, []).append(entry)

    # The room each voice actually has: from its own start to the start of the next
    # one, which is the instant the following voice has to be speaking. Computed here
    # over the whole plan because finalize_file only ever sees the cues that share one
    # audio file and cannot know what comes after them.
    #
    # The last cue gets 0, meaning unbounded - nothing follows it. Two cues that start
    # at the same millisecond also get 0: that is a real overlap in the source
    # subtitles, and the app answers it with a second lane rather than by cutting one
    # of them short.
    ordered = sorted(planned, key=lambda item: (item["startMs"], item["index"]))
    for position, entry in enumerate(ordered):
        entry["slotMs"] = (
            max(0, ordered[position + 1]["startMs"] - entry["startMs"])
            if position + 1 < len(ordered)
            else 0)

    # Counted in cues, not in requests, so the app's "voice N of M" still refers to
    # the subtitles the user selected rather than to the deduplicated work. What it
    # counts is *finished* cues - measured, re-timed, printed, and by then already on
    # the timeline. Counting synthesized ones instead left the last two passes
    # outside the meter, so a long run sat at 95% looking done for minutes.
    progress = Progress(len(planned))
    queue: asyncio.Queue = asyncio.Queue()
    for path in unique:
        queue.put_nowait(path)
    loop = asyncio.get_running_loop()
    # Bounded on purpose: this is the backpressure. Measuring reads a whole file and
    # a re-time is an ffmpeg process, so finishing cannot be allowed to pile up
    # behind the requests that keep arriving.
    pool = ThreadPoolExecutor(max_workers=FIT_CONCURRENCY,
                              thread_name_prefix="tts-finish")

    async def finish(path: Path) -> None:
        entries = await loop.run_in_executor(
            pool, finalize_file, path, cues.get(path, []), ffmpeg, max_speed,
            fixed_speed)
        # One line per cue, handed over the moment it could become a clip. The app
        # places them as they arrive instead of waiting for the manifest, so the
        # track fills while it is still being spoken.
        for entry in entries:
            print("CUE " + json.dumps(entry, ensure_ascii=False), flush=True)
        progress.advance(len(entries))

    # Cues the service would not speak, kept rather than raised. See the gather below.
    failures: dict[Path, str] = {}

    async def worker() -> None:
        while True:
            try:
                path = queue.get_nowait()
            except asyncio.QueueEmpty:
                return
            # Free for a file an earlier run already produced: this is the resume.
            try:
                await synthesize_one(unique[path], voice, path)
            except (asyncio.CancelledError, VoiceServiceRejected):
                raise
            except Exception as exc:
                # One line the endpoint refuses is not a reason to throw away the
                # whole dub. It used to be: gather() propagated the first failure,
                # every other worker was cancelled, and a 55-line Khmer transcript
                # came back with nothing on the timeline because one cue in the
                # middle of it got no audio six times running - the other 54 had
                # already been spoken and were discarded with it. The cue is recorded
                # and named at the end instead, and the rest of the transcript is
                # still a usable voice track.
                failures[path] = str(exc)
                progress.advance(len(cues.get(path, [])))
                continue
            await finish(path)

    workers = [asyncio.create_task(worker())
               for _ in range(max(1, min(concurrency, len(unique) or 1)))]
    try:
        # Only a cancel or a bug reaches here now; a cue that cannot be spoken is in
        # failures instead.
        await asyncio.gather(*workers)
    except BaseException:
        for task in workers:
            task.cancel()
        await asyncio.gather(*workers, return_exceptions=True)
        raise
    finally:
        pool.shutdown(wait=True)

    # The promise, checked rather than assumed: every file the manifest will point
    # at exists and is big enough to be speech. Files a cue failed on are not in the
    # manifest, so they are not part of the promise.
    missing = [str(path) for path in unique
               if path not in failures and not usable(path)]
    if missing:
        raise RuntimeError(
            f"{len(missing)} voice file(s) missing after synthesis, first: {missing[0]}")

    if failures:
        lost = {str(path) for path in failures}
        planned = [entry for entry in planned if entry["path"] not in lost]
        if not planned:
            # Nothing came back at all. That is a real failure and it is worth the
            # error the app shows, with one cue named so it can be recognised.
            first = next(iter(failures.values()))
            raise RuntimeError(first)
        # slotMs is deliberately left as it was computed over the whole plan: a kept
        # cue keeps the shorter slot it had when the dropped one still followed it,
        # which can only make it fit more easily, never overrun.
        #
        # stderr, because the app already shows it as the panel's status line, and the
        # run has to stay a success or the 54 spoken lines never reach the timeline.
        texts = [unique[path][:40] for path in failures]
        print(f"{len(failures)} of {len(unique)} line(s) could not be spoken and were "
              f"left out: {'; '.join(texts[:3])}"
              f"{' ...' if len(texts) > 3 else ''}",
              file=sys.stderr, flush=True)
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
    # "zh-CN" and "km-KH" mean the same voice as "zh" and "km"; the app has more than
    # one list of language codes and they do not all agree on the region suffix.
    base = language.strip().lower().replace("_", "-").split("-")[0]
    voices = VOICES.get(base)
    if voices is None:
        # Named, not substituted. Falling back to English here was silent, and an
        # English voice handed a Khmer line is refused by the service with "No audio
        # was received" - an error that describes the request rather than the mistake,
        # and that sent this bug hunt after the Khmer text and the Khmer voices when
        # neither was ever at fault.
        return fail(f"no voice is configured for the language {language!r}. "
                    f"Available: {', '.join(sorted(VOICES))}.")
    voice = voices.get(gender) or voices["female"]
    # The app has always passed its own ffmpeg down here; until now nothing read
    # it. It is what re-times a line that overruns its cue.
    ffmpeg = str(request.get("ffmpeg") or "")
    if ffmpeg and not Path(ffmpeg).is_file():
        ffmpeg = ""
    try:
        max_speed = min(3.0, max(1.0, float(request.get("maxSpeed") or MAX_SPEED)))
    except (TypeError, ValueError):
        max_speed = MAX_SPEED
    # A uniform pace for every line, independent of the auto-fit limit above.
    # 1.0 (the default) means "off" - the auto-fit rules decide each line.
    try:
        fixed_speed = min(3.0, max(1.0, float(request.get("fixedSpeed") or 1.0)))
    except (TypeError, ValueError):
        fixed_speed = 1.0

    output_dir = Path(sys.argv[2])
    try:
        output_dir.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        return fail(f"could not create the output folder: {exc}")

    try:
        outputs = asyncio.run(synthesize_segments(
            segments, voice, output_dir,
            resolve_concurrency(request.get("concurrency")), ffmpeg, max_speed,
            fixed_speed))
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

