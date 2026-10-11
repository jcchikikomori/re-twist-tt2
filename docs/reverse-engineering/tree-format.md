# Text Twist 2 `.tree` word-file format

## TL;DR

A `.tree` file is a **pre-order serialized trie**, plain ASCII, with no header, no length
field, no checksum and no trailing newline.

```
file   := node                      ; the root node (empty prefix)
node   := LETTER* FLAG child*       ; one child node per LETTER, same order
LETTER := 'a'..'z'                  ; children of this node, ascending order
FLAG   := '0' | '1'                 ; '1' = the prefix spelled so far is a word
```

- Each node writes its sorted child letters, then a flag saying whether the path
  to this node is a complete word. After that, its children are serialized
  depth-first in the same order.
- A leaf is just `1`. A leaf that isn't a word can't exist in a canonical trie,
  so you never see a bare `0`.
- Word count = number of `1` bytes. Pre-order traversal gives words in sorted
  order.
- The parser ends when the root's subtree is done. That lands exactly on EOF
  for both files.

## The "header lines" aren't a header

`abcdefghijklmnopqrstuvwyz0` is just the **root node**. It lists the letters
words can start with, and its flag is `0` because the empty string isn't a word.
TextTwist2Words has **no words starting with `x`**, so `x` is missing.
`abcdefghijklmnopqrstuvwxyz0` is the **`a` node**: `aa`… through `az`… all
appear as prefixes, and `a` alone isn't a word. Nothing else is hidden there.
Both "headers" fall out of the encoder naturally.

## Worked example: first 60 bytes of TextTwist2Words.tree

```
abcdefghijklmnopqrstuvwyz0abcdefghijklmnopqrstuvwxyz0r0d0v0a
```
plus the bytes that follow (`r0k01abdehijlnorsuy0clnst0ku01s0e1s01o0n0e0s11...`):

| bytes                       | node (prefix) | children           | flag | meaning |
|-----------------------------|---------------|--------------------|------|---------|
| `abcdefghijklmnopqrstuvwyz0`| ""  (root)    | a..w,y,z (no x)    | 0    | |
| `abcdefghijklmnopqrstuvwxyz0`| `a`          | a..z               | 0    | "a" not a word |
| `r0`                        | `aa`          | r                  | 0    | |
| `d0`                        | `aar`         | d                  | 0    | |
| `v0`                        | `aard`        | v                  | 0    | |
| `a0`                        | `aardv`       | a                  | 0    | |
| `r0`                        | `aardva`      | r                  | 0    | |
| `k0`                        | `aardvar`     | k                  | 0    | |
| `1`                         | `aardvark`    | –                  | 1    | **aardvark** |
| `abdehijlnorsuy0`           | `ab`          | a,b,d,e,h,i,j,l,n,o,r,s,u,y | 0 | back up to `a`'s 2nd child |
| `clnst0`                    | `aba`         | c,l,n,s,t          | 0    | |
| `ku0`                       | `abac`        | k,u                | 0    | |
| `1`                         | `aback`       | –                  | 1    | **aback** |
| `s0`                        | `abacu`       | s                  | 0    | |
| `e1`                        | `abacus`      | e                  | 1    | **abacus** |
| `s0`                        | `abacuse`     | s                  | 0    | |
| `1`                         | `abacuses`    | –                  | 1    | **abacuses** |
| `o0` `n0` `e0`              | `abal`,`abalo`,`abalon` | o / n / e | 0  | |
| `s1`                        | `abalone`     | s                  | 1    | **abalone** |
| `1`                         | `abalones`    | –                  | 1    | **abalones** |
| `d0`…                       | `aban`        | d                  | 0    | → abandon, abandons, … |

## Round trip

`tree_encode.py` builds a trie from the word list and writes it pre-order, with
children sorted by byte. The format is canonical: one word set maps to exactly
one byte stream. So the round trip is **byte-identical**:

| file | size | sha256 | round trip |
|------|------|--------|------------|
| TextTwist2Words.tree | 95477  | 5d13e77a…91027b | identical |
| DamBeaversWords.tree | 470839 | 585f7648…f43b11 | identical |

## Contents and likely roles

| file | words | length range | distribution |
|------|-------|--------------|--------------|
| TextTwist2Words | 25,004  | 3–15 | 3:574 4:2243 5:4113 6:6130 7:7094 8:4711, then 9+: only 139 |
| DamBeaversWords | 116,757 | 2–16 | 2:101 … peaks at 8 (27,265), 15:464, 16:1 (`mathematizations`) |

- **TextTwist2Words is a strict subset of DamBeaversWords**: 0 words appear only
  in TT2, and 91,753 appear only in DamBeavers.
- **TextTwist2Words** looks like the curated *common-word* list for Text Twist
  itself: 3+ letters, mostly up to 8, matching the game's rack sizes. The 139
  words with 9+ letters come in odd clusters (`actualiz*`, `lithograph*`,
  `rightful*`, `verif*`), which I'd guess are leftovers rather than intended rack
  words. This is my reading; I didn't check it against the exe. I'd expect this
  file to serve as both the rack/source-word list and the accepted-guess list.
- **DamBeaversWords** looks like a full Scrabble-style (TWL-like) dictionary.
  It has the two-letter words (`aa ab ad ae ag …`), lengths up to 15, and
  `zyzzyvas`. The name points to the bundled/companion "Dam Beavers" word game,
  not Text Twist 2's own word check. That's inferred from the filename and
  contents only.

## Tools

- `python3 tree_decode.py <file.tree>` prints sorted words, one per line.
- `python3 tree_decode.py --stats <file.tree>` prints the count, min/max length,
  and a per-length histogram.
- `python3 tree_encode.py <words.txt> <out.tree>` takes lowercase a–z words, one
  per line (deduplicated).
