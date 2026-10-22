#!/bin/sh
# Builds the open dictionaries in data/ from the word lists in data-src/
# (docs/dictionary-sources.md). Needs a built tt2-tree; no downloads.
#
# Usage: scripts/build-open-dictionary.sh [tt2-tree]   (default: build/tt2-tree)
#
#   data/open-common.tree  written: data-src/common.words minus
#                          data-src/blocklist.words.
#   data/open-full.tree    checked, never written: the run fails if a fresh
#                          encode of data-src/enable.words differs from it.
#
# It also checks that the committed word lists still match their raw sources:
#   enable.words = enable2k.raw.txt, lowercased, a-z only, sorted, unique;
#   common.words = the 3-8 letter enable.words that are in en_50k.raw.txt.
# The output is deterministic: a second run writes the same bytes.
set -eu

TT2_TREE=${1:-build/tt2-tree}
case $TT2_TREE in
    /*) ;;
    *) TT2_TREE=$PWD/$TT2_TREE ;;
esac
if [ "$#" -gt 1 ] || [ ! -x "$TT2_TREE" ]; then
    echo "usage: $0 [tt2-tree]   (no executable tt2-tree at $TT2_TREE)" >&2
    exit 2
fi

cd "$(dirname "$0")/.."
LC_ALL=C   # byte order for sort and comm, the order .tree files use
export LC_ALL

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
trap 'exit 1' HUP INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# 1. The committed word lists still match their raw sources.
tr '[:upper:]' '[:lower:]' < data-src/enable2k.raw.txt | tr -d '\r' | grep -E '^[a-z]+$' | sort -u > "$WORK/enable.words"
cmp -s "$WORK/enable.words" data-src/enable.words ||
    fail "data-src/enable.words no longer matches data-src/enable2k.raw.txt"
cut -d ' ' -f 1 data-src/en_50k.raw.txt | sort -u > "$WORK/en_50k.words"
grep -E '^[a-z]{3,8}$' data-src/enable.words | comm -12 - "$WORK/en_50k.words" > "$WORK/common.words"
cmp -s "$WORK/common.words" data-src/common.words ||
    fail "data-src/common.words no longer matches enable.words (3-8 letters) and en_50k.raw.txt"

# 2. open-full.tree is the whole ENABLE2k list and must not change.
"$TT2_TREE" encode data-src/enable.words "$WORK/open-full.tree"
cmp -s "$WORK/open-full.tree" data/open-full.tree ||
    fail "a fresh encode of data-src/enable.words differs from data/open-full.tree"

# 3. The blocklist: lowercase a-z words that are all in common.words.
sed -e '/^#/d' -e '/^$/d' data-src/blocklist.words > "$WORK/blocklist.raw"
if grep -v -E '^[a-z]+$' "$WORK/blocklist.raw" > "$WORK/bad"; then
    fail "data-src/blocklist.words has lines that aren't lowercase a-z words: $(tr '\n' ' ' < "$WORK/bad")"
fi
sort -u "$WORK/blocklist.raw" > "$WORK/blocklist.words"
comm -13 data-src/common.words "$WORK/blocklist.words" > "$WORK/dead"
if [ -s "$WORK/dead" ]; then
    fail "data-src/blocklist.words lists words that aren't in common.words: $(tr '\n' ' ' < "$WORK/dead")"
fi

# 4. open-common.tree: common.words minus the blocklist.
comm -23 data-src/common.words "$WORK/blocklist.words" > "$WORK/open-common.words"
"$TT2_TREE" encode "$WORK/open-common.words" "$WORK/open-common.tree"
cp "$WORK/open-common.tree" data/open-common.tree
"$TT2_TREE" verify data/open-common.tree
echo "open-common.tree: $(wc -l < "$WORK/open-common.words" | tr -d ' ') words" \
    "($(wc -l < "$WORK/blocklist.words" | tr -d ' ') blocked; open-full.tree keeps them)"
