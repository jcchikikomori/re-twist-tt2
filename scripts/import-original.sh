#!/usr/bin/env sh
# Import the images, fonts, music and sounds the UI needs from the user's
# OWN Text Twist 2 install. A thin wrapper around `tt2 --import`, which
# checks every file against data/original-assets.sha256 and copies them
# into the per-user asset folder (~/.local/share/re-twist-tt2/imported-assets
# on Linux; %APPDATA%\re-twist-tt2\imported-assets on Windows).
#
# Only run this against a copy you legally own. The repository never
# ships these files, and whether your licence allows extracting them
# is your responsibility, not the project's. The game itself also asks
# on launch, so this script is optional.
#
# Usage: scripts/import-original.sh "/path/to/Text Twist 2" [tt2 flags...]
#   e.g. --asset-dir DIR to import somewhere else.
# The tt2 binary is $TT2, else build/tt2, else tt2 on the PATH.
set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 /path/to/Text Twist 2 [--asset-dir DIR]" >&2
    exit 2
fi
SRC=$1
shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)

if [ -n "${TT2:-}" ]; then
    BIN=$TT2
elif [ -x "$ROOT/build/tt2" ]; then
    BIN=$ROOT/build/tt2
elif command -v tt2 > /dev/null 2>&1; then
    BIN=tt2
else
    echo "no tt2 binary: build it first (cmake -S . -B build && cmake --build build) or set TT2" >&2
    exit 2
fi

exec "$BIN" --import "$SRC" "$@"
