"""Download reference photographs for the letter styles in TextEffectCatalog.

Run from python-app/. Pulls from Wikimedia Commons, which unlike an image-search
scrape gives a licence and an author per file, and writes both into README.md
beside the images. One-off tool: the images are references for tuning the style
recipes, not application assets, so nothing here is embedded in the binary.
"""

import json
import pathlib
import urllib.parse
import urllib.request

OUT = pathlib.Path("reference/text-styles")
UA = "CutPro-reference-fetch/1.0 (local editing tool)"

# style id -> what to look for. Phrases describe the real-world thing the recipe
# imitates, because that is what the numbers in the catalog are aiming at.
QUERIES = {
    "outline": "outline lettering shop sign",
    "shadow": "painted lettering drop shadow sign",
    "extrude3d": "three dimensional block letters sign",
    "neon": "neon sign lettering night",
    "gold": "gold leaf lettering sign",
    "fire": "flaming letters fire text",
    "chrome": "chrome lettering car emblem",
    "sticker": "sticker lettering white border",
    "retro": "retro lettering sign 1950s",
}


def search(phrase, limit=2):
    url = "https://commons.wikimedia.org/w/api.php?" + urllib.parse.urlencode({
        "action": "query",
        "generator": "search",
        "gsrsearch": phrase,
        "gsrnamespace": 6,
        "gsrlimit": limit,
        "prop": "imageinfo",
        "iiprop": "url|extmetadata",
        "iiurlwidth": 1000,
        "format": "json",
    })
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=40) as reply:
        pages = json.load(reply).get("query", {}).get("pages", {})
    hits = sorted(pages.values(), key=lambda p: p.get("index", 99))
    out = []
    for page in hits:
        info = (page.get("imageinfo") or [{}])[0]
        meta = info.get("extmetadata", {})
        src = info.get("thumburl") or info.get("url")
        if not src:
            continue
        out.append({
            "title": page.get("title", "").removeprefix("File:"),
            "src": src.split("?")[0],
            "page": info.get("descriptionurl", ""),
            "licence": meta.get("LicenseShortName", {}).get("value", "see page"),
            "author": meta.get("Artist", {}).get("value", "see page"),
        })
    return out


def strip_html(value):
    out, depth = [], 0
    for ch in value:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            out.append(ch)
    return " ".join("".join(out).split())


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for style, phrase in QUERIES.items():
        for n, hit in enumerate(search(phrase), start=1):
            suffix = pathlib.PurePosixPath(
                urllib.parse.urlparse(hit["src"]).path).suffix.lower()
            if suffix not in (".jpg", ".jpeg", ".png", ".webp"):
                suffix = ".jpg"
            name = f"{style}-{n}{suffix}"
            req = urllib.request.Request(hit["src"], headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=60) as reply:
                data = reply.read()
            (OUT / name).write_bytes(data)
            rows.append((style, name, len(data), strip_html(hit["author"]),
                         hit["licence"], hit["page"]))
            print(f"{name}  {len(data) // 1024} KB")

    lines = [
        "# Letter-style reference photographs",
        "",
        "What the recipes in `src/app/text_effect_catalog.cpp` are imitating, one",
        "or two photographs per style id. References only: they are not embedded",
        "in the binary and nothing in the app reads this folder.",
        "",
        "Fetched from Wikimedia Commons by `tools/fetch_text_style_refs.py`.",
        "",
        "| style | file | author | licence | source |",
        "| --- | --- | --- | --- | --- |",
    ]
    for style, name, _size, author, licence, page in rows:
        lines.append(f"| `{style}` | [{name}]({name}) | {author or '-'} | "
                     f"{licence} | [Commons]({page}) |")
    (OUT / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\n{len(rows)} images -> {OUT}")


if __name__ == "__main__":
    main()
