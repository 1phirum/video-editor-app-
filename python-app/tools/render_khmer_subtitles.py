#!/usr/bin/env python3
"""Create a UTF-8 ASS subtitle file with Khmer-safe styling for FFmpeg."""
from __future__ import annotations
import json
import math
import sys
import unicodedata
from pathlib import Path

FONTS = {"Khmer OS System", "Khmer OS Siemreap", "Khmer OS Battambang"}

def clean_text(value: object) -> str:
    text = unicodedata.normalize("NFC", str(value or "")).replace("\ufeff", "")
    text = "".join(ch for ch in text if ch in "\n\t" or ord(ch) >= 0x20)
    # ASS uses braces for override tags. Escape them so subtitle text cannot
    # accidentally change position, color, or font while rendering.
    return text.strip().replace("\\", "\\\\").replace("{", "\\{").replace("}", "\\}").replace("\n", "\\N")

def number(value: object, name: str) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{name} must be numeric") from error
    if not math.isfinite(result):
        raise ValueError(f"{name} must be finite")
    return result

def ass_time(seconds: float) -> str:
    cs = max(0, round(float(seconds) * 100))
    return f"{cs//360000}:{(cs//6000)%60:02d}:{(cs//100)%60:02d}.{cs%100:02d}"

def ass_color(value: str, fallback: str) -> str:
    value = value if isinstance(value, str) and value.startswith("#") else fallback
    raw = value[1:]
    if len(raw) == 6:
        raw = "ff" + raw
    a, r, g, b = raw[0:2], raw[2:4], raw[4:6], raw[6:8]
    return f"&H{255-int(a,16):02X}{b}{g}{r}".upper()

def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: render_khmer_subtitles.py input.json output.ass")
    source = Path(sys.argv[1])
    if not source.is_file():
        raise ValueError("subtitle input JSON does not exist")
    payload = json.loads(source.read_text(encoding="utf-8-sig"))
    out = Path(sys.argv[2])
    out.parent.mkdir(parents=True, exist_ok=True)
    style = payload.get("style", {})
    width, height = int(number(payload.get("width", 1920), "width")), int(number(payload.get("height", 1080), "height"))
    if width < 2 or height < 2:
        raise ValueError("width and height must be positive")
    font = style.get("captionFontFamily", "Khmer OS System")
    if font not in FONTS:
        font = "Khmer OS System"
    align_name = style.get("captionAlignment", "center")
    align = 1 if align_name == "left" else 3 if align_name == "right" else 2
    position = style.get("captionPosition", "bottom")
    align += 6 if position == "top" else 3 if position in ("center", "custom") else 0
    margin = 28 if position != "center" else height // 2
    requested_size = max(12, min(160, int(style.get("captionFontSize", 42))))
    export_size = max(12, min(480, round(requested_size * height / 720)))
    lines = ["[Script Info]", "ScriptType: v4.00+", "ScaledBorderAndShadow: yes", f"PlayResX: {width}", f"PlayResY: {height}", "", "[V4+ Styles]", "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding"]
    lines.append(f"Style: Khmer,{font},{export_size},{ass_color(style.get('captionTextColor','#ffffff'),'#ffffff')},&H000000FF,&H00000000,{ass_color(style.get('captionBackgroundColor','#b3000000'),'#b3000000')},{-1 if style.get('captionBold',True) else 0},{-1 if style.get('captionItalic',False) else 0},0,0,100,100,0,0,{3 if style.get('captionBackgroundVisible',True) else 1},{1 if style.get('captionBackgroundVisible',True) else 3},0,{align},40,40,{margin},1")
    lines += ["", "[Events]", "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text"]
    segments = payload.get("segments", [])
    if not isinstance(segments, list) or not segments:
        raise ValueError("segments must be a non-empty list")
    for segment in segments:
        if not isinstance(segment, dict):
            raise ValueError("each subtitle segment must be an object")
        start = number(segment.get("start", 0), "segment start")
        end = number(segment.get("end", 0), "segment end")
        if start < 0 or end <= start:
            raise ValueError("subtitle timings must satisfy 0 <= start < end")
        text = clean_text(segment.get("text", ""))
        if not text:
            continue
        override = ""
        if position == "custom":
            x = round(max(0, min(1, float(style.get("captionPositionX", .5)))) * width)
            y = round(max(0, min(1, float(style.get("captionPositionY", .85)))) * height)
            override = f"{{\\pos({x},{y})}}"
        lines.append(f"Dialogue: 0,{ass_time(start)},{ass_time(end)},Khmer,,0,0,0,,{override}{text}")
    if len(lines) <= 8:
        raise ValueError("no non-empty subtitle segments were supplied")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8-sig", newline="\n")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
