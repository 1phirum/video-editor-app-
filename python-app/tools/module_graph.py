"""Validate a proposed DLL split of the src tree.

Run from python-app:  python tools/module_graph.py [--split]

Without --split, prints the raw per-directory include graph.  With --split,
maps every file onto the proposed CMake target list below and prints the
resulting target graph plus any mutual pair.  MinGW resolves an import library
at link time, so two DLLs that include each other's headers cannot both be
linked last: the mutual-pair list must be empty before the split is safe.
"""

import collections
import os
import re
import sys

SRC = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

# Proposed target for a file, first match wins.  Prefixes are src-relative and
# use forward slashes.
SPLIT = [
    # QtQuick-dependent diagnostics. Kept out of "diagnostics" on purpose:
    # cutpro_backend links diagnostics publicly and must not acquire a
    # Qt6::Quick dependency, which is the whole reason the launcher owns the
    # image providers and AppCursor today.
    ("app/diagnostics/item_tree_census", "scene"),
    ("app/diagnostics/diagnostics_bridge", "scene"),
    ("app/diagnostics/", "diagnostics"),
    ("app/preview/gui_dispatch", "diagnostics"),
    ("app/preview/gui_stall", "diagnostics"),
    ("app/preview/gui_thread_watchdog", "diagnostics"),
    ("core/", "core"),
    ("app/caption_style", "core"),
    ("app/settings/backend_settings", "backend"),
    ("app/settings/", "settings"),
    ("app/media/", "media"),
    ("app/subtitles/", "subtitles"),
    ("app/effects/", "effects"),
    ("app/lumetri/", "lumetri"),
    ("app/timeline/", "timeline"),
    ("app/preview/timeline_thumbnail_provider", "launcher"),
    ("app/preview/waveform_window_provider", "launcher"),
    ("app/preview/", "preview"),
    ("app/export/", "export"),
    ("app/core_app/", "backend"),
    ("app/project/", "backend"),
    ("app/ui/", "launcher"),
    ("main.cpp", "launcher"),
    ("tools/", "tools"),
]


def directory_module(rel):
    parts = rel.split("/")
    if parts[0] == "core":
        return "core"
    if parts[0] == "app" and len(parts) > 2:
        return parts[1]
    if parts[0] == "app":
        return "app_root"
    return parts[0]


def split_module(rel):
    for prefix, target in SPLIT:
        if rel.startswith(prefix):
            return target
    return "UNMAPPED"


def collect(classify):
    owner = {}
    for root, _dirs, names in os.walk(SRC):
        for name in names:
            if not name.endswith((".cpp", ".h")):
                continue
            rel = os.path.relpath(os.path.join(root, name), SRC)
            owner[rel.replace("\\", "/")] = None
    for rel in owner:
        owner[rel] = classify(rel)
    return owner


def graph(owner):
    include_re = re.compile(r'#include\s+"([^"]+)"')
    edges = collections.defaultdict(set)
    detail = collections.defaultdict(set)
    for rel, mod in owner.items():
        try:
            text = open(os.path.join(SRC, rel), encoding="utf-8",
                        errors="replace").read()
        except OSError:
            continue
        for inc in include_re.findall(text):
            target = owner.get(inc.replace("\\", "/"))
            if target and target != mod:
                edges[mod].add(target)
                detail[(mod, target)].add(rel + " -> " + inc)
    return edges, detail


def report(owner, edges, detail):
    counts = collections.Counter(owner.values())
    mods = sorted(counts)
    print("modules (files):")
    for mod in mods:
        print("  %-13s %d" % (mod, counts[mod]))
    print()
    for mod in mods:
        print("%-13s -> %s" % (mod, ", ".join(sorted(edges[mod])) or "-"))
    print()
    print("=== mutual pairs ===")
    found = False
    for a in mods:
        for b in sorted(edges[a]):
            if a < b and a in edges.get(b, ()):
                found = True
                print("  %s <-> %s" % (a, b))
                for line in sorted(detail[(a, b)])[:8]:
                    print("      " + line)
                for line in sorted(detail[(b, a)])[:8]:
                    print("      " + line)
    if not found:
        print("  none")


def emit_cmake(owner):
    """Print each target's file list in CMake syntax.

    Transcribing 100+ paths by hand into CMakeLists.txt is how a split loses a
    translation unit and gains an hour of link errors; this prints exactly what
    the mapping above says, so the two cannot disagree.
    """
    by_target = collections.defaultdict(list)
    for rel, mod in owner.items():
        by_target[mod].append(rel)
    for mod in sorted(by_target):
        files = sorted(by_target[mod],
                       key=lambda p: (not p.endswith(".cpp"), p))
        print("# --- %s (%d files) ---" % (mod, len(files)))
        for rel in files:
            print("    src/%s" % rel)
        print()


def main():
    use_split = "--split" in sys.argv or "--cmake" in sys.argv
    owner = collect(split_module if use_split else directory_module)
    if use_split and "UNMAPPED" in owner.values():
        print("!! unmapped files:")
        for rel, mod in sorted(owner.items()):
            if mod == "UNMAPPED":
                print("   " + rel)
        print()
    if "--cmake" in sys.argv:
        emit_cmake(owner)
        return
    edges, detail = graph(owner)
    report(owner, edges, detail)


if __name__ == "__main__":
    main()
