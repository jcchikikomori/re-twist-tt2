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
cp -R "$FAKE" "$FAKE.good"   # step 3 damages $FAKE; step 5 imports this copy

# 1. A clean import succeeds and stamps the folder.
"$TT2" --import "$FAKE" --asset-dir "$ASSETS" --asset-manifest "$MANIFEST" || fail "import exited $?"
[ -f "$ASSETS/import-stamp.txt" ] || fail "no import-stamp.txt"
"$TT2_ASSETS" verify "$ASSETS" "$MANIFEST" || fail "the copies don't verify"
if [ -n "$(find "$ASSETS" -iname '*radrm*' -o -iname '*.nfo')" ]; then
    fail "a decoy DRM / .nfo file was copied"
fi

# 2. The game honours the stamp: no prompt when the menu shows (~6.6 s in).
LOG="$WORK/launch.log"
"$TT2" --asset-dir "$ASSETS" --asset-manifest "$MANIFEST" --save-dir "$WORK/save" --exit-after 8000 > "$LOG"
grep -q '(user import, imported)' "$LOG" || { cat "$LOG"; fail "the import was not recognised"; }
grep -q 'scene 2' "$LOG" || { cat "$LOG"; fail "the menu never showed"; }
if grep -q 'prompt shown' "$LOG"; then cat "$LOG"; fail "asked to import again"; fi

# 3. One flipped byte: the re-import fails and keeps the previous import.
printf 'X' | dd of="$FAKE/images/gui/fake_2.png" bs=1 seek=10 conv=notrunc 2>/dev/null
if "$TT2" --import "$FAKE" --asset-dir "$ASSETS" --asset-manifest "$MANIFEST"; then
    fail "a changed file was imported"
fi
"$TT2_ASSETS" verify "$ASSETS" "$MANIFEST" || fail "the previous import was damaged"

# 4. An empty asset folder asks once the menu shows; '~' answers NO.
"$TT2" --asset-dir "$WORK/empty" --asset-manifest "$MANIFEST" --save-dir "$WORK/save" --type '~' \
    --exit-after 8000 > "$LOG"
grep -q 'import needed' "$LOG" || { cat "$LOG"; fail "no import needed for an empty folder"; }
grep -q 'assets: prompt shown' "$LOG" || { cat "$LOG"; fail "the prompt never showed"; }
grep -q 'assets: prompt closed' "$LOG" || { cat "$LOG"; fail "NO did not close the prompt"; }

# 5. The in-game import, end to end: YES, the picker answered by
#    --import-pick, verify + copy, free the loaded assets, swap the folders,
#    reload them live; then OK. The next launch doesn't ask.
LIVE="$WORK/live-assets"
"$TT2" --asset-dir "$LIVE" --asset-manifest "$MANIFEST" --save-dir "$WORK/save" --import-pick "$FAKE.good" \
    --type '||' --exit-after 9000 > "$LOG" 2>&1 || { cat "$LOG"; fail "the game exited $?"; }
grep -q 'assets: reloaded from' "$LOG" || { cat "$LOG"; fail "no live reload after the in-game import"; }
grep -q 'assets: prompt closed' "$LOG" || { cat "$LOG"; fail "OK did not close the result"; }
"$TT2_ASSETS" verify "$LIVE" "$MANIFEST" || fail "the in-game import doesn't verify"
"$TT2" --asset-dir "$LIVE" --asset-manifest "$MANIFEST" --save-dir "$WORK/save" --exit-after 8000 > "$LOG"
if grep -q 'prompt shown' "$LOG"; then cat "$LOG"; fail "asked again after the in-game import"; fi

# 6. Re-import from the main menu's SETTINGS (deviation OP-7): NO at
#    launch, '#' SETTINGS, '&' IMPORT (straight to the picker), '|' OK.
#    --player skips the name dialog, which would hold the keys.
AGAIN="$WORK/settings-assets"
"$TT2" --asset-dir "$AGAIN" --asset-manifest "$MANIFEST" --save-dir "$WORK/save" --import-pick "$FAKE.good" \
    --player CI --type '~#&|' --exit-after 11000 > "$LOG" 2>&1 || { cat "$LOG"; fail "the game exited $?"; }
grep -q 'assets: re-import from settings' "$LOG" || { cat "$LOG"; fail "IMPORT in SETTINGS did nothing"; }
grep -q 'assets: reloaded from' "$LOG" || { cat "$LOG"; fail "no live reload after the SETTINGS import"; }
"$TT2_ASSETS" verify "$AGAIN" "$MANIFEST" || fail "the SETTINGS import doesn't verify"

echo "import smoke: ok"
