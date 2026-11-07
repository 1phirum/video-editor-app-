#!/usr/bin/env python3
# Drafts Khmer translations into cutpro_km.ts.
#
# This is a one-time drafting aid, not part of the build. It walks every
# <message> in the .ts, and when the English <source> exactly matches a key in
# KM below it writes the Khmer into <translation> and clears the "unfinished"
# flag. Sources with no entry are left untouched (still unfinished), so Qt falls
# back to the English source for them - the UI is never blank.
#
# The .ts remains the editable source of truth: refine wording there directly.
# Re-running this only fills entries it has a key for; it never overwrites a
# translation you have already edited by hand (it only touches unfinished ones).
#
#   python i18n/fill_km.py
#
# Brand/proper nouns (Gemini, FFmpeg, TikTok, Demucs, MDX-Net, codec names) and
# pure unit/number strings (px, "60 ms", "24 fps") are intentionally absent -
# they read the same in Khmer, so English fallback is correct.

import re
import html
import sys
from pathlib import Path

TS = Path(__file__).with_name("cutpro_km.ts")

# English source -> Khmer draft. Trailing spaces on concat fragments are
# preserved deliberately.
KM = {}


def add(mapping):
    KM.update(mapping)


# --- filled in by the chunks below ---
import fill_km_data  # noqa: E402  (dictionary lives in a sibling module)
add(fill_km_data.DICT)


def main():
    if not TS.exists():
        sys.exit(f"not found: {TS}")
    text = TS.read_text(encoding="utf-8")

    filled = [0]
    skipped_existing = [0]

    def repl(m):
        block = m.group(0)
        src_m = re.search(r"<source>(.*?)</source>", block, re.S)
        if not src_m:
            return block
        src = html.unescape(src_m.group(1))
        km = KM.get(src)
        if km is None:
            return block
        # Do not clobber a translation the user has already finished.
        tr_m = re.search(r"<translation(.*?)>(.*?)</translation>", block, re.S)
        if tr_m and "unfinished" not in tr_m.group(1) and tr_m.group(2).strip():
            skipped_existing[0] += 1
            return block
        esc = html.escape(km, quote=False)
        new_tr = f"<translation>{esc}</translation>"
        filled[0] += 1
        return re.sub(r"<translation.*?</translation>", new_tr, block, flags=re.S)

    text = re.sub(r"<message>.*?</message>", repl, text, flags=re.S)
    TS.write_text(text, encoding="utf-8")
    print(f"filled {filled[0]} translation(s); "
          f"kept {skipped_existing[0]} already-finished; "
          f"{len(KM)} dictionary entries")


if __name__ == "__main__":
    main()
