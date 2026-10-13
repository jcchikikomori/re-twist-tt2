#!/usr/bin/env sh
# Copy the images, music and sounds the UI needs out of the user's OWN
# Text Twist 2 install into `imported-assets/` (gitignored).
#
# Only run this against a copy you legally own. The repository never
# ships these files, and whether your licence allows extracting them
# is your responsibility, not the project's. Without them the game
# boots with procedural placeholders.
#
# Usage: scripts/import-original.sh "/path/to/Text Twist 2"
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/Text Twist 2" >&2
    exit 2
fi
SRC=$1
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DST="$ROOT/imported-assets"

if [ ! -f "$SRC/properties/resources.xml" ]; then
    echo "$SRC does not look like a Text Twist 2 install (no properties/resources.xml)" >&2
    exit 1
fi

copy_dir() {
    [ -d "$SRC/$1" ] || { echo "skip $1 (missing)"; return 0; }
    mkdir -p "$DST/$1"
    cp -R "$SRC/$1/." "$DST/$1/"
    echo "copied $1"
}

copy_dir images/splash
copy_dir images/loadscene
copy_dir images/selectionscene
copy_dir music
copy_dir sound

echo "done: $DST"
echo "git status must not list anything under imported-assets/."
