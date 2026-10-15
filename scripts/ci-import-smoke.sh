#!/usr/bin/env bash
# End-to-end check of `tt2 --import` on a SYNTHETIC install: random bytes,
# never GameHouse files. Used by CI on Linux and Windows (MSYS2).
#
# Usage: scripts/ci-import-smoke.sh <tt2> <tt2-assets> <work dir>
#
# Every run passes --asset-dir, so the real per-user folder is never
# touched. Needs SDL_VIDEODRIVER=dummy / SDL_AUDIODRIVER=dummy in CI.
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <tt2> <tt2-assets> <work dir>" >&2
    exit 2
fi
TT2=$1
TT2_ASSETS=$2
WORK=$3
# MSYS2: $RUNNER_TEMP is a Windows path (D:\a\_temp). Use the POSIX form;
# MSYS2 converts it back when it passes arguments to tt2.exe.
if command -v cygpath > /dev/null 2>&1; then WORK=$(cygpath -u "$WORK"); fi

FAKE="$WORK/fake-install"
ASSETS="$WORK/assets"
MANIFEST="$WORK/fake.sha256"
rm -rf "$WORK"
mkdir -p "$FAKE/properties" "$FAKE/images/gui" "$FAKE/sound"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# The install: a probe file, some random "assets", and decoys that must
# never be copied (DRM / licensing names, at the root and in a folder).
echo '<resources/>' > "$FAKE/properties/resources.xml"
head -c 70000 /dev/urandom > "$FAKE/images/gui/fake_1.png"
head -c 1234 /dev/urandom > "$FAKE/images/gui/fake_2.png"
head -c 4096 /dev/urandom > "$FAKE/sound/fake.ogg"
echo junk > "$FAKE/texttwist2_RADRM.dll"
echo junk > "$FAKE/x.nfo"
echo junk > "$FAKE/images/gui/texttwist2_RADRM.dll"
echo junk > "$FAKE/images/gui/x.nfo"

"$TT2_ASSETS" manifest "$FAKE" > "$MANIFEST"

# 1. A clean import succeeds and stamps the folder.
"$TT2" --import "$FAKE" --asset-dir "$ASSETS" --asset-manifest "$MANIFEST" || fail "import exited $?"
[ -f "$ASSETS/import-stamp.txt" ] || fail "no import-stamp.txt"
"$TT2_ASSETS" verify "$ASSETS" "$MANIFEST" || fail "the copies don't verify"
if [ -n "$(find "$ASSETS" -iname '*radrm*' -o -iname '*.nfo')" ]; then
    fail "a decoy DRM / .nfo file was copied"
fi

# 2. The game honours the stamp: no prompt on the next launch.
LOG="$WORK/launch.log"
"$TT2" --asset-dir "$ASSETS" --asset-manifest "$MANIFEST" --save-dir "$WORK/save" --exit-after 1500 > "$LOG"
grep -q '(user import, imported)' "$LOG" || { cat "$LOG"; fail "the import was not recognised"; }

# 3. One flipped byte: the re-import fails and keeps the previous import.
printf 'X' | dd of="$FAKE/images/gui/fake_2.png" bs=1 seek=10 conv=notrunc 2>/dev/null
if "$TT2" --import "$FAKE" --asset-dir "$ASSETS" --asset-manifest "$MANIFEST"; then
    fail "a changed file was imported"
fi
"$TT2_ASSETS" verify "$ASSETS" "$MANIFEST" || fail "the previous import was damaged"

# 4. An empty asset folder asks for an import.
"$TT2" --asset-dir "$WORK/empty" --asset-manifest "$MANIFEST" --save-dir "$WORK/save" --exit-after 1500 > "$LOG"
grep -q 'import needed' "$LOG" || { cat "$LOG"; fail "no import prompt for an empty folder"; }

echo "import smoke: ok"
