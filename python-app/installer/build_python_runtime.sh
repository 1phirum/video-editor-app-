#!/usr/bin/env bash
# Assemble the embeddable Python runtime that ships inside "Cut Pro 2026/python".
#
# The shipped app carries its own interpreter so a client needs no system Python:
# it covers the cloud features (Gemini transcription, translation, Text to Speech)
# whose only third-party need is edge-tts. Whisper/Demucs are NOT bundled (torch is
# gigabytes); a power user installs those into their own Python and points
# Settings > General > "Python executable" at it.
#
# Two properties this script guarantees, both load-bearing for the in-app
# "Requirements" check to read accurately:
#   * The interpreter version matches the dev machine's Python, so pip fetches
#     matching cp3xx win_amd64 wheels for edge-tts's compiled deps (aiohttp).
#   * python3xx._pth does NOT enable `import site`. That keeps the bundle fully
#     isolated: it never reads a developer's (or client's) global/user
#     site-packages, so "is edge_tts installed" reflects only what is bundled.
#
# Usage:  bash installer/build_python_runtime.sh
set -euo pipefail

DIST="$(cd "$(dirname "$0")/.." && pwd)/dist/Cut Pro 2026"
PYDIR="$DIST/python"
PYVER="$(python -c 'import sys;print("%d.%d.%d"%sys.version_info[:3])')"
TAG="python$(python -c 'import sys;print("%d%d"%sys.version_info[:2])')"   # e.g. python313
ZIP_URL="https://www.python.org/ftp/python/${PYVER}/python-${PYVER}-embed-amd64.zip"
TMP_ZIP="$(mktemp -u).zip"

echo "Bundling embeddable Python ${PYVER} into: $PYDIR"
rm -rf "$PYDIR"; mkdir -p "$PYDIR"
curl -fL -o "$TMP_ZIP" "$ZIP_URL"
powershell -NoProfile -Command "Expand-Archive -Force -LiteralPath '$(cygpath -w "$TMP_ZIP")' -DestinationPath '$(cygpath -w "$PYDIR")'"
rm -f "$TMP_ZIP"

# Isolated path config: expose the bundle's own site-packages, and nothing else.
# No `import site` line -> no user/global site-packages leak.
printf '%s.zip\n.\nLib\\site-packages\n' "$TAG" > "$PYDIR/${TAG}._pth"

# Cloud-feature dependency. --target fetches wheels for THIS Python's ABI, which
# matches the embeddable of the same version.
mkdir -p "$PYDIR/Lib/site-packages"
python -m pip install --disable-pip-version-check --no-input \
    --target "$PYDIR/Lib/site-packages" "edge-tts==7.3.1"

# Bootstrap pip INTO the bundle so the app can install the optional offline
# engines (Whisper/Demucs) on demand, into this same bundle - no system Python
# and no manual commands required. The embeddable distribution omits pip and
# ensurepip, so get-pip.py is the supported way in.
GETPIP="$(mktemp -u).py"
curl -fL -o "$GETPIP" https://bootstrap.pypa.io/get-pip.py
"$PYDIR/python.exe" "$GETPIP" --no-warn-script-location
rm -f "$GETPIP"

echo "Done. Verifying isolation and pip..."
"$PYDIR/python.exe" -c "import edge_tts, aiohttp; print('bundle OK:', edge_tts.__version__)"
"$PYDIR/python.exe" -m pip --version
