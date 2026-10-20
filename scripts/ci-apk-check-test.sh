#!/usr/bin/env bash
# Proves scripts/ci-apk-check.sh catches what it is there to catch, on
# doctored copies of a good APK. The planted files are random bytes; the
# "original" one is only in a copy of the manifest made here.
#
# Usage: scripts/ci-apk-check-test.sh <good apk> <original-assets manifest>
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <good apk> <original-assets manifest>" >&2
    exit 2
fi
CHECK="$(cd "$(dirname "$0")" && pwd)/ci-apk-check.sh"
APK=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
MANIFEST=$2

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# A doctored copy of the APK must fail the check, for the given reason.
expect_fail() {
    local name=$1 manifest=$2 reason=$3
    if "$CHECK" "$WORK/$name.apk" "$manifest" 2> "$WORK/$name.err"; then
        fail "$name: the check passed"
    fi
    grep -q "$reason" "$WORK/$name.err" || { cat "$WORK/$name.err" >&2; fail "$name: wrong reason"; }
    echo "ok: $name fails ($reason)"
}

# 0. The good APK passes.
"$CHECK" "$APK" "$MANIFEST" || fail "the good APK fails the check"

# 1. A file whose hash the manifest lists, under a placeholder's name.
mkdir -p "$WORK/hash/assets/data/placeholder/images"
head -c 4096 /dev/urandom > "$WORK/hash/assets/data/placeholder/images/planted.png"
cp "$APK" "$WORK/hash.apk"
(cd "$WORK/hash" && zip -q "$WORK/hash.apk" assets/data/placeholder/images/planted.png)
{ cat "$MANIFEST"; echo "$(sha256sum < "$WORK/hash/assets/data/placeholder/images/planted.png" | cut -d' ' -f1)  images/planted.png"; } \
    > "$WORK/hash.sha256"
expect_fail hash "$WORK/hash.sha256" "files from the original game"

# 2. The player's own full.tree picked up from data/.
mkdir -p "$WORK/name/assets/data"
head -c 4096 /dev/urandom > "$WORK/name/assets/data/full.tree"
cp "$APK" "$WORK/name.apk"
(cd "$WORK/name" && zip -q "$WORK/name.apk" assets/data/full.tree)
expect_fail name "$MANIFEST" "not on the allow-list"

# 3. A licence text left out.
cp "$APK" "$WORK/licence.apk"
zip -q -d "$WORK/licence.apk" assets/licenses/SDL3_mixer.txt
expect_fail licence "$MANIFEST" "missing assets/licenses/SDL3_mixer.txt"
