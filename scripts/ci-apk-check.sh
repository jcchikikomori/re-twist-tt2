#!/usr/bin/env bash
# Checks what an APK carries before CI keeps or publishes it.
#
# Usage: scripts/ci-apk-check.sh <apk> <original-assets manifest>
#
# 1. The native libraries, the open dictionaries and the bundled manifest
#    are there, and so are the licence texts (assets/licenses/, written by
#    the bundleLicenses Gradle task).
# 2. Every file under assets/ is on the allow-list below. data/ in the source
#    tree may hold the player's own GameHouse common.tree / full.tree; a
#    build that picked them up fails here.
# 3. No entry has the SHA-256 of a file listed in the manifest (the original
#    game's files, paths and hashes only). Names alone can't tell: the
#    placeholder art reuses the original file names on purpose.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <apk> <original-assets manifest>" >&2
    exit 2
fi
APK=$1
MANIFEST=$2

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$APK" ] || fail "no APK at $APK"
grep -q '^# re-twist-tt2 original-asset manifest' "$MANIFEST" || fail "$MANIFEST is not an asset manifest"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
unzip -Z1 "$APK" | sort > "$WORK/entries"

# 1. What must be there.
for f in lib/arm64-v8a/libmain.so lib/arm64-v8a/libSDL3.so \
    assets/data/open-common.tree assets/data/open-full.tree assets/data/LICENSE.md \
    assets/data/original-assets.sha256 assets/data/bundle-manifest.txt \
    assets/licenses/re-twist-tt2.txt assets/licenses/super-xbr.txt assets/licenses/kotlin-stdlib.txt \
    assets/licenses/SDL3.txt assets/licenses/SDL3_image.txt assets/licenses/SDL3_mixer.txt; do
    grep -qxF "$f" "$WORK/entries" || fail "missing $f"
done

# 2. The allow-list for assets/.
grep '^assets/' "$WORK/entries" | grep -vE \
    '^assets/data/(open-common\.tree|open-full\.tree|LICENSE\.md|original-assets\.sha256|bundle-manifest\.txt)$|^assets/data/placeholder/|^assets/licenses/[A-Za-z0-9_.-]+\.txt$' \
    > "$WORK/unexpected" || true
if [ -s "$WORK/unexpected" ]; then
    cat "$WORK/unexpected" >&2
    fail "files under assets/ that are not on the allow-list"
fi

# 3. No original file, under any name.
awk '!/^#/ && NF >= 2 { print $1 }' "$MANIFEST" | sort -u > "$WORK/banned"
[ -s "$WORK/banned" ] || fail "$MANIFEST lists no files"
mkdir "$WORK/x"
unzip -q "$APK" -d "$WORK/x"
(cd "$WORK/x" && find . -type f -print0 | xargs -0 sha256sum) | sort > "$WORK/hashes"
awk 'NR == FNR { banned[$1] = 1; next } ($1 in banned) { print }' "$WORK/banned" "$WORK/hashes" > "$WORK/matches"
if [ -s "$WORK/matches" ]; then
    cat "$WORK/matches" >&2
    fail "the APK holds files from the original game"
fi

echo "ok: $(wc -l < "$WORK/entries") entries, $(wc -l < "$WORK/banned") original hashes, none found"
